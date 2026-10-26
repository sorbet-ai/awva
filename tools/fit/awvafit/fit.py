# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The fit driver: CMA-ES (the cma package, BSD-3) over an engine's live parameters, in a normalised
space, with checkpoints, a log, the best so far, and hold-out scoring at the end.

  space = Space({'gain': (1.0, 300.0, True), 'pres_lin': (0.0, 1.0, False), ...})   (lo, hi, log)
  space.clip_to(renderer.params(engine, preset))      the engine's own ranges narrow it
  run = Fit(outdir, space, score, x0_params, sigma=0.12, pop=24, seed=1, workers=5)
  best = run.run(gens=40)                              (resumes from outdir/checkpoint.pkl)

score(params, worker_id) -> (total, components) runs in a worker process; it must be a module-level
function (picklable). A candidate that raises scores 1e3. Every generation: log.txt gets a line, the
checkpoint (the CMA-ES state, the best, the generation) is rewritten, and best.json when it improves.
Normalised space: u in [0, 1] per parameter, log-spaced for log parameters; values are rounded to 6
decimals before rendering (what the fit saw is what the preset gets). The seed makes a run
repeatable on one machine; the worker count does not change the result (scores are per candidate).
"""
import json
import math
import os
import pickle
import time
from concurrent.futures import ProcessPoolExecutor

import numpy as np


class Space:
    def __init__(self, bounds):
        """bounds: {name: (lo, hi, log)} (a 4-tuple (start, lo, hi, log) is read as the same)"""
        self.b = {}
        for k, v in bounds.items():
            lo, hi, lg = v[-3:]
            self.b[k] = (float(lo), float(hi), bool(lg))

    @property
    def names(self):
        return list(self.b)

    def clip_to(self, params):
        """narrow each range to the engine's (render.Renderer.params): a log range stays positive"""
        for k, (lo, hi, lg) in list(self.b.items()):
            p = params[k]
            self.b[k] = (max(lo, p.lo, 1e-6 if lg else -1e30), min(hi, p.hi), lg)
        return self

    def to_unit(self, k, v):
        lo, hi, lg = self.b[k]
        v = min(max(v, lo), hi)
        return (math.log(v / lo) / math.log(hi / lo)) if lg else (v - lo) / (hi - lo)

    def from_unit(self, k, u):
        lo, hi, lg = self.b[k]
        u = min(max(u, 0.0), 1.0)
        return lo * (hi / lo) ** u if lg else lo + u * (hi - lo)

    def encode(self, params, keys=None):
        return [self.to_unit(k, params[k]) for k in keys or self.names]

    def decode(self, x, base, keys=None):
        p = dict(base)
        for k, u in zip(keys or self.names, x):
            p[k] = round(self.from_unit(k, u), 6)
        return p


def _safe(args):
    score, params, wid = args
    try:
        return score(params, wid)
    except Exception as ex:  # a crash or a guard: very bad
        return 1e3, {'error': str(ex)[:300]}


class Fit:
    def __init__(self, outdir, space, score, base, sigma=0.12, pop=24, seed=1, workers=5, only=None, fmt=None, start_score=True):
        self.outdir, self.space, self.score, self.base = outdir, space, score, dict(base)
        self.sigma, self.pop, self.seed, self.workers = sigma, pop, seed, workers
        self.keys = [k for k in space.names if only is None or k in only]
        self.fmt = fmt or (lambda c: ' '.join(f'{k}={v:.3g}' for k, v in c.items() if isinstance(v, float)))
        self.start_score = start_score
        os.makedirs(outdir, exist_ok=True)

    def _save_best(self, best):
        with open(os.path.join(self.outdir, 'best.json'), 'w') as f:
            json.dump({'total': best[0], 'params': best[1], 'comps': best[2]}, f, indent=1)

    def run(self, gens):
        import cma
        if len(self.keys) < 2:
            raise ValueError('CMA-ES searches two parameters or more (for one, scan it)')
        ck = os.path.join(self.outdir, 'checkpoint.pkl')
        with open(os.path.join(self.outdir, 'log.txt'), 'a', encoding='utf-8') as log:
            return self._run(gens, ck, log)

    def _run(self, gens, ck, log):
        import cma
        g0, best = 0, None
        if os.path.exists(ck):
            with open(ck, 'rb') as f:
                st = pickle.load(f)
            es, g0, best = st['es'], st['gen'], st['best']
            print(f'resume at generation {g0}, best {best[0]:.3f}', file=log, flush=True)
        else:
            x0 = self.space.encode(self.base, self.keys)
            es = cma.CMAEvolutionStrategy(x0, self.sigma, {'bounds': [0, 1], 'popsize': self.pop, 'verbose': -9, 'seed': self.seed})
        t0 = time.time()
        with ProcessPoolExecutor(max_workers=self.workers) as ex:
            if best is None:
                if self.start_score:
                    f0, c0 = _safe((self.score, self.base, 0))
                    best = (f0, dict(self.base), c0)
                    self._save_best(best)
                    print(f"start {f0:.3f} | {self.fmt(c0)}", file=log, flush=True)
                else:
                    best = (1e9, dict(self.base), {})
            for g in range(g0, gens):
                X = es.ask()
                cands = [self.space.decode(x, self.base, self.keys) for x in X]
                res = list(ex.map(_safe, [(self.score, p, j) for j, p in enumerate(cands)]))
                fs = [r[0] for r in res]
                es.tell(X, fs)
                j = int(np.argmin(fs))
                if 'error' in res[j][1]:
                    print(f"gen {g} all candidates failed: {res[j][1]['error']}", file=log, flush=True)
                else:
                    if fs[j] < best[0]:
                        best = (fs[j], cands[j], res[j][1])
                        self._save_best(best)
                    print(f"gen {g} {time.time() - t0:.0f}s min {fs[j]:.3f} med {np.median(fs):.3f} best {best[0]:.3f} sigma {es.sigma:.3f} | "
                          f"{self.fmt(res[j][1])}", file=log, flush=True)
                with open(ck + '.tmp', 'wb') as f:
                    pickle.dump({'es': es, 'gen': g + 1, 'best': best}, f)
                os.replace(ck + '.tmp', ck)
        print('done', best[0], file=log, flush=True)
        return best


def holdout(score_hold, params):
    """the hold-out score of a fit's parameters (score_hold: params -> (total, components))"""
    return score_hold(params)
