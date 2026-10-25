# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""CMA-ES fit of the flute model's physics to the Iowa recordings (objective.py).

  python -I fit.py <bintag> <inst> <run> [--gens N] [--pop N] [--sigma s] [--start best.json]
                  [--workers N] [--every n] [--only a,b,...]
Searches a normalised space (log for positive parameters). Writes fits/<run>/log.txt and best.json.
"""
import json
import math
import os
import sys
import time
from concurrent.futures import ProcessPoolExecutor

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT, replay  # noqa: E402
sys.path.insert(0, HERE)
import evalm  # noqa: E402
import objective as O  # noqa: E402

# name: (start, lo, hi, log)
SPACE = {
    'p_soft': (150.0, 40.0, 600.0, True),
    'p_loud': (900.0, 200.0, 4000.0, True),
    'p_reg': (0.6, 0.0, 1.6, False),
    'p_curve': (1.0, 0.4, 2.5, True),
    'pres_hz': (14.0, 4.0, 40.0, True),
    'theta': (0.5, 0.3, 0.8, False),
    'theta_reg': (0.0, -0.4, 0.4, False),
    'w_track': (0.6, 0.0, 1.0, False),
    'jet_h': (1.0, 0.4, 2.0, True),
    'h_loud': (0.3, -0.5, 1.5, False),
    'jet_w': (10.0, 4.0, 18.0, True),
    'g_soft': (1.3, 1.05, 6.0, True),
    'amp_loud': (0.5, -1.5, 2.5, False),
    'y0': (0.4, -3.0, 3.0, False),
    'jet_st': (0.08, 0.02, 0.5, True),
    'vortex': (0.6, 0.2, 3.0, True),
    'bore_decay': (0.025, 0.006, 0.12, True),
    'bore_reg': (0.5, -0.3, 1.5, False),
    'cut_hz': (2000.0, 600.0, 12000.0, True),
    'wall': (4.0, 1.0, 30.0, True),
    'disp': (0.0, -0.6, 0.6, False),
    'end': (0.5, 0.0, 3.0, False),
    'dipole': (0.3, 0.0, 6.0, False),
    'tone_hz': (9000.0, 800.0, 16000.0, True),
    'tone_q': (0.6, 0.5, 1.5, False),
    'turb': (0.02, 0.0, 0.5, False),
    'turb_hz': (2000.0, 200.0, 10000.0, True),
    'hiss': (0.002, 0.0, 0.05, False),
    'hiss_st': (0.15, 0.03, 0.6, True),
    't_noise': (0.3, 0.0, 2.0, False),
    'tongue': (1.2, 0.3, 2.0, False),
    'acc_level': (0.4, 0.0, 1.5, False),
    'acc_decay': (0.03, 0.008, 0.2, True),
    'tongue_t': (0.012, 0.001, 0.08, True),
}


def to_unit(name, v):
    s, lo, hi, lg = SPACE[name]
    v = min(max(v, lo), hi)
    return (math.log(v / lo) / math.log(hi / lo)) if lg else (v - lo) / (hi - lo)


def from_unit(name, u):
    s, lo, hi, lg = SPACE[name]
    u = min(max(u, 0.0), 1.0)
    return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)


def score(args):
    bintag, inst, params, wid, every = args
    try:
        comp, det, rows = evalm.evaluate(bintag, inst, params, os.path.join(ROOT, 'tmp', f'w{wid}'), every)
    except Exception as ex:
        return 1e3, {'error': str(ex)[:200]}
    if comp is None:
        return 1e3, {'error': 'non-finite'}
    return comp['total'], comp


def main():
    a = sys.argv[1:]
    bintag, inst, run = a[0], a[1], a[2]
    gens, pop, sigma, start, workers, every, only, seed = 200, 20, 0.2, None, 5, 2, None, 7
    i = 3
    while i < len(a):
        k, v = a[i], a[i + 1]
        if k == '--gens': gens = int(v)
        elif k == '--pop': pop = int(v)
        elif k == '--sigma': sigma = float(v)
        elif k == '--start': start = v
        elif k == '--workers': workers = int(v)
        elif k == '--every': every = int(v)
        elif k == '--only': only = v.split(',')
        elif k == '--seed': seed = int(v)
        i += 2
    import cma
    base = {k: s[0] for k, s in SPACE.items()}
    if start:
        with open(start) as f:
            base.update({k: v for k, v in json.load(f)['params'].items() if k in SPACE})
    keys = [k for k in SPACE if (only is None or k in only)]
    x0 = [to_unit(k, base[k]) for k in keys]
    outd = os.path.join(ROOT, 'fits', run)
    os.makedirs(outd, exist_ok=True)
    log = open(os.path.join(outd, 'log.txt'), 'a')
    es = cma.CMAEvolutionStrategy(x0, sigma, {'bounds': [0, 1], 'popsize': pop, 'verbose': -9, 'seed': seed})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=workers) as ex:
        f0, c0 = score((bintag, inst, base, 0, every))
        best = (f0, dict(base), c0)
        print(f"start {f0:.3f} | {O.fmt(c0) if 'error' not in c0 else c0}", file=log, flush=True)
        with open(os.path.join(outd, 'best.json'), 'w') as f:
            json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, f, indent=1)
        for g in range(gens):
            X = es.ask()
            cands = []
            for x in X:
                p = dict(base)
                for k, u in zip(keys, x):
                    p[k] = round(from_unit(k, u), 6)
                cands.append(p)
            res = list(ex.map(score, [(bintag, inst, p, j, every) for j, p in enumerate(cands)]))
            fs = [r[0] for r in res]
            es.tell(X, fs)
            j = int(np.argmin(fs))
            if fs[j] < best[0]:
                best = (fs[j], cands[j], res[j][1])
                with open(os.path.join(outd, 'best.json'), 'w') as f:
                    json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, f, indent=1)
            print(f"gen {g} {time.time() - t0:.0f}s min {fs[j]:.3f} med {np.median(fs):.3f} best {best[0]:.3f} sigma {es.sigma:.3f} | "
                  f"{O.fmt(res[j][1]) if 'error' not in res[j][1] else res[j][1]['error']}", file=log, flush=True)
    print('done', best[0], file=log, flush=True)


if __name__ == '__main__':
    main()
