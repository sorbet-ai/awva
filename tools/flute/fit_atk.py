# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""CMA-ES of the attack parameters only (the sustain's stay as baked): the recordings' objective
with the tone's rise weighted, plus the recorded Striso takes' rise (hard strikes <= 50 ms, soft
<= 120 ms, from the key).
  python -I fit_atk.py <bintag> <inst> <run> [--gens N] [--pop N] [--sigma s] [--workers N]"""
import json, math, os, sys, time
from concurrent.futures import ProcessPoolExecutor
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT, replay  # noqa: E402
sys.path.insert(0, HERE)
import evalm, takes
import objective as O
O.WEIGHT['trise'] = 3.0
SPACE = {
    'tongue': (0.4, 0.2, 2.0, False),
    'acc_level': (0.05, 0.0, 1.5, False),
    'acc_decay': (0.03, 0.008, 0.08, True),
    'tongue_t': (0.003, 0.0008, 0.03, True),
    't_noise': (1.2, 0.0, 3.0, False),
    'att_gain': (2.0, 0.0, 40.0, False),
    'att_t': (0.04, 0.008, 0.1, True),
    'att_soft': (0.4, 0.0, 1.0, False),
}
PRESET = dict(flute=0, alto=1)


def to_unit(k, v):
    s, lo, hi, lg = SPACE[k]; v = min(max(v, lo), hi)
    return math.log(v / lo) / math.log(hi / lo) if lg else (v - lo) / (hi - lo)


def from_unit(k, u):
    s, lo, hi, lg = SPACE[k]; u = min(max(u, 0.0), 1.0)
    return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)


def score(args):
    bt, inst, p, wid = args
    try:
        comp, det, rows = evalm.evaluate(bt, inst, p, os.path.join(ROOT, 'tmp', f'aw{wid}'), 2)
        hard, soft, nh, ns = takes.summary(takes.rises(bt, PRESET[inst], p, f'at{wid}'))
    except Exception as ex:
        return 1e3, {'error': str(ex)[:200]}
    if comp is None:
        return 1e3, {'error': 'non-finite'}
    comp['takes_hard'] = hard; comp['takes_soft'] = soft
    pen = max(0.0, hard - 0.05) / 0.01 + max(0.0, soft - 0.12) / 0.02
    comp['total'] += pen
    return comp['total'], comp


def fmt(c):
    return O.fmt(c) + f" takes hard {1000*c.get('takes_hard',0):.0f} soft {1000*c.get('takes_soft',0):.0f} ms"


def main():
    a = sys.argv[1:]
    bt, inst, run = a[0], a[1], a[2]
    gens, pop, sigma, workers, start = 60, 16, 0.25, 5, None
    i = 3
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
    es = cma.CMAEvolutionStrategy([to_unit(k, base[k]) for k in keys], sigma, {'bounds': [0, 1], 'popsize': pop, 'verbose': -9, 'seed': 5})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=workers) as ex:
        f0, c0 = score((bt, inst, base, 0))
        best = (f0, dict(base), c0)
        print(f"start {f0:.3f} | {fmt(c0) if 'error' not in c0 else c0}", file=log, flush=True)
        for g in range(gens):
            X = es.ask()
            cands = [{k: round(from_unit(k, u), 6) for k, u in zip(keys, x)} for x in X]
            res = list(ex.map(score, [(bt, inst, p, j) for j, p in enumerate(cands)]))
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
