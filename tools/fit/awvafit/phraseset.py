# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Phrase sets: legato lines and swells taken from recordings (ChoraleBricks now; URMP and Bach10 when
they are on disk), as key streams any engine plays, with the recording to compare the render with.
Isolated notes miss what phrases show (the step-0 lesson): the note changes.

The manifest (format "awvafit-phrases/1", also read as "awva-phrases/1" and in its first, unversioned
form) lives at $AWVA_REFS/phrases/<set>/manifest.json, or $AWVA_REFS/phrases/manifest.json. Per item:
  id, kind (legato_line | swell), family, instrument, engine, preset (awva_render's names), transpose
  (semitones: keys are pressed at midi - transpose), stream (a key stream CSV, relative to the
  manifest), audio0_s (the stream-clock time where a render's audio starts: the CSV's first row, before
  keys.Stream.write's +0.1), source and reference (the recording: reference.path relative to the
  source's root, start_s the recording's time of stream time 0, end_s), notes (onset_s/offset_s or
  t_s/dur_s on the stream clock, midi, cents, level_db), transitions (the builder's measurements of the
  recording), swell (range_db, hf_vs_level_db_per_db).

  items = load('choralebricks')                     every item, its licence checked (sources.allowed)
  res = render(item, out)                           awva_render --rec <stream> --transpose T --settle 1 --tail 1
  T = transitions(x, sr, item, clock)               the note changes measured on a render or the recording
  d = distance(T_ref, T_model)                      the phrase-level components (PHRASE_OBJECTIVE)

The transition measures (the same code on the recording and the render; the stream clock maps to each:
the render's audio starts at audio0_s, the recording's segment at reference.start_s):
  dip_db, dip_ms   the level dip at the change (10 ms RMS) re the mean of the two notes' levels around
                   it, and its width below -3 dB
  step_db          the level step from the first note to the second
  glide_ms         the pitch's 10 -> 90 % of the interval (slurs: the pitch changes)
  overshoot_c      how far the pitch passes the target (cents)
  shape            the glide as a fraction of the interval, every 10 ms from -60 to +120 ms around the
                   change (the trajectory, for the shape distance)
  arrival_s        when the second note's pitch arrives (within 50 cents, periodic), on the stream clock
Components of distance() (each a median over the item's transitions):
  glide_shape (RMS of the trajectories' difference, % of the interval), glide_ms (|difference|),
  onset_ms (|arrival difference|: the timing), dip_db, dip_ms, step_db (|differences|)
These are new distances: like any, they enter an objective only after the back-test (docs/fitting.md).
"""
import csv
import json
import os
from dataclasses import dataclass, field

import numpy as np

from . import env, sources
from .audio import read, resample
from .features.measures import pitch_track
from .objective import Objective
from .render import Renderer

FORMATS = ('awvafit-phrases/1', 'awva-phrases/1', None)
HOP = 0.01
SHAPE_T = np.arange(-0.06, 0.121, 0.01)
SETTLE, TAIL = 1.0, 1.0
PHRASE_OBJECTIVE = Objective([('glide_shape', 15.0, 1.0), ('glide_ms', 30.0, 1.0), ('onset_ms', 30.0, 1.0), ('dip_db', 3.0, 1.0),
                              ('dip_ms', 30.0, 0.5), ('step_db', 3.0, 0.5)])


@dataclass
class Item:
    id: str
    kind: str
    family: str
    instrument: str
    engine: str
    preset: str
    transpose: float
    stream: str
    audio0_s: float
    source: str
    ref_path: str
    ref_start: float
    ref_end: float
    notes: list
    transitions: list = field(default_factory=list)
    swell: dict = None
    note: str = ''  # a licence restriction, said wherever the item is used


def manifest_path(set_name='choralebricks'):
    root = os.path.join(env.refs_root(), 'phrases')
    p = os.path.join(root, set_name, 'manifest.json')
    return p if os.path.exists(p) else os.path.join(root, 'manifest.json')


def _first_row(csv_path):
    with open(csv_path) as f:
        next(f)
        return float(next(f).split(',')[0]) - 0.1


def _item(d, base, default_source):
    eng, pre = (d.get('engine'), d.get('preset')) if d.get('engine') else d['entry'].split(':', 1)
    transpose = d.get('transpose', 12 * d.get('oct', 0))
    stream = os.path.join(base, *d['stream'].split('/'))
    audio0 = d.get('audio0_s')
    if audio0 is None:
        audio0 = _first_row(stream)
    ref = d['reference']
    source = d.get('source') or ref.get('source') or default_source
    rel = ref.get('path')
    if rel is None:  # the first form: relative to the refs root, <source>/<root>/<path>
        parts = ref['audio'].split('/')
        source = source or parts[0]
        root = sources.SOURCES[source].get('root')
        rel = '/'.join(parts[2:] if root and parts[1] == root else parts[1:])
    notes = []
    for n in d['notes']:
        on = n.get('onset_s', n.get('t_s'))
        off = n.get('offset_s', on + n['dur_s'] if 'dur_s' in n else None)
        notes.append(dict(onset_s=on, offset_s=off, midi=n['midi'], cents=n.get('cents', 0), level_db=n.get('level_db')))
    trans = [dict(t, **{'from': t.get('from', t.get('from_'))}) for t in d.get('transitions', [])]
    return Item(d['id'], d['kind'], d['family'], d['instrument'], eng, pre, float(transpose), stream, float(audio0), source, rel,
                float(ref['start_s']), float(ref['end_s']), notes, trans, d.get('swell'))


def load(set_name='choralebricks', family=None, kind=None, path=None):
    """the items of a phrase set (filtered), each source's licence checked for reading"""
    p = path or manifest_path(set_name)
    with open(p, encoding='utf-8') as f:
        m = json.load(f)
    if m.get('format') not in FORMATS:
        raise ValueError(f"{p}: format {m.get('format')!r} is not one this reader knows ({FORMATS[:2]})")
    base = os.path.dirname(p)
    out = []
    for d in m['items']:
        if (family and d['family'] != family) or (kind and d['kind'] != kind):
            continue
        default = next((k for k in (m.get('source'), m.get('set')) if k in sources.SOURCES), None)  # (the first form's 'source' is a description)
        it = _item(d, base, default)
        ok, note = sources.allowed(it.source, 'read')
        if not ok:
            raise PermissionError(note)
        it.note = note
        out.append(it)
    return out


# --- rendering and the recording ----------------------------------------------------------------------


def render(item, out, engine=None, preset=None, sets=None, renderer=None):
    """the item's key stream through the key path, in its register: awva_render --rec --transpose"""
    r = renderer or Renderer()
    return r.render(engine or item.engine, preset if preset is not None else item.preset, rec=item.stream, transpose=item.transpose,
                    sets=sets, settle=SETTLE, tail=TAIL, out=out)


def reference(item, sr_out=48000):
    """(x, sr) of the recording from reference.start_s to end_s (zeros where start_s is before the
    file), resampled to the renders' rate so both are measured on one frame grid"""
    x, sr = read(sources.path(item.source, item.ref_path), np.float64)
    a, b = int(round(item.ref_start * sr)), int(round(item.ref_end * sr))
    seg = x[max(a, 0):max(b, 0)]
    if a < 0:
        seg = np.concatenate([np.zeros(-a), seg])
    return (resample(seg, sr, sr_out), sr_out) if sr_out and sr != sr_out else (seg, sr)


# --- the transition measures ------------------------------------------------------------------------


def env_db(x, sr):
    n = int(HOP * sr)
    m = len(x) // n
    return 10 * np.log10(np.mean(np.asarray(x[:m * n], np.float64).reshape(m, n) ** 2, 1) + 1e-14)


def _track(x, sr, a, b, t_lo, t_hi):
    """the pitch from t_lo to t_hi (s on this audio): cents re note a, the periodicity; from two
    trackers (near a, near b) taking the more periodic per frame"""
    s0, s1 = max(int(t_lo * sr), 0), min(int(t_hi * sr), len(x))
    if s1 - s0 < int(0.06 * sr):
        return np.zeros(0), np.zeros(0), np.zeros(0)
    seg = x[s0:s1]
    win = max(0.0427, 3.0 / (440 * 2 ** ((min(a, b) - 69) / 12)))  # three periods of the lower note at least
    ta, tb = pitch_track(seg, sr, a, win_s=win), pitch_track(seg, sr, b, win_s=win)
    n = min(len(ta), len(tb))
    ta, tb = ta[:n], tb[:n]
    use_b = tb[:, 2] > ta[:, 2]
    cents = np.where(use_b, tb[:, 1] + 100 * (b - a), ta[:, 1])
    per = np.maximum(ta[:, 2], tb[:, 2])
    return ta[:, 0] + s0 / sr, cents, per


def transitions(x, sr, item, clock):
    """the item's note changes (consecutive notes less than 0.12 s apart) measured on x, whose time 0
    is stream time `clock` (a render: item.audio0_s; the recording segment: 0)"""
    e = env_db(x, sr)
    i = lambda t: int((t - clock) / HOP)  # noqa: E731
    out = []
    for na, nb in zip(item.notes[:-1], item.notes[1:]):
        a_end, b_start = na['offset_s'], nb['onset_s']
        if a_end is None or b_start - a_end >= 0.12:
            continue
        mid = 0.5 * (a_end + b_start)
        la = e[max(i(a_end) - 15, 0):max(i(a_end) - 3, 1)]
        lb = e[i(b_start) + 3:i(b_start) + 15]
        w = e[max(i(mid) - 8, 0):i(mid) + 8]
        if not len(la) or not len(lb) or not len(w):
            continue
        la, lb = float(np.median(la)), float(np.median(lb))
        base = 0.5 * (la + lb)
        r = dict(at_s=b_start, slur=nb['midi'] != na['midi'], dip_db=float(base - w.min()), dip_ms=float(np.sum(w < base - 3) * HOP * 1000),
                 step_db=lb - la)
        tt, c, per = _track(x, sr, na['midi'], nb['midi'], mid - clock - 0.25, min(nb['offset_s'] or mid + 0.4, mid + 0.4) - clock)
        tt = tt + clock
        if len(tt):
            ok = (per > 0.5) & np.isfinite(c)
            arr = np.where(ok & (tt > b_start - 0.1) & (np.abs(c - 100 * (nb['midi'] - na['midi'])) < 50))[0]
            r['arrival_s'] = float(tt[arr[0]]) if len(arr) else np.nan
            if r['slur'] and ok.sum() > 5:
                span = 100.0 * (nb['midi'] - na['midi'])
                frac = np.interp(mid + SHAPE_T, tt[ok], c[ok] / span, left=np.nan, right=np.nan)
                r['shape'] = frac
                near = (tt > mid - 0.15) & (tt < mid + 0.15) & ok
                if near.sum() > 5:
                    f = c[near] / span
                    t10 = tt[near][np.argmax(f > 0.1)] if (f > 0.1).any() else np.nan
                    t90 = tt[near][np.argmax(f > 0.9)] if (f > 0.9).any() else np.nan
                    r['glide_ms'] = float(max(t90 - t10, 0.0) * 1000) if np.isfinite(t10) and np.isfinite(t90) else np.nan
                    r['overshoot_c'] = float(max(np.max(f) - 1.0, 0.0) * abs(span))
        out.append(r)
    return out


def distance(R, M):
    """the phrase-level components between the recording's (R) and the render's (M) transitions,
    matched in order (the same phrase: the same note changes); medians over the transitions"""
    def med(v):
        v = [x for x in v if np.isfinite(x)]
        return float(np.median(v)) if v else 0.0
    c = dict(glide_shape=[], glide_ms=[], onset_ms=[], dip_db=[], dip_ms=[], step_db=[])
    for r, m in zip(R, M):
        c['dip_db'].append(abs(m['dip_db'] - r['dip_db']))
        c['dip_ms'].append(abs(m['dip_ms'] - r['dip_ms']))
        c['step_db'].append(abs(m['step_db'] - r['step_db']))
        if 'arrival_s' in r and 'arrival_s' in m:
            c['onset_ms'].append(abs(m['arrival_s'] - r['arrival_s']) * 1000)
        if 'shape' in r and 'shape' in m:
            d = m['shape'] - r['shape']
            d = d[np.isfinite(d)]
            if len(d):
                c['glide_shape'].append(float(np.sqrt(np.mean(np.clip(d, -2, 2) ** 2)) * 100))
        if 'glide_ms' in r and 'glide_ms' in m:
            c['glide_ms'].append(abs(m['glide_ms'] - r['glide_ms']))
    out = {k: med(v) for k, v in c.items()}
    out['_n'] = len(R)
    return out


def score(item, out_dir, engine=None, preset=None, sets=None, renderer=None):
    """render the item, measure it and its recording: (components with total, R, M)"""
    os.makedirs(out_dir, exist_ok=True)
    res = render(item, os.path.join(out_dir, item.id + '.f32'), engine, preset, sets, renderer)
    x = res.x.astype(np.float64)
    y, sr = reference(item)
    R = transitions(y, sr, item, 0.0)
    M = transitions(x, 48000, item, item.audio0_s)
    d = distance(R, M)
    d['total'] = PHRASE_OBJECTIVE.total(d)
    return d, R, M


def summary_csv(rows, path):
    keys = ['id', 'family', 'instrument', 'engine', 'preset'] + PHRASE_OBJECTIVE.names + ['total', '_n']
    with open(path, 'w', newline='', encoding='utf-8') as f:
        w = csv.writer(f)
        w.writerow(keys)
        for r in rows:
            w.writerow([r.get(k, '') if not isinstance(r.get(k), float) else f'{r[k]:.4g}' for k in keys])
