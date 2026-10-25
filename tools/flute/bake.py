# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Write fitted parameters into voice_flute.h's preset functions.
  python -I bake.py <voice_flute.h> <flute best.json> <alto best.json> <gain_flute> <gain_alto>
Every field of the flute's Params gets the fitted value (by the live parameter's name, through
the param table), the alto's function its fitted values on top of the flute's."""
import json, re, sys
hp, fj, aj, gf, ga = sys.argv[1:6]
s = open(hp, encoding='utf-8').read()
# live name -> field
table = dict(re.findall(r'\{\{"(\w+)", "\w+", [^}]*\}, &Params::(\w+)\}', s))
def body(params, base=None):
    out = []
    for k, v in params.items():
        f = table[k]
        out.append(f"\tp.{f} = {float(v):.6g}f;")
    return '\n'.join(out)
fp = json.load(open(fj))['params']; fp['gain'] = float(gf)
ap = json.load(open(aj))['params']; ap['gain'] = float(ga)
# flute: replace every assignment in flute_params()
m = re.search(r'(static inline Params flute_params\(\) \{\n\tParams p;\n)(.*?)(\n\treturn p;\n\})', s, re.S)
lines = m.group(2).split('\n')
fields = {table[k]: v for k, v in fp.items()}
new = []
for ln in lines:
    mm = re.match(r'\tp\.(\w+) = .*;$', ln)
    if mm and mm.group(1) in fields:
        new.append(f"\tp.{mm.group(1)} = {float(fields[mm.group(1)]):.6g}f;")
    else:
        new.append(ln)
s = s[:m.start(2)] + '\n'.join(new) + s[m.end(2):]
# alto: the flute's, then the alto's geometry and its fit
m = re.search(r'(static inline Params alto_params\(\) \{\n\tParams p = flute_params\(\);\n)(.*?)(\n\treturn p;\n\})', s, re.S)
keep = [ln for ln in m.group(2).split('\n') if re.match(r'\tp\.(bore_mm|low_note) = ', ln)]
alines = keep + [f"\tp.{table[k]} = {float(v):.6g}f;" for k, v in ap.items()]
s = s[:m.start(2)] + '\n'.join(alines) + s[m.end(2):]
open(hp, 'w', encoding='utf-8', newline='\n').write(s)
print('baked')
