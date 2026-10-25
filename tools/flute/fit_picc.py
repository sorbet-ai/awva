# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""CMA-ES of the piccolo's player on its recordings (VSCO-2 CE, sustains at one dynamic): the
harmonics, the noise, the attack and the tone's rise, pitch and register; plus the tone's rise from
the key on the takes (hard <= 50 ms, soft <= 120 ms) and the in-range robustness (74..103).
The level across notes and the dynamics are not targets (one dynamic; the samples' gains unknown).
  python -I fit_picc.py <bintag> <run> [--gens N] [--pop N] [--sigma s] [--workers N] [--start json]"""
import json, math, os, sys, time
from concurrent.futures import ProcessPoolExecutor
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT  # noqa: E402
import evalm, takes, robust  # noqa: E402
import objective as O  # noqa: E402
O.WEIGHT.update(trise=3.0, lvl=0.0, dyn=0.0)
SPACE = {
    'p_soft': (233.3, 80.0, 800.0, True), 'p_loud': (1920.0, 500.0, 6000.0, True), 'p_reg': (0.773, 0.0, 1.6, False),
    'theta': (0.473, 0.3, 0.7, False), 'jet_h': (0.388, 0.15, 1.2, True), 'jet_w': (10.77, 3.0, 18.0, True),
    'g_soft': (1.053, 1.05, 4.0, True), 'y0': (0.207, -1.0, 1.0, False), 'jet_st': (0.496, 0.05, 1.0, True),
    'bore_decay': (0.0233, 0.004, 0.1, True), 'bore_reg': (0.849, -0.3, 1.5, False), 'cut_hz': (9169.0, 2000.0, 12000.0, True),
    'wall': (7.21, 1.0, 40.0, True), 'dipole': (1.467, 0.0, 6.0, False), 'tone_hz': (1951.0, 800.0, 16000.0, True),
    'turb': (0.2965, 0.0, 1.0, False), 'hiss': (0.005222, 0.0, 0.05, False), 'hiss_st': (0.1727, 0.03, 0.6, True),
    'tongue': (0.517, 0.2, 2.0, False), 'tongue_t': (0.003666, 0.0008, 0.03, True),
    'att_gain': (32.14, 0.0, 40.0, False), 'att_t': (0.0327, 0.008, 0.1, True),
}
NOTES = list(range(74, 104, 2))


def to_unit(k, v):
    s, lo, hi, lg = SPACE[k]; v = min(max(v, lo), hi)
    return math.log(v / lo) / math.log(hi / lo) if lg else (v - lo) / (hi - lo)


def from_unit(k, u):
    s, lo, hi, lg = SPACE[k]; u = min(max(u, 0.0), 1.0)
    return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)


def score(args):
    bt, p, wid = args
    try:
        comp, det, rows = evalm.evaluate(bt, 'piccolo', p, os.path.join(ROOT, 'tmp', f'pw{wid}'), 1)
        if comp is None:
            return 1e3, {'error': 'non-finite'}
        h, s, nh, ns = takes.summary(takes.rises(bt, 2, p, f'pt{wid}'))
        bf, bad = robust.bad_fraction(bt, 2, p, NOTES, tag=f'pr{wid}')
    except Exception as ex:
        return 1e3, {'error': str(ex)[:200]}
    comp['takes_hard'], comp['takes_soft'], comp['robust_bad'] = h, s, bf
    comp['total'] += max(0.0, h - 0.05) / 0.01 + max(0.0, s - 0.12) / 0.02 + 3.0 * bf / 0.05
    return comp['total'], comp


def fmt(c):
    return O.fmt(c) + f" takes {1000*c.get('takes_hard',0):.0f}/{1000*c.get('takes_soft',0):.0f} ms robust_bad {c.get('robust_bad',0):.3f}"


def main():
    a = sys.argv[1:]
    bt, run = a[0], a[1]
    gens, pop, sigma, workers, start = 60, 16, 0.12, 5, None
    i = 2
    while i < len(a):
        k, v = a[i], a[i + 1]
        if k == '--gens': gens = int(v)
        elif k == '--pop': pop = int(v)
        elif k == '--sigma': sigma = float(v)
        elif k == '--workers': workers = int(v)
        elif k == '--start': start = v
        i += 2
    import cma
    base = {k: s[0] for k, s in SPACE.items()}
    if start:
        base.update({k: v for k, v in json.load(open(start))['params'].items() if k in SPACE})
    keys = list(SPACE)
    outd = os.path.join(ROOT, 'fits', run); os.makedirs(outd, exist_ok=True)
    log = open(os.path.join(outd, 'log.txt'), 'a')
    es = cma.CMAEvolutionStrategy([to_unit(k, base[k]) for k in keys], sigma, {'bounds': [0, 1], 'popsize': pop, 'verbose': -9, 'seed': 3})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=workers) as ex:
        f0, c0 = score((bt, base, 0))
        best = (f0, dict(base), c0)
        json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, open(os.path.join(outd, 'best.json'), 'w'), indent=1)
        print(f"start {f0:.3f} | {fmt(c0) if 'error' not in c0 else c0}", file=log, flush=True)
        for g in range(gens):
            X = es.ask()
            cands = [{k: round(from_unit(k, u), 6) for k, u in zip(keys, x)} for x in X]
            res = list(ex.map(score, [(bt, p, j) for j, p in enumerate(cands)]))
            fs = [r[0] for r in res]
            es.tell(X, fs)
            j = int(np.argmin(fs))
            if fs[j] < best[0]:
                best = (fs[j], cands[j], res[j][1])
                json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, open(os.path.join(outd, 'best.json'), 'w'), indent=1)
            print(f"gen {g} {time.time()-t0:.0f}s min {fs[j]:.3f} best {best[0]:.3f} sigma {es.sigma:.3f} | {fmt(res[j][1]) if 'error' not in res[j][1] else res[j][1]['error']}", file=log, flush=True)
    print('done', best[0], file=log, flush=True)


if __name__ == '__main__':
    main()
