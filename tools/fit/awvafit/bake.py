# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Fitted parameters into an engine's preset tables (the engine's header in dsp/include/awva).

A fit works in live parameter names (awva_render --set). The header maps them to Params fields in
its parameter table, entries of the form {{"name", "group", ...}, &Params::field}; param_fields()
reads that map. Values are written as float literals of the value the fit rendered (repr, or %.6g
where the lanes used it), so the baked preset renders the fit's sound.

  bake_case(header, k, params, base_header, base_func)   a `case k:` block in a preset switch: every
        value that differs from the base preset function's (the trumpet's), other lines kept
  bake_assignments(header, func, params)                  the assignments in one preset function:
        those present are replaced, the others appended before `return p;`
  write_switch(header, func_sig, cases, names, default)   a whole preset switch, every parameter
"""
import re

FIELD_RE = r'\{\{"(\w+)", "\w+", [^}]*\}, &Params::(\w+)\}'


def param_fields(text):
    """{live parameter name: Params field} from a header's parameter table"""
    return dict(re.findall(FIELD_RE, text))


def literal(v, style='repr'):
    """a C++ float literal: 'repr' (the shortest round trip) or 'g6' (%.6g)"""
    t = repr(float(v)) if style == 'repr' else f'{float(v):.6g}'
    if 'e' not in t and '.' not in t and 'inf' not in t and 'nan' not in t:
        t += '.0'
    return t + 'f'


def function_values(text, func):
    """{field: value} of the `p.field = value;` lines of a function `... func() {` up to `return p;`"""
    body = text[text.index(f'{func}() {{'):]
    body = body[:body.index('return p;')]
    return {m: float(v.rstrip('f')) for m, v in re.findall(r'\tp\.(\w+) = ([^;]+);', body)}


def bake_case(header, k, params, base_header, base_func, comment=None):
    """params into `case k:` of header's switch; returns the number of lines written"""
    vt = open(base_header, encoding='utf-8').read()
    table = param_fields(vt)
    base = function_values(vt, base_func)
    s = open(header, encoding='utf-8').read()
    m = re.search(r'(\tcase %d: //[^\n]*\n)(.*?)(\t\tbreak;\n)' % k, s, re.S)
    if not m:
        raise ValueError(f'no case {k} in {header}')
    fitted = {table[n]: float(v) for n, v in params.items()}
    keep = [ln for ln in m.group(2).splitlines(keepends=True)
            if not re.match(r'\t\tp\.(\w+) = ', ln) or re.match(r'\t\tp\.(\w+) = ', ln).group(1) not in fitted]
    lines = [f'\t\tp.{mem} = {literal(v)};\n' for mem, v in fitted.items() if v != base.get(mem)]
    head = f'\tcase {k}: // {comment}\n' if comment else m.group(1)
    s = s[:m.start()] + head + ''.join(lines) + ''.join(keep) + m.group(3) + s[m.end():]
    open(header, 'w', encoding='utf-8', newline='\n').write(s)
    return len(lines)


def bake_assignments(header, func, params, style='g6', base_line=None):
    """params into the `p.field = ...;` lines of `func() {`: present ones replaced, the rest appended
    before `return p;`; returns (replaced, appended)"""
    s = open(header, encoding='utf-8').read()
    table = param_fields(s)
    a = s.index(f'{func}() {{')
    b = s.index('\treturn p;\n}', a)
    blk = s[a:b]
    rep = add = 0
    tail = ''
    for k, v in params.items():
        f = table.get(k, k)
        blk, n = re.subn(rf"\tp\.{f} = [^;]*;", f"\tp.{f} = {literal(v, style)};", blk)
        if n:
            rep += 1
        else:
            tail += f"\tp.{f} = {literal(v, style)};\n"
            add += 1
    s = s[:a] + blk + tail + s[b:]
    open(header, 'w', encoding='utf-8', newline='\n').write(s)
    return rep, add


def write_switch(header, func_sig, cases, names, default=None, style='g6'):
    """a whole `switch (i) {` of func_sig: cases {k: {field: value}} (ordered), names {k: comment};
    `default:` goes before case `default`"""
    s = open(header, encoding='utf-8').read()
    body = []
    for k, fields in cases.items():
        if default is not None and k == default:
            body.append('\tdefault:')
        body.append(f'\tcase {k}: // {names[k]}')
        body += [f'\t\tp.{f} = {literal(v, style)};' for f, v in fields.items()]
        body.append('\t\tbreak;')
    a = s.index(func_sig)
    b0 = s.index('\tswitch (i) {\n', a) + len('\tswitch (i) {\n')
    b1 = s.index('\t}\n\treturn p;\n}', b0)
    s = s[:b0] + '\n'.join(body) + '\n' + s[b1:]
    open(header, 'w', encoding='utf-8', newline='\n').write(s)
