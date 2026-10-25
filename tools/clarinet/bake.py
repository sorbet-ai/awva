# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""bake.py <header> <bb best.json> <bass best.json> [bb_gain] [bass_gain]: the fitted values into the presets."""
import json, re, sys
def fl(v):
    t = f'{float(v):.6g}'
    return t if ('.' in t or 'e' in t) else t + '.0'
hdr, fb, fs = sys.argv[1:4]
gains = [float(g) for g in sys.argv[4:6]]
# the live parameter names -> Params fields
src = open(hdr, encoding='utf-8').read()
table = dict(re.findall(r'\{\{"(\w+)", "\w+", [^}]*\}, &Params::(\w+)\}', src))
def body(params, gain, base):
    lines = []
    for k, v in params.items():
        lines.append(f"\tp.{table[k]} = {float(v):.6g}f;")
    if gain is not None:
        lines.append(f"\tp.gain = {gain:.6g}f;")
    return '\n'.join(lines)
pb = json.load(open(fb))['params']; ps = json.load(open(fs))['params']
a = src.index('[[nodiscard]] static inline Params bb_params() {')
b = src.index('\treturn p;\n}', a)
blk = src[a:b]
# Bb: overwrite each assignment present
for k, v in list(pb.items()) + ([('gain', gains[0])] if gains else []):
    f = table.get(k, k)
    blk, n = re.subn(rf"\tp\.{f} = [^;]*;", f"\tp.{f} = {float(v):.6g}f;", blk)
    assert n == 1, (k, n)
src = src[:a] + blk + src[b:]
a = src.index('[[nodiscard]] static inline Params bass_params() {')
b = src.index('\treturn p;\n}', a)
new = '[[nodiscard]] static inline Params bass_params() {\n\tParams p = bb_params();\n\tp.bore_r = 11.5f;\n'
for k, v in list(ps.items()) + ([('gain', gains[1])] if len(gains) > 1 else []):
    if k in pb and abs(float(pb[k]) - float(v)) < 1e-9 and k != 'gain':
        continue
    new += f"\tp.{table.get(k, k)} = {float(v):.6g}f;\n"
src = src[:a] + new + src[b:]
open(hdr, 'w', encoding='utf-8', newline='\n').write(src)
print('baked')
