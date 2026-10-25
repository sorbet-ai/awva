# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""airy.py <bintag> <preset> [best.json|-] [k=v ...]: how airy the onset and the pp are.

Per dynamic (pp, mf; notes across the range), means over the notes:
  sus_noise  noise between the harmonics re the note's power, 0-8 kHz (sustain, dB)
  atk_noise  the same in the attack: 0-60 ms after the key press, re that window's power (dB)
  pre_noise  noise before the reed speaks: power in 0..t_speak re the sustain (dB)
  t_speak    key press -> the level reaches 10 dB below the sustain (ms)
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import model  # noqa: E402
import notefeats as nf  # noqa: E402

a = sys.argv[1:]
bt, pr = a[0], int(a[1])
params = json.load(open(a[2]))['params'] if len(a) > 2 and a[2].endswith('.json') else {}
for kv in a[3:]:
    k, v = kv.split('=')
    params[k] = float(v)
low = 50 if pr == 0 else 37
notes = [low + k for k in (0, 5, 10, 15, 20, 25, 30)]
SR = 48000
X = model.render(bt, pr, params, [(f'n{n}{d}', n, d) for n in notes for d in ('pp', 'mf')], os.path.join(model.ROOT, 'tmp', 'airy'))
for d in ('pp', 'mf'):
    r = {k: [] for k in ('sus_noise', 'atk_noise', 'pre_noise', 't_speak')}
    for n in notes:
        x = X[f'n{n}{d}'].astype(np.float64)
        f0 = 440 * 2 ** ((n - 69) / 12)
        on = int(model.PRE * SR)
        F = model.features(x, n, d)
        r['sus_noise'].append(10 * np.log10(np.sum(10 ** (F['nhr'][:3] / 10))))
        w = x[on:on + int(0.06 * SR)]
        S = nf._spec(w, SR, 1024)
        nb = nf.noise_ratio(S, SR, 1024, F['f0'])
        r['atk_noise'].append(10 * np.log10(np.sum(10 ** (nb[:3] / 10))))
        e, hop = nf.env(x[on:], SR, 2.0)
        sus = 10 ** (F['lvl'] / 20)
        k = int(np.argmax(e >= 0.316 * sus))
        r['t_speak'].append(k * 2.0)
        pre = x[on:on + max(k, 1) * hop]
        r['pre_noise'].append(10 * np.log10(np.mean(pre ** 2) + 1e-20) - F['lvl'])
    print(d, ' '.join(f"{k} {np.mean(v):6.1f}" for k, v in r.items()))
