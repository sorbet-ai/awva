# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""pernote.py <bintag> <preset> [best.json|-] [k=v ...]: per note x dyn, the model vs the recording."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit, model, objective
a = sys.argv[1:]
bintag, preset = a[0], int(a[1])
params = json.load(open(a[2]))['params'] if len(a) > 2 and a[2].endswith('.json') else {}
for kv in a[3:]:
    k, v = kv.split('='); params[k] = float(v)
keys, ref = fit.grid(preset, 'fit')
X = model.render(bintag, preset, params, [(f'n{n}{d}', n, d) for n, d in keys], os.path.join(model.ROOT, 'tmp', 'pn'))
pairs = [(model.features(X[f'n{n}{d}'], n, d), ref[(n, d)]) for n, d in keys]
g = objective.compare(pairs)['_g']
for (n, d), (M, R) in zip(keys, pairs):
    print(f"{n} {d} dl {M['lvl']-R['lvl']-g:+5.1f} hs {M['hshare']:.2f} c {M['cents']:+4.0f} spec {objective.harm_dist(M['harm'],R['harm'],R['hfloor']):4.1f} rise {M['rise']:4.0f}/{R['rise']:4.0f} acent {M['a_cent']:+.1f}/{R['a_cent']:+.1f} | M {' '.join(f'{v:4.0f}' for v in M['harm'][:6])} | R {' '.join(f'{v:4.0f}' for v in R['harm'][:6])}")
