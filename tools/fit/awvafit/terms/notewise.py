# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Model notes against recorded notes, note x dynamic: per-note distances, then the means, with one
level offset per preset (the recordings keep one input gain; the engine's gain is free). Three
comparisons, one per note analysis (features.notes), each the lane's exactly:

harmonic_sum (the sax) - compare(model, refs, inst, src, objective, use_dyn_level, loud):
  spec    log-band harmonic spectrum (re total, floored -50), RMS over bands (dB)
  harm    harmonics 1..8 re total, mean |dB|
  bright  harmonic power above 1.5 kHz re total (dB); the loud dynamics x 1.5
  hnr     harmonic-to-noise per broad band, where the recording has harmonics (dB, clipped 20)
  dyn     level of each dynamic re the loudest, per note (dB; recordings with real levels only)
  attack  rise time (ms / 10), overshoot (dB), attack brightness (dB), / 3: on the loud notes
  pitch   |sustain pitch re the note| (cents, clipped 100)
  robust  notes that don't speak, squeak, double their period or are off by > 60 cents (fraction)
  strike_term: the attack must follow the strike: rise(0.95) + 5 ms < rise(0.1), rise(breath) >= 60 ms

windowed (the flute) - combine(pairs, objective) over (R, M) per note:
  harm (H1..H8 re the level, clipped -50, RMS, H1-H3 x2), noise (floor between harmonics, 5 bands),
  atk (attack windows: H1..H4 and three noise bands), rise (|log2| ratio, floor 20 ms), trise (the
  tone's rise from the key, |log2|, floor 25 ms), pitch (cents, clipped 50), bad (doesn't speak in
  its register), lvl (|dB| after one offset), dyn (the change of H2..H5 between two dynamics)

lobes (the clarinet) - compare(pairs, low) over (M, R):
  spec (harmonics re total, floored at the recording's noise floor + 3 dB and -60, RMS, first 8 x2),
  lvl, noise (more noise than the recording counts x3), rise, acent, aharm (mf and ff), pitch,
  robust (25 dB below after the offset, or < 60 % harmonic), even (the chalumeau's H2 re H1)
"""
import numpy as np

# --- harmonic_sum: the sax ---------------------------------------------------------------------


def bad_note(f):
    """a note that doesn't speak, squeaks, doubles its period, is off pitch or lost its fundamental"""
    return f is None or f['lvl'] < -90 or f['per'] < 0.5 or abs(f['f0c']) > 60 or f['sub'] > -12 or f['H'][0] < -30


def compare(model, R, inst, src, objective, use_dyn_level, loud=('ff', 'f')):
    """model: [dict(dyn, slot, note, v, f)], R: {(inst, src, dyn, note): features}"""
    LOUD = set(loud)
    SCALE = objective.scale
    c = {k: [] for k in ('spec', 'harm', 'bright', 'hnr', 'attack', 'pitch', 'robust')}
    lv_m, lv_r = {}, {}
    for m in model:
        r = R.get((inst, src, m['dyn'], m['note']))
        f = m['f']
        bad = bad_note(f)
        c['robust'].append(1.0 if bad else 0.0)
        if f is None:
            continue
        c['pitch'].append(min(abs(f['f0c']), 100.0))
        if r is None:
            continue
        if bad:  # nothing meaningful to compare: count the worst
            for k in ('spec', 'harm', 'bright', 'hnr'):
                c[k].append(3 * SCALE[k])
            continue
        mb, rb = np.maximum(f['band'], -50), np.maximum(r['band'], -50)
        ok = np.isfinite(mb) & np.isfinite(rb) & ((mb > -50) | (rb > -50))
        if ok.any():
            c['spec'].append(float(np.sqrt(np.mean((mb[ok] - rb[ok]) ** 2))))
        hm, hr = np.maximum(f['H'][:8], -50), np.maximum(r['H'][:8], -50)
        okh = (hm > -50) | (hr > -50)
        c['harm'].append(float(np.mean(np.abs(hm[okh] - hr[okh]))) if okh.any() else 0.0)
        c['bright'].append(abs(f['bright'] - r['bright']) * (1.5 if m['dyn'] in LOUD else 1.0))
        okn = np.isfinite(f['hnr']) & np.isfinite(r['hnr'])
        if okn.any():
            c['hnr'].append(float(np.mean(np.minimum(np.abs(f['hnr'][okn] - r['hnr'][okn]), 20))))
        if m['dyn'] in LOUD or src == 'mtg':
            a = min(abs(min(f['t_rise'], 300) - min(r['t_rise'], 300)), 200) / 10.0
            a += min(abs(f['over'] - r['over']), 12)
            a += min(abs(f['a_br'] - r['a_br']), 15)
            c['attack'].append(a / 3.0)
        lv_m[(m['dyn'], m['note'])] = f['lvl']
        lv_r[(m['dyn'], m['note'])] = r['lvl']
    out = {k: (float(np.mean(v)) if v else 0.0) for k, v in c.items()}
    d = []  # dynamics: each level re the loudest, per note
    if use_dyn_level:
        for (dy, n) in lv_m:
            ln = ('ff', n) if ('ff', n) in lv_m else (('f', n) if ('f', n) in lv_m else None)
            if dy in LOUD or ln is None:
                continue
            d.append(min(abs((lv_m[(dy, n)] - lv_m[ln]) - (lv_r[(dy, n)] - lv_r[ln])), 20))
    out['dyn'] = float(np.mean(d)) if d else 0.0
    return out


def strike_term(model, slots=3):
    """rise(0.95) + 5 ms < rise(0.1) and rise(breath) >= 60 ms, per slot; and every note speaks"""
    pen, by = [], {}
    for m in model:
        if m['f'] is None or bad_note(m['f']):
            pen.append(1.0)
            continue
        by[(m['slot'], m['v'])] = m['f']['t_rise']
    for k in range(slots):
        if (k, 0.95) in by and (k, 0.1) in by:
            pen.append(max(by[(k, 0.95)] + 5 - by[(k, 0.1)], 0) / 20.0)
        if (k, 0.0) in by:
            pen.append(max(60 - by[(k, 0.0)], 0) / 30.0)
    return float(np.mean(pen)) if pen else 0.0


# --- windowed: the flute -----------------------------------------------------------------------

HW = np.array([2, 2, 2, 1, 1, 1, 1, 1], float)


def flute_note_cost(R, M):
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
    c['trise'] = float(abs(np.log2(max(M['trise'], 0.025) / max(R['trise'], 0.025))))
    c['pitch'] = float(min(abs(M['f0c']), 50.0)) if np.isfinite(M['f0c']) else 50.0
    bad = (M['per'] < 0.8) or (M['sub'] > -15) or (M['oct_up'] > 0) or not np.isfinite(M['f0c']) or abs(M['f0c']) > 50
    c['bad'] = 1.0 if bad else 0.0
    return c


def flute_combine(pairs, objective):
    """pairs: [(R, M)] -> components with total, per-note costs"""
    per = [flute_note_cost(R, M) for R, M in pairs]
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
    comp['total'] = objective.total(comp)
    return comp, per


# --- lobes: the clarinet -----------------------------------------------------------------------


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


def clarinet_compare(pairs, objective, low=None):
    """pairs: [(M, R)] feature dicts (the model's with its harmonic share 'hshare')"""
    d = {k: [] for k in objective.scale}
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
        # (more noise than the recording counts three times: v1's was tagged breathy-attack)
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
