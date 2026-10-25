# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The match objective: model notes against the recorded ones (per note x dynamic), one preset.

Components (each with a scale = "clearly audible"; mean over the notes):
  spec    sustain harmonic spectrum (dB re the harmonic total, both floored at the recording's
          noise floor + 3 dB and at -60 dB), RMS over the harmonics below 10 kHz (the first 8
          weighted double)
  lvl     sustain level after one offset per preset (the dynamics and register balance)
  noise   inter-harmonic noise re the note's power, bands 0-1.5, 1.5-4, 4-8 kHz (floored)
  rise    attack time 10 -> 90 %, |log2 ratio| (mf, ff: the pp takes are swells)
  acent   attack centroid re the sustain (octaves, mf and ff)
  aharm   attack harmonic shape, the first 8 (mf, ff)
  pitch   |cents| (the model, re the note: in tune)
  robust  per note 1 if the model doesn't speak (25 dB below the reference after the offset) or
          plays another register (less than 60 % of its energy on the note's harmonics)
"""
import numpy as np

SCALE = dict(even=4.0, spec=3.0, lvl=2.5, noise=6.0, rise=0.5, acent=0.5, aharm=4.0, pitch=10.0, robust=0.05)
WEIGHT = dict(even=1.5, spec=3.0, lvl=2.0, noise=0.6, rise=1.0, acent=0.7, aharm=1.0, pitch=0.5, robust=3.0)


def harm_dist(m, r, floor):
    n = min(len(m), len(r))
    m, r, fl = m[:n], r[:n], floor[:n]
    ok = np.isfinite(m) & np.isfinite(r)
    if not ok.any():
        return 0.0
    lo = np.maximum(np.nan_to_num(fl, nan=-90) + 3.0, -60.0)
    a = np.maximum(m, lo)[ok]
    b = np.maximum(r, lo)[ok]
    w = np.where(np.arange(n)[ok] < 8, 2.0, 1.0)
    return float(np.sqrt(np.sum(w * (a - b) ** 2) / np.sum(w)))


def compare(pairs, low=None):
    """pairs: list of (M, R) feature dicts (model with 'harmony' share 'hshare'). Returns comps."""
    d = {k: [] for k in SCALE}
    lv = np.array([M['lvl'] - R['lvl'] for M, R in pairs])
    ok = lv > -200
    g = float(np.median(lv[ok])) if ok.any() else 0.0
    for (M, R), dl in zip(pairs, lv):
        bad = (dl - g < -25.0) or (M.get('hshare', 1.0) < 0.6)
        d['robust'].append(1.0 if bad else 0.0)
        if bad:
            continue
        d['spec'].append(harm_dist(M['harm'], R['harm'], R['hfloor']))
        d['lvl'].append(abs(dl - g))
        fl = R['nfloor'][:3] + 3.0
        # (more noise than the recording counts three times: the user heard v1 as airy)
        dn = np.maximum(M['nhr'][:3], fl) - np.maximum(R['nhr'][:3], fl)
        d['noise'].append(float(np.mean(np.where(dn > 0, 3.0 * dn, -dn))))
        d['pitch'].append(min(abs(M['cents']), 50.0))
        if low is not None and R['note'] < low + 18.5 and R['dyn'] != 'pp':
            # the chalumeau's 2nd harmonic re its fundamental (the cylinder's weakest partial)
            dm, dr = M['harm'][1] - M['harm'][0], R['harm'][1] - R['harm'][0]
            if np.isfinite(dm) and np.isfinite(dr):
                d['even'].append(min(abs(dm - dr), 20.0))
        if R['dyn'] != 'pp':
            d['rise'].append(min(abs(np.log2(max(M['rise'], 4.0) / max(R['rise'], 4.0))), 1.5))
            d['acent'].append(abs(M['a_cent'] - R['a_cent']))
            d['aharm'].append(harm_dist(M['a_harm'], R['a_harm'], np.full(8, -63.0)))
    c = {k: (float(np.mean(v)) if v else 0.0) for k, v in d.items()}
    c['_g'] = g
    return c


def total(c):
    return float(sum(WEIGHT[k] * c[k] / SCALE[k] for k in SCALE))
