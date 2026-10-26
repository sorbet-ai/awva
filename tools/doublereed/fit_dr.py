# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""CMA-ES of a double reed's player and instrument on its recordings (Iowa MIS: pp/mf/ff), the
Track B way: the sustain (harmonics, noise, level across notes and dynamics, the timbre's response to
the dynamic), the attack (its spectra, the tone's rise from the key, its pitch excursion), the tone's
life (its pitch and level wobble re the recording), pitch and register; plus the takes' rise from
the key (hard strikes <= 50 ms, soft <= 120 ms), the in-range robustness at three pressures, and a
dynamic that keeps growing to the firmest press (key 0.4 -> 1.0 at least 4 dB, 0.7 -> 1.0 at least
1.5 dB, 0.06 -> 1.0 at least 12 dB). ADEV_W sets the attack excursion's weight.
  python -I fit_dr.py <bintag> <inst> <run> [--gens N] [--pop N] [--sigma s] [--workers N] [--start json] [--seed n] [--every n]
"""
import json
import math
import os
import subprocess
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
import robust  # noqa: E402
import takes  # noqa: E402
O.WEIGHT['lvl'] = 3.0

# name: (lo, hi, log); the start is the preset's (replay --list) or --start
SPACE = {
    'pres_lin': (0.0, 1.0, False), 'pres_sq': (0.0, 1.5, False), 'pres_hz': (6.0, 80.0, True),
    'p_soft': (0.9, 2.0, False), 'p_loud': (0.15, 2.0, True), 'p_reg': (-1.0, 1.0, False), 'p_curve': (0.3, 3.0, True),
    'reed_hz': (300.0, 8000.0, True), 'reed_reg': (-1.0, 1.5, False), 'reed_q': (0.3, 20.0, True),
    'reed_open': (0.05, 1.5, True), 'open_reg': (-1.0, 1.5, False), 'reed_w': (3.0, 20.0, True), 'p_close': (1.0, 40.0, True),
    'contact': (0.0, 20.0, False), 'duct': (0.1, 5.0, True), 'emb_soft': (0.1, 1.0, False), 'jitter': (0.0, 20.0, False),
    'wander': (0.0, 0.1, False), 'lip_wander': (0.0, 0.1, False), 'wander_hz': (0.5, 8.0, True),
    'apex': (2.0, 70.0, True), 'cavity': (0.0, 2.0, False), 'bore_d': (1.5, 10.0, True), 'bore_decay': (0.005, 0.3, True),
    'bore_reg': (-1.0, 2.0, False), 'lattice_hz': (150.0, 6000.0, True), 'lattice_q': (0.4, 3.0, True), 'lattice_reg': (-1.0, 1.5, False),
    'rad_hz': (20.0, 1500.0, True), 'tone_hz': (800.0, 16000.0, True), 'tone_q': (0.5, 2.0, False),
    'breath': (0.0, 0.1, False), 't_noise': (0.0, 1.0, False), 'tongue': (0.2, 2.0, False), 'acc_level': (0.0, 1.5, False),
    'acc_decay': (0.008, 0.06, True), 'tongue_t': (0.0008, 0.03, True), 'tongue_shut': (0.0, 1.2, False), 'pitch_t': (0.004, 0.3, True),
}
FIXED = {'hiss': 0.0}  # no added breath
NOTES = dict(oboe=list(range(58, 92, 3)), bassoon=list(range(34, 53)) + list(range(55, 75, 3)))
if os.environ.get('ADEV_W'):
    O.WEIGHT['adev'] = float(os.environ['ADEV_W'])
PRESET = dict(oboe=0, bassoon=2)


def to_unit(k, v):
    lo, hi, lg = SPACE[k]
    v = min(max(v, lo), hi)
    return math.log(v / lo) / math.log(hi / lo) if lg else (v - lo) / (hi - lo)


def from_unit(k, u):
    lo, hi, lg = SPACE[k]
    u = min(max(u, 0.0), 1.0)
    return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)


def score(args):
    bt, inst, p, wid, every = args
    p = dict(p, **FIXED)
    try:
        comp, det, rows = evalm.evaluate(bt, inst, p, os.path.join(ROOT, 'tmp', f'dw{wid}'), every)
        if comp is None:
            return 1e3, {'error': 'non-finite'}
        h, s, nh, ns = takes.summary(takes.rises(bt, PRESET[inst], p, f'dt{wid}'))
        res = robust.check(bt, PRESET[inst], p, NOTES[inst], pres=(0.06, 0.4, 0.7, 1.0), tag=f'dr{wid}')
    except Exception as ex:
        return 1e3, {'error': str(ex)[:200]}
    bf = sum(r[2] for r in res) / len(res)
    L = {(r[0], r[1]): r[3] for r in res}
    grow = float(np.median([L[(n, 1.0)] - L[(n, 0.4)] for n in NOTES[inst]]))
    rng = float(np.median([L[(n, 1.0)] - L[(n, 0.06)] for n in NOTES[inst]]))
    top = float(np.median([L[(n, 1.0)] - L[(n, 0.7)] for n in NOTES[inst]]))
    comp['top'] = top
    comp['total'] += 2.0 * max(0.0, 1.5 - top)
    comp['takes_hard'], comp['takes_soft'], comp['robust_bad'], comp['grow'], comp['range'] = h, s, bf, grow, rng
    comp['total'] += max(0.0, h - 0.05) / 0.01 + max(0.0, s - 0.12) / 0.02 + 3.0 * bf / 0.05 + max(0.0, 4.0 - grow) + max(0.0, 12.0 - rng)
    return comp['total'], comp


def fmt(c):
    return O.fmt(c) + f" takes {1000*c.get('takes_hard', 0):.0f}/{1000*c.get('takes_soft', 0):.0f} ms robust_bad {c.get('robust_bad', 0):.3f} grow {c.get('grow', 0):.1f} range {c.get('range', 0):.1f} top {c.get('top', 0):.1f} dB"


def preset_values(bt, preset):
    out = subprocess.run([replay(bt), '--engine', 'doublereed', '--preset', str(preset), '--list'],
                         capture_output=True, text=True).stdout
    return {ln.split()[0]: float(ln.split()[-1]) for ln in out.splitlines() if ln.strip()}


def main():
    a = sys.argv[1:]
    bt, inst, run = a[0], a[1], a[2]
    gens, pop, sigma, workers, start, seed, every = 80, 16, 0.15, 5, None, 7, 2
    i = 3
    while i < len(a):
        k, v = a[i], a[i + 1]
        if k == '--gens': gens = int(v)
        elif k == '--pop': pop = int(v)
        elif k == '--sigma': sigma = float(v)
        elif k == '--workers': workers = int(v)
        elif k == '--start': start = v
        elif k == '--seed': seed = int(v)
        elif k == '--every': every = int(v)
        i += 2
    import cma
    base = {k: v for k, v in preset_values(bt, PRESET[inst]).items() if k in SPACE}
    if start:
        base.update({k: v for k, v in json.load(open(start))['params'].items() if k in SPACE})
    keys = list(SPACE)
    outd = os.path.join(ROOT, 'fits', run)
    os.makedirs(outd, exist_ok=True)
    log = open(os.path.join(outd, 'log.txt'), 'a')
    es = cma.CMAEvolutionStrategy([to_unit(k, base[k]) for k in keys], sigma, {'bounds': [0, 1], 'popsize': pop, 'verbose': -9, 'seed': seed})
    t0 = time.time()
    with ProcessPoolExecutor(max_workers=workers) as ex:
        f0, c0 = score((bt, inst, base, 0, every))
        best = (f0, dict(base), c0)
        json.dump({'total': best[0], 'params': dict(best[1], **FIXED), 'comps': best[2]}, open(os.path.join(outd, 'best.json'), 'w'), indent=1)
        print(f"start {f0:.3f} | {fmt(c0) if 'error' not in c0 else c0}", file=log, flush=True)
        for g in range(gens):
            X = es.ask()
            cands = [{k: round(from_unit(k, u), 6) for k, u in zip(keys, x)} for x in X]
            res = list(ex.map(score, [(bt, inst, p, j, every) for j, p in enumerate(cands)]))
            fs = [r[0] for r in res]
            es.tell(X, fs)
            j = int(np.argmin(fs))
            if fs[j] < best[0]:
                best = (fs[j], cands[j], res[j][1])
                json.dump({'total': best[0], 'params': dict(best[1], **FIXED), 'comps': best[2]}, open(os.path.join(outd, 'best.json'), 'w'), indent=1)
            print(f"gen {g} {time.time()-t0:.0f}s min {fs[j]:.3f} best {best[0]:.3f} sigma {es.sigma:.3f} | "
                  f"{fmt(res[j][1]) if 'error' not in res[j][1] else res[j][1]['error']}", file=log, flush=True)
    print('done', best[0], file=log, flush=True)


if __name__ == '__main__':
    main()
