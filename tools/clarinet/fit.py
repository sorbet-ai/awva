# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""CMA-ES fit of one clarinet preset's physics to the recordings (objective.py).

  python -I fit.py <bintag> <preset> <run> [--gens N] [--pop N] [--sigma s] [--start best.json]
                   [--workers N] [--only a,b,c] [--set all|fit]

Each candidate renders the fit grid (model.DYN per recorded dynamic) with clar_render and compares
every note with its recording. Parameters are searched in a normalised space (log for positive
ones). Writes fits/<run>/log.txt and best.json.
"""
import json
import math
import os
import pickle
import sys
import time
from concurrent.futures import ProcessPoolExecutor

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import model  # noqa: E402

ROOT = model.ROOT
import objective  # noqa: E402

# name: (lo, hi, log)
SPACE = {
    'pres_lin': (0.05, 1.0, False),
    'pres_sq': (0.0, 1.5, False),
    'pres_hz': (3.0, 40.0, True),
    'p_soft': (1.1, 1.6, False),
    'g_loud': (0.4, 1.2, False),
    'g_reg': (-0.3, 0.3, False),
    'p_curve': (0.4, 2.5, True),
    'zeta': (0.05, 0.8, True),
    'zeta_reg': (-1.0, 1.0, False),
    'reed_hz': (1000.0, 10000.0, True),
    'reed_q': (0.08, 1.0, True),
    'lay': (0.0, 10.0, False),
    'wall': (0.3, 5.0, True),
    'lat_hz': (500.0, 8000.0, True),
    'lat_q': (0.3, 1.0, False),
    'reg_pos': (0.08, 0.4, False),
    'reg_tau': (0.2, 2.0, True),
    'reg_r': (0.1, 2.0, True),
    'alt_rel': (19.0, 36.0, False),
    'tone_hz': (2500.0, 18000.0, True),
    'breath': (0.0, 0.05, False),
    'tongue': (0.3, 2.0, False),
    'acc_level': (0.0, 0.8, False),
    'acc_decay': (0.01, 0.2, True),
    'tongue_t': (0.002, 0.012, True),
    'tongue_f': (0.0, 3.0, False),
}
INST = {0: 'bb_clarinet', 1: 'bass_clarinet'}
LOW = {0: 50.0, 1: 34.0}


def to_unit(k, v):
    lo, hi, lg = SPACE[k]
    v = min(max(v, lo), hi)
    return math.log(v / lo) / math.log(hi / lo) if lg else (v - lo) / (hi - lo)


def from_unit(k, u):
    lo, hi, lg = SPACE[k]
    u = min(max(u, 0.0), 1.0)
    return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)


def grid(preset, which='fit'):
    R = pickle.load(open(os.path.join(ROOT, 'refs.pkl'), 'rb'))[INST[preset]]
    ref = {(F['note'], F['dyn']): F for F in R}
    notes = sorted({F['note'] for F in R})
    if which == 'fit':
        notes = notes[::2]
    else:  # hold-out: the others
        notes = notes[1::2]
    keys = [(n, d) for n in notes for d in ('pp', 'mf', 'ff') if (n, d) in ref]
    return keys, ref


def evaluate(bintag, preset, params, keys, ref, wid):
    jobs = [(f'n{n}{d}', n, d) for n, d in keys]
    X = model.render(bintag, preset, params, jobs, os.path.join(ROOT, 'tmp', f'w{preset}_{wid}'))
    pairs = []
    for (n, d) in keys:
        x = X[f'n{n}{d}']
        if not np.all(np.isfinite(x)) or np.abs(x).max() > 8.0:
            return 1e3, {'error': 'non-finite or blow-up'}
        M = model.features(x, n, d)
        pairs.append((M, ref[(n, d)]))
    c = objective.compare(pairs, LOW[preset])
    return objective.total(c), c


def score(args):
    bintag, preset, params, wid, keys, ref = args
    try:
        return evaluate(bintag, preset, params, keys, ref, wid)
    except Exception as ex:
        return 1e3, {'error': str(ex)[:200]}


def fmt(c):
    return ' '.join(f"{k}={c[k]:.3g}" for k in objective.SCALE if k in c) + (f" g={c['_g']:+.1f}" if '_g' in c else '')


def main():
    a = sys.argv[1:]
    bintag, preset, run = a[0], int(a[1]), a[2]
    gens, pop, sigma, start, workers, only = 100, 20, 0.2, None, 3, None
    i = 3
    while i < len(a):
        k, v = a[i], a[i + 1]
        if k == '--gens': gens = int(v)
        elif k == '--pop': pop = int(v)
        elif k == '--sigma': sigma = float(v)
        elif k == '--start': start = v
        elif k == '--workers': workers = int(v)
        elif k == '--only': only = v.split(',')
        i += 2
    import cma
    base = {}
    if start:
        with open(start) as f:
            base.update(json.load(f)['params'])
    keys_p = [k for k in SPACE if (only is None or k in only)]
    # the start point: the given parameters, else the preset's defaults (from the header via a probe)
    x0 = []
    for k in keys_p:
        if k in base:
            x0.append(to_unit(k, base[k]))
        else:
            x0.append(0.5)
    outd = os.path.join(ROOT, 'fits', run)
    os.makedirs(outd, exist_ok=True)
    log = open(os.path.join(outd, 'log.txt'), 'a')
    keys, ref = grid(preset)
    es = cma.CMAEvolutionStrategy(x0, sigma, {'bounds': [0, 1], 'popsize': pop, 'verbose': -9, 'seed': 1})
    t0 = time.time()
    best = (1e9, None, None)
    with ProcessPoolExecutor(max_workers=workers) as ex:
        if start:
            f0, c0 = score((bintag, preset, base, 0, keys, ref))
            best = (f0, dict(base), c0)
            print(f"start {f0:.3f} | {fmt(c0)}", file=log, flush=True)
        for g in range(gens):
            X = es.ask()
            cands = []
            for x in X:
                p = dict(base)
                for k, u in zip(keys_p, x):
                    p[k] = round(from_unit(k, u), 6)
                cands.append(p)
            res = list(ex.map(score, [(bintag, preset, p, j, keys, ref) for j, p in enumerate(cands)]))
            fs = [r[0] for r in res]
            es.tell(X, fs)
            j = int(np.argmin(fs))
            if fs[j] < best[0]:
                best = (fs[j], cands[j], res[j][1])
                with open(os.path.join(outd, 'best.json'), 'w') as f:
                    json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, f, indent=1)
            print(f"gen {g} {time.time() - t0:.0f}s min {fs[j]:.3f} med {np.median(fs):.3f} best {best[0]:.3f} sigma {es.sigma:.3f} | {fmt(res[j][1])}",
                  file=log, flush=True)
    print('done', best[0], file=log, flush=True)


if __name__ == '__main__':
    main()
