# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Write fitted parameters into one of the flute's preset files (the factory presets are files).
  python -I bake.py <presets/flute/NN-name.preset.json> <best.json> [gain]
Every fitted parameter (by its live name) gets its value at %.6g, the value the fit rendered; the
other lines of the file are kept. Then: cmake -P tools/presets_inc.cmake (the files compiled in)."""
import json, re, sys
path, bj = sys.argv[1:3]
params = json.load(open(bj))['params']
if len(sys.argv) > 3:
    params['gain'] = float(sys.argv[3])
text = open(path, encoding='utf-8').read()
style = 'g6'
for k, v in params.items():
    t = repr(float(v)) if style == 'repr' else f'{float(v):.6g}'
    text, n = re.subn(r'(\n    "' + re.escape(k) + r'": )[^,\n]+', lambda m: m.group(1) + t, text)
    if n != 1:
        sys.exit(f'{path}: no parameter {k}')
open(path, 'w', encoding='utf-8', newline='\n').write(text)
print('baked')
