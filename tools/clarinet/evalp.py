# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""evalp.py <bintag> <preset> [best.json|-] [fit|hold] [k=v ...]: the objective once, per component."""
import json, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fit, objective
a = sys.argv[1:]
bintag, preset = a[0], int(a[1])
params = json.load(open(a[2]))['params'] if len(a) > 2 and a[2].endswith('.json') else {}
which = a[3] if len(a) > 3 else 'fit'
for kv in a[4:]:
    k, v = kv.split('='); params[k] = float(v)
keys, ref = fit.grid(preset, which)
t = time.time()
f, c = fit.evaluate(bintag, preset, params, keys, ref, 99)
print(f"J {f:.3f} ({len(keys)} notes, {time.time()-t:.1f}s) | {fit.fmt(c)}")
