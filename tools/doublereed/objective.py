# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The flute model against the Iowa recordings, note by note on the same pitches and dynamics.

The fit set: every recorded note of an instrument (non-vibrato), rendered on the model as one held
note on voice 0 (1.0 s), at the key pressure of its dynamic (DYN_P) with a moderate tongued strike.
Features (feat.py) per note; components (each over notes, then weighted, scales ~ "clearly audible"):
  harm   steady harmonic profile H1..H8 re the level, dB (clipped at -50), RMS, H1-H3 weighted x2
  lvl    sustain level across notes and dynamics after one offset per instrument, |dB| (the dynamic
         range and the register balance; the recordings kept one input gain)
  noise  the floor between harmonics in 5 bands re the level, |dB| (clipped at -95)
  atk    attack windows (0-30, 30-60, 60-120 ms): H1..H4 and three noise bands re the sustain, |dB|
  rise   |log2 rise time ratio| (floor 20 ms)
  pitch  |f0 re the note| of the model (cents, clipped 50: the player is in tune)
  dyn    the timbre's response to the dynamic: per pitch recorded at two dynamics, the change of H2..H5
         (re the level) from the softer to the louder, model vs recording, |dB|
  trise  |log2| of the tone's rise (harmonics 1-3 within 3 dB of their sustain; the model from the
         key, the recording from its first sound), floor 25 ms
  bad    fraction of notes that don't speak in their register (aperiodic, period doubled, octave off)
"""
import numpy as np

DYN_P = dict(pp=0.25, mf=0.55, ff=1.0)
STRIKE = 0.8  # the board's strikes: vpres peaks 0.5-1.0, the key rising in ~30-40 ms (the recorded takes)
HOLD = 1.0
SCALE = dict(pw=0.5, aw=0.5, adev=0.7, trise=0.5, dyn=4.0, harm=4.0, lvl=3.0, noise=5.0, atk=6.0, rise=0.7, pitch=10.0, bad=0.05)
WEIGHT = dict(pw=1.5, aw=1.5, adev=1.5, trise=3.0, dyn=2.0, harm=3.0, lvl=1.5, noise=1.0, atk=1.0, rise=0.5, pitch=0.5, bad=3.0)
HW = np.array([2, 2, 2, 1, 1, 1, 1, 1], float)


def note_cost(R, M):
    c = {}
    hr = np.maximum(R['harm'][:8], -50)
    hm = np.maximum(M['harm'][:8], -50)
    c['harm'] = float(np.sqrt(np.sum(HW * (hm - hr) ** 2) / HW.sum()))
    nr = np.maximum(R['noise'], -95)
    nm = np.maximum(M['noise'], -95)
    c['noise'] = float(np.mean(np.abs(nm - nr)))
    ar = np.maximum(R['atk'], -70)
    am = np.maximum(M['atk'], -70)
    c['atk'] = float(np.mean(np.minimum(np.abs(am - ar), 30)))
    c['rise'] = float(abs(np.log2(max(M['rise'], 0.02) / max(R['rise'], 0.02))))
    # the tone's rise, from the key (the model) vs from the first sound (the recording), floor 25 ms
    # the tone's life: its pitch and level wobble and the attack's pitch excursion, re the recording's
    # (|log2| of the ratio, floors 0.5 c, 0.1 dB, 15 c)
    c['pw'] = float(abs(np.log2(max(M['pw'], 0.5) / max(R['pw'], 0.5))))
    c['aw'] = float(abs(np.log2(max(M['aw'], 0.1) / max(R['aw'], 0.1))))
    c['adev'] = float(abs(np.log2(max(M['adev'], 15.0) / max(R['adev'], 15.0))))
    c['trise'] = float(abs(np.log2(max(M['trise'], 0.025) / max(R['trise'], 0.025))))
    c['pitch'] = float(min(abs(M['f0c']), 50.0)) if np.isfinite(M['f0c']) else 50.0
    # (no octave-up test from H2 > H1: the double reeds' low notes have it naturally)
    bad = (M['per'] < 0.8) or (M['sub'] > -15) or not np.isfinite(M['f0c']) or abs(M['f0c']) > 50
    c['bad'] = 1.0 if bad else 0.0
    return c


def combine(pairs):
    """pairs: [(R, M)] -> components, total"""
    per = [note_cost(R, M) for R, M in pairs]
    comp = {k: float(np.mean([p[k] for p in per])) for k in per[0]}
    lr = np.array([R['lvl'] for R, M in pairs])
    lm = np.array([M['lvl'] for R, M in pairs])
    d = lm - lr
    off = float(np.median(d))
    comp['lvl'] = float(np.mean(np.minimum(np.abs(d - off), 20)))
    by = {}
    for R, M in pairs:
        by.setdefault((R['inst'], R['note']), {})[R['dyn']] = (R, M)
    dd = []
    for g in by.values():
        for a, b in (('pp', 'mf'), ('mf', 'ff'), ('pp', 'ff')):
            if a in g and b in g:
                dr = np.maximum(g[b][0]['harm'][1:5], -50) - np.maximum(g[a][0]['harm'][1:5], -50)
                dm = np.maximum(g[b][1]['harm'][1:5], -50) - np.maximum(g[a][1]['harm'][1:5], -50)
                dd.append(np.mean(np.minimum(np.abs(dm - dr), 30)))
    comp['dyn'] = float(np.mean(dd)) if dd else 0.0
    comp['_off'] = off
    comp['total'] = float(sum(WEIGHT[k] * comp[k] / SCALE[k] for k in SCALE))
    return comp, per


def fmt(c):
    return ' '.join(f"{k}={c[k]:.3g}" for k in list(SCALE) + ['total'] if k in c)
