# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The listening gate: is a candidate build measurably closer to the recordings than its baseline, before
anyone listens? Nothing goes to a listener unless it passes.

  python -m awvafit gate --inst I --candidate X --baseline Y [--out gate.json] [--workers N]

X and Y say how to render a key stream:
  <engine>:<preset>       awva_render --rec (AWVA_RENDER), the SDK's engines
  cmd:<command>           any other player; the command gets {csv} (the key stream), {out} (where to
                          write the dry output: mono float32, 48 kHz, time 0 = the stream's first row),
                          {octave} and {transpose}; a label may lead it: cmd[label]:<command>

The phrases: the instrument's held-out set (gate_set.json: a third of its performances in ChoraleBricks
and URMP, whole, the lowest-pitched among them; no fit may use any phrase of them: held_out_tracks()),
up to 16 legato lines from them, a quarter low-register, each played through a stream calibrated on that
build (awvafit.control: pressure, legato, strikes and ramps, per engine), so the comparison is the
engine's, not the inference's.

The measures, per phrase, against the recording (R), on R's notes and their render counterparts:
  dynamics   level: each note's sustain level re the phrase median, |render - R| (median over notes);
             range: the phrase's per-note level range
  harmonics  h1-h10 (dB re the total) in the sustain's middle third; their spread across notes; their
             change from the sustain's first third to its last
  noise      the aperiodic part (a one-period comb, x[n] - x[n - T], / sqrt 2, the period refined every
             20 ms): its level re the tone; its band shape (100-1k, 1-2.5k, 2.5-5k, 5-10k, 10-16k Hz, re
             its own total); its pulsing: the depth of its power above 1.5 kHz folded on the period
  onset      the rise (10 -> 90 %, ms); the attack's overshoot over the sustain (dB); the noise before
             the tone speaks (1 - the periodicity of the first 40 ms)
  slurs      at each connected change: the dip (dB) and its recovery (ms, from the dip's bottom to
             within 3 dB of the next note's level)
  texture    band-envelope modulation: in carriers 0.5-1, 1-2, 2-4, 4-8 kHz, the envelope's modulation
             power at 4-20, 20-80, 80-300 Hz re its mean (dB), averaged over the carriers
Every measure has a floor: R's own spread (the same measure between R's notes of the same pitch, or
between the phrase's two halves), so a difference under it is not a difference. A measure's score is
its distance from R in floors (the median over the phrases); a group's is its measures' mean; the total
is the groups' mean.

The range check (awvafit.range_check): every note of the instrument's range, soft, mid, loud and short, must speak,
sit on its partial within 15 cents, not flip and start within 150 ms; a candidate with a failing note
fails, whatever its phrases say.

The verdict:
  identical  candidate and baseline within a floor of each other on every measure: nothing to hear
  pass       the candidate's total is lower, it is better on most groups, and no group is worse by
             more than one floor
  fail       otherwise
Recordings are high-passed under min(60 Hz, 0.8 x the phrase's lowest note) before measuring (URMP's
rumble); the renders the same way.
"""
import argparse
import hashlib
import json
import math
import os
import subprocess
import threading
import types
from concurrent.futures import ThreadPoolExecutor

import numpy as np
from scipy.signal import butter, hilbert, resample_poly, sosfilt, sosfiltfilt

from . import control, env, labels, range_check, sources, strike

SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
GATE_SET = os.path.join(HERE, 'gate_set.json')
GROUPS = {
    'dynamics': ('level', 'range'),
    'harmonics': ('harm', 'harm_spread', 'harm_change'),
    'noise': ('noise_level', 'noise_shape', 'noise_pulse'),
    'onset': ('rise', 'overshoot', 'onset_noise'),
    'slurs': ('dip', 'recovery'),
    'texture': ('mod_slow', 'mod_mid', 'mod_fast'),
}
MIN_FLOOR = dict(level=0.5, range=0.5, harm=0.5, harm_spread=0.3, harm_change=0.5, noise_level=1.0, noise_shape=1.0,
                 noise_pulse=0.05, rise=5.0, overshoot=0.5, onset_noise=0.02, dip=1.0, recovery=5.0,
                 mod_slow=0.5, mod_mid=0.5, mod_fast=0.5)
NOISE_EDGES = ((100, 1000), (1000, 2500), (2500, 5000), (5000, 10000), (10000, 16000))
CARRIERS = ((500, 1000), (1000, 2000), (2000, 4000), (4000, 8000))
MODS = (('mod_slow', 4, 20), ('mod_mid', 20, 80), ('mod_fast', 80, 300))
# the instruments the gate knows: (ChoraleBricks instrument, URMP instrument)
INSTRUMENTS = {
    'trumpet': ('Trumpet', 'Trumpet'), 'flugelhorn': ('Flugelhorn', None), 'trombone': ('Trombone', 'Trombone'),
    'horn': ('French horn', 'French horn'), 'tuba': ('Tuba', 'Tuba'), 'sax': ('Alto Saxophone', 'Saxophone'),
    'clarinet': ('Clarinet', 'Clarinet'), 'oboe': ('Oboe', 'Oboe'), 'flute': ('Flute', 'Flute'),
    'bassoon': (None, 'Bassoon'), 'violin': (None, 'Violin'), 'viola': (None, 'Viola'), 'cello': (None, 'Cello'),
    'bass': (None, 'Double bass'),
}


# --- the held-out set ------------------------------------------------------------------------------

def _fingerprint(it, seconds=20.0):
    """a track's level envelope (50 ms, dB) over its first `seconds` of sound"""
    from scipy.signal import resample_poly

    from .audio import read
    x, sr = read(sources.path(it.source, it.ref_path), np.float64)
    if x.ndim > 1:
        x = x.mean(1)
    h = int(0.05 * sr)
    n = len(x) // h
    e = 10 * np.log10(np.mean(x[:n * h].reshape(n, h) ** 2, 1) + 1e-12)
    k = int(np.argmax(e > e.max() - 40))
    return e[k:k + int(seconds / 0.05)]


def performances(items):
    """{ref_path: performance key}: tracks whose first 20 s of level envelope correlate above 0.98 are one
    performance (URMP files one recording under several pieces)"""
    paths = sorted({it.ref_path for it in items})
    first = {}
    for it in items:
        first.setdefault(it.ref_path, it)
    fp = {p: _fingerprint(first[p]) for p in paths}
    key = {}
    for p in paths:
        for q in sorted(set(key.values())):
            a, b = fp[p], fp[q]
            n = min(len(a), len(b))
            if n > 100 and np.corrcoef(a[:n], b[:n])[0, 1] > 0.98:
                key[p] = q
                break
        else:
            key[p] = p
    return key


HELD_FRACTION = 1 / 3  # of each set's performances of an instrument, held out for the gate


def choose_set(inst, n_cb=8, n_urmp=8, exclude=(), per_track=2):
    """the held-out phrases of an instrument: a third of each set's performances (URMP's duplicate files
    grouped by fingerprint) are held out whole, the lowest-pitched among them; from those, legato lines of
    6-30 notes, 4-14 s, spread over the performances, a quarter of each set's share from its low register
    (the lines with the lowest median note), the rest in the order of the SHA-1 of their ids (fixed, not
    chosen by hand). -> (phrases, the held-out tracks: every file of every performance
    a phrase comes from; no fit may use any phrase of them)"""
    from .phraseset import load
    out, held = [], set()
    for set_name, name, k in (('choralebricks', INSTRUMENTS[inst][0], n_cb), ('urmp', INSTRUMENTS[inst][1], n_urmp)):
        if not name or not k:
            continue
        try:
            items = load(set_name, kind='legato_line')
        except Exception:  # the set isn't on disk
            continue
        cand = []
        for it in items:
            if it.instrument != name or it.id in exclude:
                continue
            dur = it.notes[-1]['offset_s'] - it.notes[0]['onset_s'] if it.notes[-1].get('offset_s') else 0
            if 6 <= len(it.notes) <= 30 and 4 <= dur <= 14:
                cand.append(it)
        allof = [it for it in items if it.instrument == name]
        perf = performances(allof) if set_name == 'urmp' else {it.ref_path: it.ref_path for it in allof}
        low = sorted(cand, key=lambda i: (float(np.median([n['midi'] for n in i.notes])), i.id))
        sha = sorted(cand, key=lambda i: hashlib.sha1(i.id.encode()).hexdigest())
        # the held-out performances first: a third of the set's (at least one), the lowest-pitched
        # performance among them, the rest in SHA-1 order; then the phrases from those only
        perfs = sorted(set(perf.values()))
        n_perf = max(1, round(len(perfs) * HELD_FRACTION))
        with_cand = {perf[it.ref_path] for it in cand}
        by_low = [perf[it.ref_path] for it in low]
        chosen = [by_low[0]] if by_low else []
        for q in sorted(with_cand, key=lambda q: hashlib.sha1(q.encode()).hexdigest()):
            if len(chosen) >= n_perf:
                break
            if q not in chosen:
                chosen.append(q)
        cap = max(per_track, math.ceil(k / max(len(chosen), 1)))
        n_low = max(1, round(k / 4))
        tracks, mine = {}, []
        for pool, quota in ((low, n_low), (sha, k)):
            for it in pool:
                if len(mine) >= quota:
                    break
                tr = perf[it.ref_path]
                if tr not in chosen or tracks.get(tr, 0) >= cap or any(m['id'] == it.id for m in mine):
                    continue
                tracks[tr] = tracks.get(tr, 0) + 1
                mine.append(dict(set=set_name, id=it.id, low=pool is low))
        out += mine
        keys_ = set(tracks)
        held |= {(set_name, p) for p, k in perf.items() if k in keys_}
    return out, [dict(set=s_, track=p) for s_, p in sorted(held)]


def held_out(inst):
    with open(GATE_SET, encoding='utf-8') as f:
        return json.load(f)['instruments'][inst]['phrases']


def held_out_tracks(inst=None):
    """the recordings no fit may use (any phrase of them): [(set, track)], of one instrument or all"""
    with open(GATE_SET, encoding='utf-8') as f:
        ins = json.load(f)['instruments']
    return sorted({(t['set'], t['track']) for k, v in ins.items() if inst in (None, k) for t in v['tracks']})


def items_for(inst):
    from .phraseset import load
    want = held_out(inst)
    out = []
    for s in sorted({w['set'] for w in want}):
        try:
            by = {i.id: i for i in load(s)}
        except Exception:
            continue
        out += [by[w['id']] for w in want if w['set'] == s and w['id'] in by]
    return out


# --- renderers -------------------------------------------------------------------------------------

def renderer(spec, workdir):
    """(label, factory(item) -> render(csv) -> mono float64)"""
    if spec.startswith('cmd'):
        label, cmd = spec[3:].split(':', 1) if not spec.startswith('cmd[') else spec[4:].split(']:', 1)
        label = label or 'cmd'
        if '{csv}' not in cmd or '{out}' not in cmd or ('{octave}' not in cmd and '{transpose}' not in cmd):
            raise env.EnvError(f'{label}: the command must take {{csv}}, {{out}}, and {{octave}} or {{transpose}} '
                               '(the stream is pressed at the note minus the transpose: without it the build plays in the wrong octave)')

        def factory(item):
            def render(csv):
                out = csv[:-4] + '.dry.f32'
                o = int(round(item.transpose / 12))
                r = subprocess.run(cmd.format(csv=csv, out=out, octave=o, transpose=item.transpose), shell=True, capture_output=True, text=True)
                if r.returncode or not os.path.exists(out):
                    raise RuntimeError(f'{label}: {r.stderr.strip() or r.stdout.strip()}')
                return np.fromfile(out, np.float32).astype(np.float64)
            return render
        return label, factory
    from .render import Renderer
    eng, pre = spec.split(':', 1)
    rd = Renderer()

    def factory(item):
        return strike.render_with(rd, eng, int(pre) if pre.isdigit() else pre, item.transpose, workdir)
    return spec, factory


# --- the measures ----------------------------------------------------------------------------------

def _hp_for(notes):
    f0 = 440 * 2 ** ((min(m for _, _, m in notes) - 69) / 12)
    return butter(2, min(60.0, 0.8 * f0), 'hp', fs=SR, output='sos')


def _f0(seg, f0n):
    n = 1 << 16
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    f = np.fft.rfftfreq(n, 1 / SR)
    m = (f > f0n * 0.94) & (f < f0n * 1.06)
    if not m.any():
        return f0n
    i = np.where(m)[0][np.argmax(S[m])]
    if 0 < i < len(S) - 1:
        a, b, c = np.log(S[i - 1:i + 2] + 1e-30)
        d = a - 2 * b + c
        return (i + (0.5 * (a - c) / d if d != 0 else 0)) * SR / n
    return f[i]


def comb_split(x, f0, chunk_s=0.02):
    """(residual, tone, phase): x minus itself one period back (the period refined per 20 ms), / sqrt 2"""
    T0 = SR / f0
    L = int(chunk_s * SR)
    res, tone, ph = [], [], []
    idx = np.arange(len(x))
    for a in range(int(T0 * 1.1) + 2, len(x) - L, L):
        c = x[a:a + L]
        n = np.arange(a, a + L)
        best, Tb = -np.inf, T0
        for t in np.linspace(T0 * 0.985, T0 * 1.015, 25):
            d = np.interp(n - t, idx, x)
            v = -np.sum((c - d) ** 2)
            if v > best:
                best, Tb = v, t
        d = np.interp(n - Tb, idx, x)
        res.append((c - d) / math.sqrt(2))
        tone.append(c)
        ph.append((n / Tb) % 1.0)
    if not res:
        return np.zeros(0), np.zeros(0), np.zeros(0)
    return np.concatenate(res), np.concatenate(tone), np.concatenate(ph)


HP15 = butter(4, 1500, 'hp', fs=SR, output='sos')
NH = 10


def harmonics(seg, f0):
    n = 1 << 14
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n)) ** 2
    f = np.fft.rfftfreq(n, 1 / SR)
    tot = S[(f > 50) & (f < 16000)].sum() + 1e-30
    return np.array([10 * math.log10(S[(f > (j - 0.3) * f0) & (f < (j + 0.3) * f0)].sum() / tot + 1e-12) for j in range(1, NH + 1)])


def _band_power(x, edges):
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)), 8192)) ** 2
    f = np.fft.rfftfreq(8192, 1 / SR)
    return np.array([S[(f >= a) & (f < b)].sum() for a, b in edges])


_CAR = [butter(4, (a, b), 'bandpass', fs=SR, output='sos') for a, b in CARRIERS]


def modulation(seg):
    out = {k: [] for k, _, _ in MODS}
    for sos in _CAR:
        e = np.abs(hilbert(sosfilt(sos, seg)))
        e = resample_poly(e, 1, 48)  # 1 kHz
        if len(e) < 200:
            return {k: math.nan for k in out}
        e = e[50:]
        m = e.mean() + 1e-20
        S = np.abs(np.fft.rfft((e - m) / m * np.hanning(len(e)))) ** 2 / len(e)
        f = np.fft.rfftfreq(len(e), 1e-3)
        for k, lo, hi in MODS:
            out[k].append(10 * math.log10(S[(f >= lo) & (f < hi)].sum() + 1e-12))
    return {k: float(np.mean(v)) for k, v in out.items()}


def note_features(x, on, off, midi):
    s0, s1 = control._sus(on, off)
    seg = x[int(s0 * SR):int(s1 * SR)]
    out = dict(midi=midi)
    if len(seg) < 0.08 * SR:
        return out
    f0 = _f0(seg, 440 * 2 ** ((midi - 69) / 12))
    out['level'] = float(np.median(control.env_db(seg)))
    L = len(seg)
    thirds = [seg[int(a * L):int((a + 1 / 3) * L)] for a in (0, 1 / 3, 2 / 3)]
    hs = [harmonics(t, f0) for t in thirds]
    out['harm'] = hs[1]
    out['harm_change'] = hs[2] - hs[0]
    r, p, ph = comb_split(seg, f0)
    if len(r):
        pw = np.mean(p ** 2) + 1e-30
        out['noise_level'] = 10 * math.log10(np.mean(r ** 2) / pw + 1e-12)
        nb = _band_power(r, NOISE_EDGES)
        out['noise_shape'] = 10 * np.log10(nb / (nb.sum() + 1e-30) + 1e-12)
        rh = sosfiltfilt(HP15, r)
        b = np.minimum((ph * 16).astype(int), 15)
        fold = np.array([np.mean(rh[b == i] ** 2) if np.any(b == i) else 0.0 for i in range(16)])
        out['noise_pulse'] = float((fold.max() - fold.min()) / (fold.mean() + 1e-30))
    out.update(modulation(seg))
    rt = strike.rise_time(x, on, off)
    out['rise'] = rt * 1000 if rt is not None else None
    ov = strike.overshoot(x, on, off)
    out['overshoot'] = ov[0] if ov else None
    head = x[int(on * SR):int((on + 0.04) * SR)]
    out['onset_noise'] = 1 - control.periodicity(head, f0, frame=0.02) if len(head) > 0.03 * SR else None
    return out


def slurs(x, notes):
    e = control.env_db(x)
    out = []
    for j in range(len(notes) - 1):
        (a0, b0, m0), (a1, b1, m1) = notes[j], notes[j + 1]
        if a1 - b0 >= control.CONNECT or round(m1) == round(m0):
            out.append(None)
            continue
        i0, i1 = max(int((b0 - 0.08) / 0.01), 0), int((a1 + 0.08) / 0.01) + 1
        s0, s1 = control._sus(a0, b0)
        t0, t1 = control._sus(a1, b1)
        la = np.median(e[int(s0 / 0.01):max(int(s1 / 0.01), int(s0 / 0.01) + 1)])
        lb = np.median(e[int(t0 / 0.01):max(int(t1 / 0.01), int(t0 / 0.01) + 1)])
        w = e[i0:i1]
        if not len(w) or not np.isfinite(la + lb):
            out.append(None)
            continue
        k = int(np.argmin(w))
        dip = float((la + lb) / 2 - w[k])
        after = e[i0 + k:i0 + k + 50]
        up = np.where(after >= lb - 3)[0]
        out.append(dict(dip=dip, recovery=float(up[0] * 10) if len(up) else 500.0))
    return out


def features(x, notes):
    """{per-note: [dict], slurs: [dict|None], phrase: dict}"""
    x = sosfilt(_hp_for(notes), x)
    per = [note_features(x, a, b, m) for a, b, m in notes]
    lv = np.array([p.get('level', np.nan) for p in per])
    med = np.nanmedian(lv)
    for p, l in zip(per, lv):
        p['level'] = float(l - med) if np.isfinite(l) else None
    H = np.array([p['harm'] for p in per if 'harm' in p])
    phrase = dict(range=float(np.nanmax(lv) - np.nanmin(lv)) if np.isfinite(lv).sum() > 1 else None,
                  harm_spread=H.std(0) if len(H) > 1 else None)
    return dict(notes=per, slurs=slurs(x, notes), phrase=phrase)


def _d(a, b):
    """the distance between two values of a measure (scalars or vectors: the mean |difference|)"""
    if a is None or b is None:
        return None
    a, b = np.asarray(a, float), np.asarray(b, float)
    v = np.abs(a - b)
    return float(np.nanmean(v)) if np.isfinite(v).any() else None


PER_NOTE = ('level', 'harm', 'harm_change', 'noise_level', 'noise_shape', 'noise_pulse', 'rise', 'overshoot', 'onset_noise', 'mod_slow', 'mod_mid', 'mod_fast')


def distance(F, G):
    """{measure: distance} between two feature sets on the same notes (R against a render, or two renders)"""
    out = {}
    for m in PER_NOTE:
        v = [_d(f.get(m), g.get(m)) for f, g in zip(F['notes'], G['notes'])]
        v = [d for d in v if d is not None]
        out[m] = float(np.median(v)) if v else None
    for m in ('range', 'harm_spread'):
        out[m] = _d(F['phrase'][m], G['phrase'][m])
    for m in ('dip', 'recovery'):
        v = [abs(f[m] - g[m]) for f, g in zip(F['slurs'], G['slurs']) if f and g]
        out[m] = float(np.median(v)) if v else None
    return out


def floors(x, notes):
    """R's own spread per measure: between notes of the same pitch (>= 3 pairs), else between halves"""
    F = features(x, notes)
    per = F['notes']
    out = {}
    pairs = [(i, k) for i in range(len(per)) for k in range(i + 1, len(per)) if round(per[i]['midi']) == round(per[k]['midi'])]
    h = len(notes) // 2
    for m in PER_NOTE:
        if len(pairs) >= 3:
            v = [_d(per[i].get(m), per[k].get(m)) for i, k in pairs]
            v = [d for d in v if d is not None]
            out[m] = float(np.median(v)) if v else None
        else:
            a = [p.get(m) for p in per[:h] if p.get(m) is not None]
            b = [p.get(m) for p in per[h:] if p.get(m) is not None]
            out[m] = _d(np.median(np.array(a, float), 0), np.median(np.array(b, float), 0)) if a and b else None
    if h >= 2:
        Fa, Fb = features(x, notes[:h]), None
        Fb = features(x, notes[h:])
        out['range'] = _d(Fa['phrase']['range'], Fb['phrase']['range'])
        out['harm_spread'] = _d(Fa['phrase']['harm_spread'], Fb['phrase']['harm_spread'])
    for m in ('dip', 'recovery'):
        v = [s[m] for s in F['slurs'] if s]
        out[m] = float(np.median(np.abs(np.diff(v)))) if len(v) >= 3 else None
    return {m: max(v if v is not None else 0.0, MIN_FLOOR[m]) for m, v in ((m, out.get(m)) for m in MIN_FLOOR)}


# --- the run ---------------------------------------------------------------------------------------

def _render_notes(item, stream):
    sn = strike.notes_of(stream)
    t0 = sn[0]['t_on']
    rn = control.rec_notes(item)
    return [(n['t_on'] - t0, n['t_off'] - t0, m) for n, (_, _, m) in zip(sn, rn)]


def run(inst, candidate, baseline, workdir, workers=4, log=print, reuse=False):
    from .phraseset import reference
    items = items_for(inst)
    if not items:
        raise env.EnvError(f'no held-out phrases on disk for {inst} (gate_set.json; AWVA_REFS)')
    builds = [renderer(candidate, workdir), renderer(baseline, workdir)]
    # one folder per candidate-baseline pair: another gate run (another lane, other builds) never shares it
    run_dir = os.path.join(workdir, inst, hashlib.sha1(f'{candidate} | {baseline}'.encode()).hexdigest()[:12])
    lock = threading.Lock()

    def one(bi, item):
        label, factory = builds[bi]
        d = os.path.join(run_dir, f'b{bi}', item.id)  # its own folder per run, build and phrase
        os.makedirs(d, exist_ok=True)
        render = factory(item)
        path = os.path.join(d, 'strike', 'calibrated.csv')
        lp = os.path.join(d, 'label.txt')
        cached = False
        if reuse and os.path.exists(path) and os.path.exists(lp):
            with open(lp, encoding='utf-8') as f:
                cached = f.read() == label
        if not cached:
            path, _ = control.calibrated(item, render, d, log=lambda m: None)
            with open(lp, 'w', encoding='utf-8') as f:
                f.write(label)
        x = render(path)
        with lock:
            log(f'  {inst} {label} {item.id}: rendered')
        return features(x, _render_notes(item, path))

    jobs = [(bi, it) for it in items for bi in (0, 1)]
    with ThreadPoolExecutor(max_workers=workers) as ex:
        feats = list(ex.map(lambda j: one(*j), jobs))
    per_item = []
    for k, it in enumerate(items):
        rec, _ = reference(it)
        rn = control.rec_notes(it)
        R = features(rec, rn)
        fl = floors(rec, rn)
        C, B = feats[2 * k], feats[2 * k + 1]
        per_item.append(dict(id=it.id, floor=fl, cand=distance(R, C), base=distance(R, B), between=distance(C, B)))
    ranges = None
    if inst in range_check.RANGES:
        def rng(bi):
            _, factory = builds[bi]
            return range_check.check(lambda o: factory(types.SimpleNamespace(transpose=12.0 * o)), inst,
                                     os.path.join(run_dir, f'b{bi}', 'range'))
        with ThreadPoolExecutor(max_workers=2) as ex:
            ranges = list(ex.map(rng, (0, 1)))
        log(f"  {inst} range: candidate {len(ranges[0]['failures'])} failing notes, baseline {len(ranges[1]['failures'])}")
    return verdict(inst, candidate, baseline, per_item, ranges)


def _med(v):
    v = [x for x in v if x is not None and np.isfinite(x)]
    return float(np.median(v)) if v else None


def verdict(inst, candidate, baseline, per_item, ranges=None):
    measures = [m for g in GROUPS.values() for m in g]
    z = {}
    for m in measures:
        fl = [p['floor'][m] for p in per_item]
        zc = _med([p['cand'][m] / f if p['cand'][m] is not None else None for p, f in zip(per_item, fl)])
        zb = _med([p['base'][m] / f if p['base'][m] is not None else None for p, f in zip(per_item, fl)])
        zx = _med([p['between'][m] / f if p['between'][m] is not None else None for p, f in zip(per_item, fl)])
        z[m] = dict(cand=zc, base=zb, between=zx, floor=_med(fl))
    groups = {}
    for g, ms in GROUPS.items():
        c = [z[m]['cand'] for m in ms if z[m]['cand'] is not None and z[m]['base'] is not None]
        b = [z[m]['base'] for m in ms if z[m]['cand'] is not None and z[m]['base'] is not None]
        groups[g] = dict(cand=float(np.mean(c)) if c else None, base=float(np.mean(b)) if b else None)
    ok = [g for g, v in groups.items() if v['cand'] is not None]
    tc = float(np.mean([groups[g]['cand'] for g in ok])) if ok else math.nan
    tb = float(np.mean([groups[g]['base'] for g in ok])) if ok else math.nan
    better = [g for g in ok if groups[g]['cand'] < groups[g]['base']]
    worse = [g for g in ok if groups[g]['cand'] - groups[g]['base'] > 1.0]
    identical = all(v['between'] is None or v['between'] < 1.0 for v in z.values())
    range_fail = bool(ranges and ranges[0]['failures'])
    if range_fail:
        v = 'fail'  # a note that doesn't speak, sits on the wrong partial or flips fails whatever else
    elif identical:
        v = 'identical'
    elif tc < tb and len(better) > len(ok) / 2 and not worse:
        v = 'pass'
    else:
        v = 'fail'
    return dict(instrument=inst, candidate=candidate, baseline=baseline, verdict=v, total=dict(cand=tc, base=tb),
                better_groups=better, worse_groups=worse, groups=groups, measures=z,
                range=dict(candidate=ranges[0]['failures'], baseline=ranges[1]['failures'],
                           notes=dict(candidate=ranges[0]['notes'], baseline=ranges[1]['notes'])) if ranges else None,
                phrases=[p['id'] for p in per_item], per_phrase=per_item)


def table(res):
    f = lambda v: '-' if v is None or (isinstance(v, float) and not np.isfinite(v)) else f'{v:.2f}'  # noqa: E731
    lines = [f"## {res['instrument']}: {res['verdict'].upper()}  (candidate {res['candidate']} against baseline {res['baseline']})",
             f"{len(res['phrases'])} held-out phrases; scores are distances from the recording in floors (R's own spread), lower is closer", '',
             '| group | measure | candidate | baseline | between them | floor |', '|---|---|---|---|---|---|']
    for g, ms in GROUPS.items():
        gv = res['groups'][g]
        lines.append(f"| **{g}** | | **{f(gv['cand'])}** | **{f(gv['base'])}** | | |")
        for m in ms:
            v = res['measures'][m]
            lines.append(f"| | {m} | {f(v['cand'])} | {f(v['base'])} | {f(v['between'])} | {f(v['floor'])} |")
    lines.append(f"| **total** | | **{f(res['total']['cand'])}** | **{f(res['total']['base'])}** | | |")
    lines += ['', f"better on: {', '.join(res['better_groups']) or 'none'}; worse by more than a floor on: {', '.join(res['worse_groups']) or 'none'}"]
    if res.get('range'):
        nm = lambda m: labels.name(m)  # noqa: E731
        for who in ('candidate', 'baseline'):
            fl = res['range'][who]
            lines.append(f"range check, {who}: " + ('every note passes' if not fl else
                         f"{len(fl)} failing: " + '; '.join(f"{nm(f['midi'])} {f['dyn']}: {', '.join(f['why'])}" for f in fl)))
    return '\n'.join(lines)


def _json(o):
    if isinstance(o, np.ndarray):
        return o.tolist()
    if isinstance(o, (np.floating, np.integer)):
        return o.item()
    raise TypeError(type(o))


def main(argv=None):
    p = argparse.ArgumentParser(prog='awvafit gate', description=__doc__.split('\n\n')[0])
    p.add_argument('--inst', required=True, choices=sorted(INSTRUMENTS))
    p.add_argument('--candidate', required=True)
    p.add_argument('--baseline', required=True)
    p.add_argument('--out')
    p.add_argument('--workers', type=int, default=4)
    p.add_argument('--reuse', action='store_true', help='reuse the calibrated streams of an earlier run with the same build labels (only for the same builds: a rebuilt engine under the same label needs them calibrated again)')
    a = p.parse_args(argv)
    work = env.workdir('gate')
    res = run(a.inst, a.candidate, a.baseline, work, a.workers, reuse=a.reuse)
    print(table(res))
    if a.out:
        os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
        with open(a.out, 'w', encoding='utf-8') as f:
            json.dump(res, f, indent=1, default=_json)
    return 0 if res['verdict'] == 'pass' else 1
