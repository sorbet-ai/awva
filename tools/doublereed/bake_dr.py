# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Fitted parameters into the double reed's preset files (the factory presets are files:
presets/doublereed/NN-<name>.preset.json), by the live parameters' names, and the cor anglais as
the oboe's player on its geometry.
  python -I bake_dr.py <presets/doublereed> <oboe best.json> <bassoon best.json> <gain oboe> <gain cor> <gain bassoon>
A fitted value is written at %.6g (the value the fit rendered); the cor anglais's scaled ones as the
engine's float arithmetic gives them (float32, the shortest text that reads back the same). The
other lines of the files are kept. Then: cmake -P tools/presets_inc.cmake (the files compiled in)."""
import json
import os
import re
import sys

import numpy as np

d, oj, bj, go, gc, gb = sys.argv[1:7]
OBOE, COR, BASSOON = (os.path.join(d, f) for f in ('00-oboe.preset.json', '01-cor-anglais.preset.json', '02-bassoon.preset.json'))
# the cor anglais: the oboe's player on the cor anglais (in F, a fifth below; a longer, wider cone, a
# larger reed, the bulb's darker lattice): not fitted, no permissively licensed recordings
COR_SCALE = {'apex': 1.5, 'bore_d': 1.15, 'reed_hz': 0.85, 'reed_open': 1.15, 'reed_w': 1.07, 'lattice_hz': 0.75}


def put(text, path, k, t):
    text, n = re.subn(r'(\n    "' + re.escape(k) + r'": )[^,\n]+', lambda m: m.group(1) + t, text)
    if n != 1:
        sys.exit(f'{path}: no parameter {k}')
    return text


def num(v):
    t = f'{float(v):.6g}'
    return t


def f32(v):
    return np.format_float_positional(np.float32(v), unique=True, trim='-')


def bake(path, params, gain):
    text = open(path, encoding='utf-8').read()
    params = dict(params, gain=float(gain))
    for k, v in params.items():
        text = put(text, path, k, num(v))
    open(path, 'w', encoding='utf-8', newline='\n').write(text)
    return text


oboe = bake(OBOE, json.load(open(oj))['params'], go)
bake(BASSOON, json.load(open(bj))['params'], gb)
ov = {k: float(v) for k, v in re.findall(r'\n    "(\w+)": ([^,\n]+)', oboe)}
cor = open(COR, encoding='utf-8').read()
for k, v in ov.items():
    if k == 'ref_note':
        continue
    if k == 'gain':
        t = num(gc)
    elif k in COR_SCALE:
        t = f32(np.float32(v) * np.float32(COR_SCALE[k]))
    else:
        t = f32(v)
    cor = put(cor, COR, k, t)
open(COR, 'w', encoding='utf-8', newline='\n').write(cor)
print('baked')
