# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The range check: every note of an instrument's range, at a soft and a loud dynamic, on one build.

Each note is an isolated press through the key path, held 1.2 s soft, mid and loud (strike / pressure
0.2 / 0.12, 0.45 / 0.4, 0.75 / 0.85) and short (0.3 s, mid), the keys an octave press away where the
layout needs it. A note passes when:
  speaks     its sustain is periodic (YIN aperiodicity <= 0.3 in most frames) and within 25 dB of the
             instrument's median note at that dynamic
  partial    its median pitch (YIN, searched from a twelfth under to a twelfth over the note, so a
             wrong partial or octave shows) within 15 cents of the note
  steady     no flip: frames more than 50 cents off the note's median pitch at most 5 % of the sustain
             and never 30 ms in a row
  starts     the pitch within 50 cents of the note by 150 ms after the press
The gate fails a candidate with any failing note (whatever its phrases say), and lists the notes.

  check(render_for, inst, workdir) -> dict(notes=[...], failures=[...])
     render_for(octave) -> render(csv) -> the dry output; octave: the octave presses the stream needs
"""
import math
import os

import numpy as np

from . import keys

SR = 48000
# a practical range (MIDI, sounding) per instrument
RANGES = {
    'trumpet': (54, 82), 'flugelhorn': (52, 77), 'trombone': (40, 72), 'horn': (41, 77), 'tuba': (29, 65),
    'sax': (49, 80), 'clarinet': (50, 89), 'oboe': (58, 89), 'flute': (60, 96), 'bassoon': (34, 75),
    'violin': (55, 100), 'viola': (48, 88), 'cello': (36, 76), 'bass': (28, 67),
}
DYNAMICS = (('soft', 0.2, 0.12), ('mid', 0.45, 0.4), ('loud', 0.75, 0.85))  # (name, strike, pressure)
SHORT = 0.3  # s: a short press at the mid dynamic too (it must speak and start in time)
HOLD = 1.2
GAP = 0.6
PARTIAL_C = 15.0
FLIP_C = 50.0


def yin(seg, f_lo, f_hi, sr=SR, frame=0.04, hop=0.01, thresh=0.15):
    """per frame: (f0 Hz or nan, aperiodicity 0..1) by YIN's cumulative mean normalised difference"""
    n = int(frame * sr)
    h = int(hop * sr)
    tmin, tmax = int(sr / f_hi), int(sr / f_lo) + 1
    out = []
    for a in range(0, len(seg) - n - tmax, h):
        x = seg[a:a + n + tmax]
        e = np.cumsum(x ** 2)
        x0 = x[:n]
        # d(tau) = sum (x[t] - x[t + tau])^2 over the frame
        c = np.correlate(x[:n + tmax], x0, mode='valid')[:tmax + 1]
        en0 = e[n - 1]
        ent = e[n - 1:n + tmax] - np.concatenate([[0.0], e[:tmax]])
        d = en0 + ent - 2 * c
        d[0] = 0
        cm = d[1:] * np.arange(1, tmax + 1) / (np.cumsum(d[1:]) + 1e-20)
        cm = np.concatenate([[1.0], cm])
        cand = np.where(cm[tmin:] < thresh)[0]
        if len(cand):
            t = tmin + cand[0]
            while t + 1 <= tmax and cm[t + 1] < cm[t]:
                t += 1
        else:
            t = tmin + int(np.argmin(cm[tmin:]))
        ap = float(cm[t])
        if 1 <= t < tmax:
            p, q, r = cm[t - 1], cm[t], cm[t + 1]
            den = p - 2 * q + r
            t = t + (0.5 * (p - r) / den if den > 0 else 0.0)
        out.append((sr / t if t > 0 else math.nan, ap))
    return np.array(out).reshape(-1, 2)


def _layout(note):
    """(layout note, octave presses) for a sounding note: the octave nearest the layout's middle"""
    for o in (0, 1, -1, 2, -2, 3, -3):
        if note - 12 * o in keys.KEYS_OF:
            return note - 12 * o, o
    return None, None


def plan(inst):
    lo, hi = RANGES[inst]
    by_oct = {}
    for m in range(lo, hi + 1):
        lay, o = _layout(m)
        if lay is None:
            continue
        by_oct.setdefault(o, []).append((m, lay))
    return by_oct


def _stream(path, notes):
    s = keys.Stream()
    t = 0.3
    sched = []
    for m, lay in notes:
        for dyn, v, P, hold in [(d, v, P, HOLD) for d, v, P in DYNAMICS] + [('short', DYNAMICS[1][1], DYNAMICS[1][2], SHORT)]:
            end = s.press(t, v, P, hold, note=lay)
            sched.append(dict(midi=m, dyn=dyn, t_on=t, t_off=end - 0.02))
            t = end + GAP
    s.write(path, notes=False)
    t0 = sched[0]['t_on']
    for d in sched:
        d['t_on'] -= t0
        d['t_off'] -= t0
    return sched


def _note(x, d):
    f = 440 * 2 ** ((d['midi'] - 69) / 12)
    a, b = int(d['t_on'] * SR), int(d['t_off'] * SR)
    seg = x[a:b]
    tr = yin(seg, f / 3.2, f * 3.2)
    if not len(tr):
        return dict(d, level=-200.0, ok_frames=0.0)
    t = np.arange(len(tr)) * 0.01
    sus = (t >= min(0.3, 0.4 * (b - a) / SR)) & (t <= (b - a) / SR - 0.1)
    cents = 1200 * np.log2(tr[:, 0] / f)
    good = sus & (tr[:, 1] <= 0.3) & np.isfinite(cents)
    s0 = d['t_on'] + min(0.3, 0.4 * (d['t_off'] - d['t_on']))
    lvl = 10 * math.log10(np.mean(x[int(s0 * SR):int((d['t_off'] - 0.1) * SR)] ** 2) + 1e-20)
    out = dict(d, level=lvl, ok_frames=float(good.sum() / max(sus.sum(), 1)))
    if good.sum() >= 5:
        med = float(np.median(cents[good]))
        off = np.abs(cents[sus] - med) > FLIP_C
        off |= ~np.isfinite(cents[sus])
        run, worst = 0, 0
        for v in off:
            run = run + 1 if v else 0
            worst = max(worst, run)
        st = np.where((np.abs(cents) <= FLIP_C) & (tr[:, 1] <= 0.3))[0]
        out.update(cents=round(med, 1), flip_frac=round(float(off.mean()), 3), flip_run_ms=worst * 10,
                   start_ms=int(st[0] * 10 + 20) if len(st) else None)
    return out


def check(render_for, inst, workdir):
    os.makedirs(workdir, exist_ok=True)
    rows = []
    for o, notes in sorted(plan(inst).items()):
        p = os.path.join(workdir, f'range_oct{o:+d}.csv')
        sched = _stream(p, notes)
        x = render_for(o)(p)
        rows += [_note(x, d) for d in sched]
    fails = []
    for dyn in [d for d, _, _ in DYNAMICS] + ['short']:
        lv = [r['level'] for r in rows if r['dyn'] == dyn]
        med = float(np.median(lv))
        for r in rows:
            if r['dyn'] != dyn:
                continue
            why = []
            if r['ok_frames'] < 0.5 or r['level'] < med - 25:
                why.append('does not speak')
            elif 'cents' not in r:
                why.append('no pitch')
            else:
                if abs(r['cents']) > PARTIAL_C:
                    why.append(f"off by {r['cents']:+.0f} c" + (' (wrong partial)' if abs(r['cents']) > 300 else ''))
                if dyn != 'short' and (r['flip_frac'] > 0.05 or r['flip_run_ms'] >= 30):
                    why.append(f"flips ({r['flip_frac'] * 100:.0f} % of the sustain, {r['flip_run_ms']} ms run)")
                if r['start_ms'] is None or r['start_ms'] > 150:
                    why.append(f"starts late ({r['start_ms']} ms)")
            if why:
                fails.append(dict(midi=r['midi'], dyn=dyn, why=why))
    return dict(instrument=inst, notes=rows, failures=fails)
