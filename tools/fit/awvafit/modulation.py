# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""No performer modulation: an engine played steadily (one pressure, no bend) should hold its pitch and
level. Vibrato, waver, wander and jitter are the performer's, and the Striso player supplies them
through bend and pressure. An engine that adds its own doubles or fights the player's. A fit on
recordings whose streams carried no pitch learnt the recorded player's motion into such parameters (the
bowed fit v1's 15-20 c vibrato, double reed v2's wander and jitter). Every fit is checked for it, and a
parameter that adds it is suspect.

  motion(x, ev)                  per held note: the pitch's and the level's motion (sd, detrended, from
                                 HOLD_FROM s in) and the pitch motion's strongest rate (Hz)
  steady(engine, preset, params) the engine's motion over steady held notes (the median over them)
  audit(engine, preset, base, fit)  each parameter the fit moved, set alone on the baseline: the motion it
                                 adds; suspect where it adds more than PITCH_C cents or LEVEL_DB dB
  penalty(m)                     a fit's penalty for its steady motion past the floors (0 within them)

  python -m awvafit modulation <engine> <preset> <fit.json> [--base base.json] [--notes 55,62,69]
"""
import json
import os

import numpy as np

from . import env
from .controls import note_events, read
from .keys import Stream
from .render import Renderer

SR = 48000
BLOCK = 32
HOLD = 2.5         # s: each steady note
HOLD_FROM = 0.5    # s into the note before its motion counts (the attack settled)
PRESSURES = (0.25, 0.5)
PITCH_C = 1.0      # cents (sd): the most pitch motion a parameter may add over the baseline
LEVEL_DB = 0.3     # dB (sd): the most level motion
FLOOR_PITCH_C = 1.5   # an engine's own steady motion above which a fit is penalised
FLOOR_LEVEL_DB = 0.5


def _detrend(y):
    t = np.arange(len(y))
    return y - np.polyval(np.polyfit(t, y, 1), t) if len(y) > 2 else y - np.mean(y)


def motion(x, ev, sr=SR, hop=0.01):
    """the held note `ev` (note_events: b_on, b_off, note) of render x: dict(pitch_c, level_db, rate_hz)"""
    from .control import env_db
    from .range_check import yin
    a = ev['b_on'] * BLOCK / sr + HOLD_FROM
    b = ev['b_off'] * BLOCK / sr - 0.1
    seg = x[int(a * sr):int(b * sr)]
    if len(seg) < int(0.5 * sr):
        return dict(pitch_c=None, level_db=None, rate_hz=None)
    f = 440.0 * 2 ** ((ev['note'] - 69) / 12)
    y = yin(seg, f * 2 ** (-2 / 12), f * 2 ** (2 / 12), sr, 0.04, hop)
    ok = np.isfinite(y[:, 0]) & (y[:, 1] < 0.3)
    pitch_c = rate = None
    if ok.sum() > 20:
        c = 1200 * np.log2(y[ok, 0] / f)
        c = _detrend(c - np.median(c))
        pitch_c = float(np.std(c))
        sp = np.abs(np.fft.rfft(c * np.hanning(len(c))))
        fr = np.fft.rfftfreq(len(c), hop)
        band = (fr >= 2) & (fr <= 12)
        rate = float(fr[band][np.argmax(sp[band])]) if band.any() else None
    L = env_db(seg, sr, hop)
    L = L[np.isfinite(L)]
    if len(L) > 3:  # over 30 ms: a 10 ms window's ripple off a period's grid, not a tremolo (5-8 Hz) gone
        L = np.convolve(L, np.ones(3) / 3, mode='valid')
    level_db = float(np.std(_detrend(L))) if len(L) > 20 else None
    return dict(pitch_c=pitch_c, level_db=level_db, rate_hz=rate)


def _med(v):
    v = [u for u in v if u is not None]
    return float(np.median(v)) if v else None


def steady(engine, preset, params=None, notes=(55, 62, 69), exe=None, workdir=None, tag='steady', transpose=0):
    """the engine's own motion over steady held notes (each note at PRESSURES, a soft strike, no bend):
    dict(pitch_c, level_db, rate_hz, rows); notes sounding, pressed `transpose` semitones lower (an octave
    multiple: the key layout reaches 34-90)"""
    d = workdir or env.workdir('tmp', 'modulation')
    os.makedirs(d, exist_ok=True)
    s, t = Stream(), 0.3
    for n in notes:
        for P in PRESSURES:
            t = s.press(t, 0.2, P, HOLD, note=n - transpose) + 0.6
    csv = os.path.join(d, f'{tag}.csv')
    s.write(csv, notes=False)
    out, ctl = os.path.join(d, f'{tag}.f32'), os.path.join(d, f'{tag}.ctl')
    x = Renderer(exe).render(engine, preset, rec=csv, sets=params, transpose=transpose, settle=1.0,
                          tail=0.5, out=out, ctl_out=ctl).x.astype(np.float64)
    rows = [dict(note=e['note'], **motion(x, e)) for e in note_events(read(ctl)[0])]
    return dict(pitch_c=_med([r['pitch_c'] for r in rows]), level_db=_med([r['level_db'] for r in rows]),
                rate_hz=_med([r['rate_hz'] for r in rows]), rows=rows)


def penalty(m):
    """a fit's penalty for its steady motion (steady()) past FLOOR_PITCH_C and FLOOR_LEVEL_DB, in floors"""
    p = max(0.0, (m['pitch_c'] or 0.0) - FLOOR_PITCH_C) / FLOOR_PITCH_C
    q = max(0.0, (m['level_db'] or 0.0) - FLOOR_LEVEL_DB) / FLOOR_LEVEL_DB
    return p + q


def audit(engine, preset, base, fit, notes=(55, 62, 69), exe=None, workdir=None, log=print, transpose=0):
    """each parameter the fit moved (fit[k] != base.get(k)), set alone on the baseline, and the whole fit:
    the steady motion each adds over the baseline's. -> dict(base, fit, params: [dict(name, old, new,
    pitch_c, level_db, rate_hz, added_pitch_c, added_level_db, suspect)], suspects: [names])"""
    base = dict(base or {}) or {k: v.value for k, v in Renderer(exe).params(engine, preset).items()}  # the preset as built
    moved = [k for k in sorted(fit) if base.get(k) != fit[k]]
    m0 = steady(engine, preset, base, notes, exe, workdir, 'base', transpose)
    log(f"  baseline: pitch {m0['pitch_c']:.2f} c, level {m0['level_db']:.2f} dB")
    rows = []
    for k in moved:
        m = steady(engine, preset, {**base, k: fit[k]}, notes, exe, workdir, 'p_' + k, transpose)
        dp = (m['pitch_c'] or 0) - (m0['pitch_c'] or 0)
        dl = (m['level_db'] or 0) - (m0['level_db'] or 0)
        sus = dp > PITCH_C or dl > LEVEL_DB
        rows.append(dict(name=k, old=base.get(k), new=fit[k], pitch_c=m['pitch_c'], level_db=m['level_db'], rate_hz=m['rate_hz'],
                         added_pitch_c=round(dp, 2), added_level_db=round(dl, 2), suspect=sus))
        log(f"  {k}: {base.get(k)} -> {fit[k]}: +{dp:.2f} c, +{dl:.2f} dB{'  SUSPECT' if sus else ''}")
    mf = steady(engine, preset, {**base, **fit}, notes, exe, workdir, 'fit', transpose)
    log(f"  the whole fit: pitch {mf['pitch_c']:.2f} c, level {mf['level_db']:.2f} dB, penalty {penalty(mf):.2f}")
    return dict(base={k: v for k, v in m0.items() if k != 'rows'}, fit={k: v for k, v in mf.items() if k != 'rows'},
                penalty=penalty(mf), params=rows, suspects=[r['name'] for r in rows if r['suspect']])


def main(argv=None):
    import argparse
    p = argparse.ArgumentParser(prog='awvafit modulation', description=__doc__.split('\n\n')[0])
    p.add_argument('engine')
    p.add_argument('preset')
    p.add_argument('fit', help='the fitted values: a JSON {name: value} (or a preset file with "params")')
    p.add_argument('--base', help='the baseline values (default: the preset as built)')
    p.add_argument('--notes', default='55,62,69')
    p.add_argument('--transpose', type=int, default=0, help='semitones the engine plays above the keys (bass: -24)')
    p.add_argument('--out')
    a = p.parse_args(argv)

    def load(path):
        with open(path, encoding='utf-8') as f:
            d = json.load(f)
        return d.get('params', d) if isinstance(d, dict) else d
    rep = audit(a.engine, int(a.preset) if a.preset.isdigit() else a.preset, load(a.base) if a.base else {}, load(a.fit),
                tuple(int(n) for n in a.notes.split(',')), transpose=a.transpose)
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as f:
            json.dump(rep, f, indent=1)
    print('suspect: ' + (', '.join(rep['suspects']) or 'none'))
    return 1 if rep['suspects'] else 0
