# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Fitted parameters into voice_doublereed.h's oboe_params() / bassoon_params() (by the live
parameters' names), and the cor anglais as the oboe's player on its geometry.
  python -I bake_dr.py <voice_doublereed.h> <oboe best.json> <bassoon best.json> <gain oboe> <gain cor> <gain bassoon>"""
import json
import re
import sys

hp, oj, bj, go, gc, gb = sys.argv[1:7]
s = open(hp, encoding='utf-8').read()
def ff(v):
    t = f"{float(v):.6g}"
    return (t if ('.' in t or 'e' in t) else t + '.0') + 'f'


table = dict(re.findall(r'\{\{"(\w+)", "\w+", [^}]*\}, &Params::(\w+)\}', s))


def bake(fn, params, gain):
    global s
    m = re.search(r'(static inline Params ' + fn + r'\(\) \{\n)(.*?)(\n\treturn p;\n\})', s, re.S)
    body = m.group(2)
    vals = {table[k]: v for k, v in params.items()}
    vals['gain'] = float(gain)
    lines = []
    seen = set()
    for ln in body.split('\n'):
        mm = re.match(r'\tp\.(\w+) = .*;$', ln)
        if mm and mm.group(1) in vals:
            lines.append(f"\tp.{mm.group(1)} = {ff(vals[mm.group(1)])};")
            seen.add(mm.group(1))
        else:
            lines.append(ln)
    for k, v in vals.items():
        if k not in seen:
            lines.append(f"\tp.{k} = {ff(v)};")
    s = s[:m.start(2)] + '\n'.join(lines) + s[m.end(2):]


bake('oboe_params', json.load(open(oj))['params'], go)
bake('bassoon_params', json.load(open(bj))['params'], gb)
# the cor anglais: the oboe's player on the cor anglais (in F, a fifth below; a longer, wider cone, a
# larger reed, the bulb's darker lattice): not fitted, no permissively licensed recordings
m = re.search(r'static inline Params cor_anglais_params\(\) \{\n.*?\n\treturn p;\n\}', s, re.S)
new = """static inline Params cor_anglais_params() {
	Params p = oboe_params();
	p.gain = """ + ff(gc) + """;
	p.apex *= 1.5f;       // the cone a fifth longer
	p.bore_d *= 1.15f;    // and wider
	p.reed_hz *= 0.85f;   // a larger reed (the reference note is a fifth lower already: 62 re 69)
	p.reed_open *= 1.15f;
	p.reed_w *= 1.07f;
	p.lat_hz *= 0.75f;    // the bulb: a darker lattice
	return p;
}"""
s = s[:m.start()] + new + s[m.end():]
open(hp, 'w', encoding='utf-8', newline='\n').write(s)
print('baked')
