# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""bake.py <presets/clarinet> <bb best.json> <bass best.json> [bb_gain] [bass_gain]

The fitted values into the factory preset files (00-clarinet, 01-bass-clarinet .preset.json):
each fitted parameter's line is rewritten with the float32's shortest decimal (the exporter's,
awva_presets). Then: cmake -P tools/presets_inc.cmake (the engine reads the files at compile time).
"""
import json
import os
import re
import sys

import numpy as np


def shortest(v):
    """the shortest decimal that reads back as the same float32"""
    x = np.float32(v)
    for d in range(1, 10):
        t = f'{float(x):.{d}g}'
        if np.float32(t) == x:
            return t
    return f'{float(x):.9g}'


def bake(path, params):
    text = open(path, encoding='utf-8').read()
    for k, v in params.items():
        text, n = re.subn(rf'("{re.escape(k)}": )[-0-9.eE+]+', lambda m: m.group(1) + shortest(v), text, count=1)
        if n != 1:
            raise SystemExit(f'{path}: no parameter {k}')
    open(path, 'w', encoding='utf-8', newline='\n').write(text)


def main():
    d, fb, fs = sys.argv[1:4]
    gains = [float(g) for g in sys.argv[4:6]]
    for i, (f, name) in enumerate(((fb, '00-clarinet.preset.json'), (fs, '01-bass-clarinet.preset.json'))):
        p = dict(json.load(open(f))['params'])
        if i < len(gains):
            p['gain'] = gains[i]
        bake(os.path.join(d, name), p)
    print('baked')


if __name__ == '__main__':
    main()
