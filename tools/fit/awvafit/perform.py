# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Musical test performances from real playing: MIDI 2.0 performances derived from annotated
recordings (ChoraleBricks now; URMP when it is on disk), written as UMP text (awva_sdk/performance.h: ump_player) that awva_render
--ump plays (and any MIDI 2.0 host can). They are what to listen to and what the musical smoke tests
play; the scripted perf_*.h performances are bit-exactness stress scripts, for regression only.

What a performance takes from its recording, per note:
  onset, offset    the annotated note (ChoraleBricks: notes.csv)
  pitch            the annotated f0 (glides and vibrato kept) less the track's tuning offset (the
                   median of the notes' deviations: the players tune above A = 440), as Registered
                   Per-Note Controller 3 (Pitch 7.25), every 10 ms where it moves 0.5 cent
  pressure         the 10 ms level envelope, P = 0.4 x 10^((L - L_median) / 40), 0.05 .. 1 (the phrase
                   sets' mapping), as Poly Pressure (32 bit, 1.0 = full), every 20 ms where it moves
  strike           a slurred note 0.1, else from how fast its attack rises (keys.py's rule), as the
                   Note On's 16-bit velocity
  articulation     slurred where the recording's level dips under 6 dB at the change and the pitch
                   moves: the next Note On 30 ms before the last Note Off; else re-tongued: the Note
                   Off 20 ms before the next onset
The control is inferred from the recording: a plausible player, not measured control.

Per family a short curated set (10-20 s each, phrase boundaries, the track with the cleanest
recording among those that have such a passage): a slow legato line (the most slurred changes), a
swell (the widest level change within one held note), a tongued passage (the most re-tongued changes per second), a
lyrical line (the deepest vibrato, 4-8 Hz in the f0; where a family has none, said so).

The file: UMP text, one packet a line '<frame> <word0> <word1>' (frames at 48 kHz, hex words), and
comments: '# ump frames at 48000 Hz', '# key: value' metadata, '# note <onset> <offset> <midi> <cents>
<art>' (the score: seconds, cents re equal temperament after the tuning offset).

  write_set(family, out_dir) -> [paths];   read_perf(path) -> (meta, notes, events)
"""
import csv
import json
import math
import os

import numpy as np

from . import labels, sources
from .audio import read as read_audio
from .keys import K

FAMILIES = {  # family: (ChoraleBricks instrument, engine, preset on main), the instrument the set plays
    'brass': ('Trumpet', 'brass', 'trumpet'),
    'sax': ('Alto Saxophone', 'wind', 'alto sax'),
    'clarinet': ('Clarinet', 'wind', 'clarinet'),
    'flute': ('Flute', 'wind', 'flute'),
    'doublereed': ('Oboe', 'wind', 'oboe'),
}
KINDS = ('legato', 'swell', 'tongued', 'lyrical')
HOP = 0.01
BREATH, CONNECT = 0.25, 0.12
LEAD = 0.25  # s before the first onset
CH = 0

UMP_RATE = 48000  # the packets' frames (awva_render plays the performances at 48 kHz)


def _cb_root():
    return sources.path('choralebricks', '01_AudioAndAnnotations')


def tracks(instrument):
    rows = list(csv.DictReader(open(os.path.join(_cb_root(), 'metadata_tracks.csv'), encoding='utf-8'), delimiter=';'))
    q = {}
    qp = os.path.join(os.path.dirname(sources.path('choralebricks', 'x')), '..', 'quality.csv')
    if os.path.exists(qp):
        q = {(r['song'], r['file']): r for r in csv.DictReader(open(qp, encoding='utf-8'))}
    out = [r for r in rows if r['instrument'] == instrument]
    for r in out:
        qq = q.get((r['song_id'], r['path_audio']), {})
        r['floor_db'] = float(qq['floor_db']) if qq.get('floor_db') else 0.0
    return sorted(out, key=lambda r: r['floor_db'])


def env_db(x, sr):
    n = int(HOP * sr)
    m = len(x) // n
    return 10 * np.log10(np.mean(x[:m * n].reshape(m, n) ** 2, 1) + 1e-14)


class Track:
    def __init__(self, row):
        song = os.path.join(_cb_root(), row['song_id'])
        self.row = row
        self.rel = f"01_AudioAndAnnotations/{row['song_id']}/tracks/{row['path_audio']}"
        self.x, self.sr = read_audio(os.path.join(song, 'tracks', row['path_audio']), np.float64)
        self.e = env_db(self.x, self.sr)
        f = np.loadtxt(os.path.join(song, 'annotations', row['path_f0']), delimiter=';', skiprows=1, usecols=(0, 1))
        self.f0t, self.f0 = f[:, 0], f[:, 1]
        nn = list(csv.DictReader(open(os.path.join(song, 'annotations', row['path_notes'])), delimiter=';'))
        self.notes = [(float(n['start_sec']), float(n['end_sec']), int(n['pitch']), int(n['pitch_dev_cents'] or 0)) for n in nn]
        self.tuning = float(np.median([d for *_, d in self.notes]))  # cents: the players' A re 440
        self.Lmed = float(np.median([np.median(self.e[int(a / HOP):int(b / HOP)]) for a, b, _, _ in self.notes if b - a > 0.1]))

    def dip(self, a_end, b_start):
        i = lambda t: int(t / HOP)  # noqa: E731
        mid = 0.5 * (a_end + b_start)
        la = np.median(self.e[max(i(a_end) - 15, 0):max(i(a_end) - 3, 1)])
        lb = np.median(self.e[i(b_start) + 3:i(b_start) + 15])
        w = self.e[max(i(mid) - 8, 0):i(mid) + 8]
        return float(0.5 * (la + lb) - w.min()) if len(w) else 99.0

    def slurred(self, j):
        """is the change from note j to j+1 slurred (connected, the level dipping under 6 dB, the pitch moving)?"""
        a, b = self.notes[j], self.notes[j + 1]
        return b[0] - a[1] < CONNECT and b[2] != a[2] and self.dip(a[1], b[0]) < 6

    def vibrato_c(self, a, b):
        """the f0's 4-8 Hz modulation depth (cents RMS) over a note's middle"""
        if b - a < 0.6:
            return 0.0
        m = (self.f0t > a + 0.15) & (self.f0t < b - 0.1) & (self.f0 > 0)
        if m.sum() < 40:
            return 0.0
        c = 1200 * np.log2(self.f0[m])
        t = self.f0t[m]
        c = c - np.polyval(np.polyfit(t, c, 2), t)  # the slow drift out
        dt = np.median(np.diff(t))
        F = np.fft.rfft(c * np.hanning(len(c)))
        fr = np.fft.rfftfreq(len(c), dt)
        band = (fr >= 4) & (fr <= 8)
        p = np.abs(F) ** 2
        return float(np.sqrt(2 * p[band].sum() / (np.hanning(len(c)) ** 2).sum()) / math.sqrt(len(c))) if band.any() else 0.0

    def phrases(self):
        """[(first note, last note)] split at breaths"""
        out, start = [], 0
        for j in range(len(self.notes) - 1):
            if self.notes[j + 1][0] - self.notes[j][1] >= BREATH:
                out.append((start, j))
                start = j + 1
        out.append((start, len(self.notes) - 1))
        return out

    def windows(self, lo=10.0, hi=20.0):
        """runs of whole phrases lasting lo..hi s: (first note, last note)"""
        ph = self.phrases()
        out = []
        for i in range(len(ph)):
            for k in range(i, len(ph)):
                a, b = ph[i][0], ph[k][1]
                d = self.notes[b][1] - self.notes[a][0]
                if d > hi:
                    break
                if d >= lo:
                    out.append((a, b))
        return out


def score(tr, w, kind):
    a, b = w
    ch = range(a, b)
    dur = tr.notes[b][1] - tr.notes[a][0]
    if kind == 'legato':
        return sum(tr.slurred(j) for j in ch) / max(len(ch), 1)
    if kind == 'tongued':
        return sum(1 for j in ch if not tr.slurred(j)) / dur  # re-tongued changes a second
    if kind == 'swell':
        # the widest level change within one held note (a long note's crescendo or diminuendo), in dB
        best = 0.0
        for on, off, _, _ in tr.notes[a:b + 1]:
            if off - on < 1.0:
                continue
            seg = tr.e[int((on + 0.08) / HOP):int((off - 0.05) / HOP)]
            sm = np.convolve(seg, np.ones(10) / 10, 'valid')
            if len(sm) > 5:
                best = max(best, float(sm.max() - sm.min()))
        return best
    if kind == 'lyrical':
        v = [tr.vibrato_c(n[0], n[1]) for n in tr.notes[a:b + 1]]
        return float(np.median(sorted(v)[len(v) // 2:])) if v else 0.0
    raise ValueError(kind)


_TRACKS = {}


def choose(family, kind, n_tracks=8):
    """the best window of the cleanest tracks: (Track, window, score)"""
    inst = FAMILIES[family][0]
    best = None
    for row in tracks(inst)[:n_tracks]:
        key = (row['song_id'], row['path_audio'])
        if key not in _TRACKS:
            _TRACKS[key] = Track(row)
        tr = _TRACKS[key]
        for w in tr.windows():
            s = score(tr, w, kind)
            if best is None or s > best[2]:
                best = (tr, w, s)
    return best


def _u32(v):
    return max(0, min(0xFFFFFFFF, int(round(v * 0xFFFFFFFF))))


def events(tr, w):
    """the UMP events of a window: [(t, w0, w1)], the score [(onset, offset, midi, cents)]"""
    a, b = w
    t0 = tr.notes[a][0] - LEAD
    ev, sco = [], []
    for j in range(a, b + 1):
        on, off, midi, dev = tr.notes[j]
        slur_in = j > a and tr.slurred(j - 1)
        slur_out = j < b and tr.slurred(j)
        nxt = tr.notes[j + 1] if j < b else None
        end = (nxt[0] + 0.03) if slur_out else (min(off, nxt[0] - 0.02) if nxt is not None and nxt[0] - off < CONNECT else off)
        seg = tr.e[int(on / HOP):max(int(off / HOP), int(on / HOP) + 2)]
        Pc = np.clip(0.4 * 10 ** ((seg - tr.Lmed) / 40), 0.05, 1.0)
        P0 = float(np.median(Pc[:max(3, len(Pc) // 4)]))
        if slur_in:
            v = 0.1
        else:
            pk = seg.max()
            rise = max((int(np.argmax(seg >= pk - 3)) + 1) * HOP, 0.004)
            v = float(np.clip(K * math.pi * P0 / (2 * rise), 0.05, 1.0))
        key = midi
        base = ((4 << 28) | (CH << 16) | (key << 8))
        # the pitch: f0 less the tuning offset, the note's own cents where there is no f0
        m = (tr.f0t >= on) & (tr.f0t <= end) & (tr.f0 > 0)
        if m.any():
            pt, pv = tr.f0t[m], 69 + 12 * np.log2(tr.f0[m] / 440.0) - tr.tuning / 100.0
            pv = np.where(np.abs(pv - midi) > 1.5, midi + (dev - tr.tuning) / 100.0, pv)  # an octave slip of the tracker: the note
        else:
            pt, pv = np.array([on]), np.array([midi + (dev - tr.tuning) / 100.0])
        p_start = float(pv[0]) if len(pv) else midi
        tt = lambda t: round(max(t - t0, 0.0), 4)  # noqa: E731
        ev.append((tt(on), base | (0x0 << 20) | 3, int(round(p_start * 2 ** 25))))  # Registered Per-Note Controller 3: pitch 7.25
        ev.append((tt(on), base | (0x9 << 20), int(round(v * 65535)) << 16))  # Note On, the strike as velocity
        ev.append((tt(on), base | (0xA << 20), _u32(P0)))
        last_p, last_c = P0, p_start
        for k in range(len(Pc)):
            t = on + k * HOP
            if t >= end:
                break
            if k % 2 == 0 and abs(Pc[k] - last_p) > 0.004:
                ev.append((tt(t), base | (0xA << 20), _u32(Pc[k])))
                last_p = Pc[k]
            c = float(np.interp(t, pt, pv)) if len(pt) > 1 else p_start
            if abs(c - last_c) * 100 > 0.5:
                ev.append((tt(t), base | 3, int(round(c * 2 ** 25))))
                last_c = c
        ev.append((tt(end), base | (0x8 << 20), int(round(0.3 * 65535)) << 16))  # Note Off, a gentle release
        sco.append((round(on - t0, 4), round(end - t0, 4), midi, round((dev - tr.tuning), 1), 'slur' if slur_in else 'tongue'))
    ev.sort(key=lambda e: e[0])
    return ev, sco


def write(path, family, kind, tr, w, s):
    ev, sco = events(tr, w)
    inst, eng, pre = FAMILIES[family]
    a, b = w
    t0 = tr.notes[a][0] - LEAD
    meta = {'format': 'ump frames at 48000 Hz', 'title': f'{family} {kind}', 'family': family, 'kind': kind, 'instrument': inst, 'engine': eng, 'preset': pre,
            'source': 'choralebricks', 'licence': 'CC-BY-4.0', 'citation': sources.SOURCES['choralebricks']['citation'],
            'reference': tr.rel, 'reference_start_s': round(t0, 3), 'reference_end_s': round(tr.notes[b][1] + 1.0, 3),
            'tuning_offset_c': round(tr.tuning, 1), 'notes': len(sco), 'seconds': round(sco[-1][1] + 0.5, 2), 'score': round(s, 3),
            'range': f'{labels.name(min(n[2] for n in sco))}-{labels.name(max(n[2] for n in sco))}',
            'control': 'inferred from the recording: pressure from the level, strikes from the attacks, slurs from the dips'}
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('# ump frames at 48000 Hz\n')
        for k, v in meta.items():
            if k != 'format':
                f.write(f'# {k}: {v}\n')
        for on, off, midi, cents, art in sco:
            f.write(f'# note {on:.4f} {off:.4f} {midi} {cents:+.1f} {art}\n')
        for t, w0, w1 in ev:
            f.write(f'{round(t * UMP_RATE)} {w0:08x} {w1 & 0xFFFFFFFF:08x}\n')
    return meta


def write_set(family, out_dir, log=print):
    os.makedirs(out_dir, exist_ok=True)
    out = []
    for kind in KINDS:
        best = choose(family, kind)
        if best is None:
            log(family, kind, 'no window')
            continue
        tr, w, s = best
        if kind == 'lyrical' and s < 5.0:
            log(f'{family}: no vibrato in the source (deepest {s:.1f} c): no lyrical line')
            continue
        p = os.path.join(out_dir, f'{family}_{kind}.ump')
        meta = write(p, family, kind, tr, w, s)
        out.append((p, meta))
        log(family, kind, meta['reference'], meta['notes'], 'notes', meta['seconds'], 's', meta['range'], f"score {s:.2f}")
    return out


def read_perf(path):
    """(meta {key: value}, score [(onset, offset, midi, cents, art)], events [(t, w0, w1)])"""
    meta, sco, ev = {}, [], []
    for ln in open(path, encoding='utf-8'):
        ln = ln.strip()
        if not ln:
            continue
        if ln.startswith('# note '):
            f = ln[7:].split()
            sco.append((float(f[0]), float(f[1]), int(f[2]), float(f[3]), f[4] if len(f) > 4 else ''))
        elif ln.startswith('# ') and ': ' in ln:
            k, v = ln[2:].split(': ', 1)
            meta[k] = v
        elif not ln.startswith('#'):
            t, w0, w1 = ln.split()
            ev.append((int(t) / UMP_RATE, int(w0, 16), int(w1, 16)))
    return meta, sco, ev


def index(perf_dir):
    rows = []
    for fam in sorted(os.listdir(perf_dir)):
        d = os.path.join(perf_dir, fam)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.endswith('.ump'):
                meta, sco, ev = read_perf(os.path.join(d, fn))
                rows.append(dict(file=f'{fam}/{fn}', **{k: meta[k] for k in ('family', 'kind', 'instrument', 'engine', 'preset', 'range', 'seconds', 'notes',
                                                                              'reference', 'reference_start_s', 'reference_end_s', 'tuning_offset_c', 'licence')},
                                 packets=len(ev)))
    with open(os.path.join(perf_dir, 'index.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(rows, f, indent=1)
        f.write('\n')
    return rows
