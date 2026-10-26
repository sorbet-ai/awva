# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""python -m awvafit <command> ...   (from tools/fit, or with tools/fit on PYTHONPATH)

  params <engine> [preset] [name=value ...]     the live parameters (awva_render --params)
  phrases <dir>                                  the standard expression phrases as key streams
  sources list | fetch SET.. | import DIR [SET..] | verify [SET..]
  segment <file> [--method plateau|runs|envelope|peak|single]
  measure <file> --note N [--method M]           the standard measures of every note of a recording
  loudness <file>...                             integrated LUFS (BS.1770-4)
  robust <engine> <preset> <lo> <hi> [--pressures 0.12,0.4,0.9] [name=value ...]
  pack <config.json> <out dir> --renderer NAME=PATH ...
  stimuli <out.csv> [--notes 62,67,..] [--soft]   test notes from the recorded takes (or the soft-strike set)
  backtest [--metrics a,b] [--require a,b] [--json f]   every metric on the listener verdict pairs
  brassfamily | sax | flute | clarinet <command> ...     an instrument's steps (their docstrings)
"""
import argparse
import json
import os
import sys

import numpy as np

from . import env


def kv(items):
    out = {}
    for a in items:
        k, v = a.split('=', 1)
        out[k] = float(v)
    return out


def load_audio(path):
    from .audio import decode_ffmpeg, read
    if path.lower().endswith(('.aif', '.aiff', '.wav')):
        return read(path, np.float64)
    if path.lower().endswith('.f32'):
        return np.fromfile(path, np.float32).astype(np.float64), 48000
    return decode_ffmpeg(path, 48000).astype(np.float64), 48000


def cmd_sources(a):
    from . import sources
    if a.action == 'list':
        for k, s in sources.SOURCES.items():
            n = len([e for e in sources.registry() if e['source'] == k])
            print(f"{k:13s} {'permissive' if s['permissive'] else 'REFUSED   '} {n:4d} files  {s['title']}: {s['licence']}")
        sets = sorted({x for e in sources.registry() for x in e['sets']})
        print('sets:', ', '.join(f"{x} ({len(sources.files(x))})" for x in sets))
    elif a.action == 'fetch':
        print(sources.fetch(a.args), 'files fetched')
    elif a.action == 'import':
        sources.import_local(a.args[0], a.args[1:])
    elif a.action == 'verify':
        bad = sources.verify(a.args)
        for e in bad:
            print('missing or different:', e['source'], e['path'])
        sys.exit(1 if bad else 0)


def cmd_segment(a):
    from .segment import notes
    x, sr = load_audio(a.file)
    for n in notes(x, sr, a.method):
        print(f'onset {n.onset:8.3f} s  off {n.off:8.3f} s  ({n.off - n.onset:.2f} s)')


def cmd_measure(a):
    from .features.measures import note_measures
    from .labels import parse_iowa
    from .segment import notes
    x, sr = load_audio(a.file)
    lab = parse_iowa(os.path.basename(a.file))
    seq = lab['notes'] if lab and a.note is None else [a.note]
    for k, n in enumerate(notes(x, sr, a.method)):
        note = seq[k] if k < len(seq) else seq[-1]
        m = note_measures(x, sr, note, n.onset, n.off)
        print(f"note {note} lvl {m['lvl']:6.1f} dB cents {m['cents']:+6.1f} settle {m['settle']:.3f} s hnr {m['hnr']:5.1f} dB "
              f"bright {m['bright']:6.1f} dB tilt {m['tilt']:6.1f} dB/oct rise {m['rise']:.3f} s over {m['over']:+.1f} dB")


def cmd_robust(a):
    from .robust import check
    rep = check(a.engine, a.preset, list(range(a.lo, a.hi + 1)), tuple(float(v) for v in a.pressures.split(',')), kv(a.set))
    for n, P, why in rep.bad:
        print(f'  {n} at {P}: {why}')
    print(f'{len(rep.bad)} of {len(rep.plays)} plays bad ({rep.fraction:.3f})')
    sys.exit(1 if rep.bad else 0)


def cmd_brassfamily(a, rest):
    from .instruments import brassfamily as bf
    p = argparse.ArgumentParser(prog='awvafit brassfamily')
    p.add_argument('step', choices=['refs', 'score', 'finish', 'fit', 'pitch', 'bake'])
    p.add_argument('args', nargs='*')
    p.add_argument('--preset', nargs='?', const=-1, type=int)
    p.add_argument('--set', dest='which', default='fit')
    p.add_argument('--gens', type=int, default=40)
    p.add_argument('--pop', type=int, default=24)
    p.add_argument('--sigma', type=float, default=0.15)
    p.add_argument('--workers', type=int, default=5)
    p.add_argument('--start')
    q = p.parse_args(rest)
    if q.step == 'refs':
        bf.refs(q.args or None)
    elif q.step == 'score':
        inst = q.args[0]
        res, comb = bf.score(inst, None if q.preset in (None, -1) else q.preset, kv(q.args[1:]), q.which)
        obj = bf.objective(inst)
        for n, c in res.items():
            print(f"  {n:28s} J={obj.total(c):6.2f} g={c.get('_g', 0):+.1f} notes={c.get('_notes', 0)} | {obj.fmt(c)}")
        print(f"TOTAL {comb['total']:.3f} | {obj.fmt(comb)}")
    elif q.step == 'finish':
        inst = q.args[0]
        r = bf.finish(inst, None if q.preset is not None else q.args[1])
        print(f"{inst}: fit {r['fit_total']:.3f} | hold-out {r['hold_total']:.3f}")
    elif q.step == 'fit':
        print(bf.fit(q.args[0], q.args[1], q.gens, q.pop, q.sigma, q.workers, q.start)[0])
    elif q.step == 'pitch':
        m, h = bf.pitch(q.args[0], q.which)
        em = np.abs(m[:, 1])
        print(f"{q.args[0]}: {len(m)} notes, sustain |pitch| mean {em.mean():.2f} c, 90% {np.quantile(em, 0.9):.2f} c, max {em.max():.2f} c"
              f" | reference mean {np.abs(h[:, 1]).mean():.2f} c")
    elif q.step == 'bake':
        print(bf.bake(q.args[0], q.args[1], q.args[2] if len(q.args) > 2 else None), 'lines baked')


def cmd_sax(a, rest):
    from .instruments import sax
    p = argparse.ArgumentParser(prog='awvafit sax')
    p.add_argument('step', choices=['refs', 'keys', 'score', 'fit', 'chroma', 'levelmatch', 'bake'])
    p.add_argument('args', nargs='*')
    p.add_argument('--gens', type=int, default=100)
    p.add_argument('--pop', type=int, default=16)
    p.add_argument('--sigma', type=float, default=0.12)
    p.add_argument('--workers', type=int, default=5)
    p.add_argument('--start')
    p.add_argument('--chroma', action='store_true')
    q = p.parse_args(rest)
    js = lambda i: json.load(open(q.args[i]))['params'] if len(q.args) > i and q.args[i] != '-' else {}  # noqa: E731
    if q.step == 'refs':
        sax.refs()
    elif q.step == 'keys':
        sax.keys()
    elif q.step == 'score':
        sax.main_score(int(q.args[0]), kv(q.args[1:]))
    elif q.step == 'fit':
        best, r = sax.fit(int(q.args[0]), q.args[1], q.gens, q.pop, q.sigma, q.workers, q.start, q.chroma)
        print('best', best[0], '| hold-out', sax.OBJECTIVE.fmt(r['valid']) if 'valid' in r else '-')
    elif q.step == 'chroma':
        sax.chroma(env.renderer(), int(q.args[0]), js(1), log=print)
    elif q.step == 'levelmatch':
        ls, lw, g = sax.levelmatch(int(q.args[0]), js(1))
        print(f'preset {q.args[0]}: sax {ls:.1f} dB, wind {lw:.1f} dB -> gain {g:.4f}')
    elif q.step == 'bake':
        ent = {}
        for e in q.args[1:]:
            pr, rest_ = e.split('=', 1)
            bj, g = rest_.rsplit(':', 1)
            ent[int(pr)] = (bj, float(g))
        sax.bake(q.args[0], ent)


def cmd_flute(a, rest):
    from .instruments import flute
    p = argparse.ArgumentParser(prog='awvafit flute')
    p.add_argument('step', choices=['refs', 'score', 'fit', 'robust', 'rises'])
    p.add_argument('args', nargs='*')
    p.add_argument('--every', type=int, default=1)
    p.add_argument('--gens', type=int, default=200)
    p.add_argument('--pop', type=int, default=20)
    p.add_argument('--sigma', type=float, default=0.2)
    p.add_argument('--workers', type=int, default=5)
    p.add_argument('--start')
    q = p.parse_args(rest)
    if q.step == 'refs':
        flute.refs()
    elif q.step == 'score':
        comp, _, rows = flute.evaluate(env.renderer(), q.args[0], kv(q.args[1:]), flute.workdir('tmp', 'eval'), q.every)
        print(f"TOTAL {comp['total']:.3f} | {flute.objective(q.args[0]).fmt(comp)} off={comp['_off']:.1f}")
    elif q.step == 'fit':
        print(flute.fit(q.args[0], q.args[1], q.gens, q.pop, q.sigma, q.workers, 2 if q.every == 1 else q.every, q.start)[0])
    elif q.step == 'robust':
        pr = int(q.args[0])
        params = json.load(open(q.args[1]))['params'] if len(q.args) > 1 else {}
        bf, bad = flute.robust(env.renderer(), pr, params, flute.ROBUST_NOTES.get(pr, list(range(60, 96, 2))))
        print(f'bad fraction {bf:.3f}', bad)
    elif q.step == 'rises':
        pr = int(q.args[0])
        params = json.load(open(q.args[1]))['params'] if len(q.args) > 1 else {}
        h, s, nh, ns = flute.rises(env.renderer(), pr, params, 'rises')
        print(f'the tone\'s rise: hard strikes {1000 * h:.0f} ms ({nh}), soft {1000 * s:.0f} ms ({ns})')


def cmd_clarinet(a, rest):
    from .instruments import clarinet as C
    p = argparse.ArgumentParser(prog='awvafit clarinet')
    p.add_argument('step', choices=['refs', 'score', 'fit'])
    p.add_argument('args', nargs='*')
    p.add_argument('--gens', type=int, default=100)
    p.add_argument('--pop', type=int, default=20)
    p.add_argument('--sigma', type=float, default=0.2)
    p.add_argument('--workers', type=int, default=3)
    p.add_argument('--start')
    q = p.parse_args(rest)
    if q.step == 'refs':
        C.refs()
    elif q.step == 'score':
        pr = int(q.args[0])
        which = q.args[1] if len(q.args) > 1 and '=' not in q.args[1] else 'fit'
        keys, ref = C.grid(pr, which)
        f, c = C.evaluate(env.renderer(), pr, kv([x for x in q.args[1:] if '=' in x]), keys, ref, 99)
        print(f"J {f:.3f} ({len(keys)} notes) | {C.OBJECTIVE.fmt(c)} g={c['_g']:+.1f}")
    elif q.step == 'fit':
        print(C.fit(int(q.args[0]), q.args[1], q.gens, q.pop, q.sigma, q.workers, q.start)[0])


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    inst = {'brassfamily': cmd_brassfamily, 'sax': cmd_sax, 'flute': cmd_flute, 'clarinet': cmd_clarinet}
    if argv and argv[0] in inst:
        return inst[argv[0]](None, argv[1:])
    p = argparse.ArgumentParser(prog='awvafit', description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest='cmd', required=True)
    s = sub.add_parser('params')
    s.add_argument('engine')
    s.add_argument('preset', nargs='?')
    s.add_argument('set', nargs='*')
    s = sub.add_parser('phrases')
    s.add_argument('dir')
    s = sub.add_parser('sources')
    s.add_argument('action', choices=['list', 'fetch', 'import', 'verify'])
    s.add_argument('args', nargs='*')
    s = sub.add_parser('segment')
    s.add_argument('file')
    s.add_argument('--method', default='peak')
    s = sub.add_parser('measure')
    s.add_argument('file')
    s.add_argument('--note', type=float)
    s.add_argument('--method', default='peak')
    s = sub.add_parser('loudness')
    s.add_argument('files', nargs='+')
    s = sub.add_parser('robust')
    s.add_argument('engine')
    s.add_argument('preset')
    s.add_argument('lo', type=int)
    s.add_argument('hi', type=int)
    s.add_argument('--pressures', default='0.12,0.4,0.9')
    s.add_argument('set', nargs='*')
    s = sub.add_parser('backtest')
    s.add_argument('--metrics')
    s.add_argument('--require')
    s.add_argument('--json')
    s = sub.add_parser('stimuli')
    s.add_argument('out')
    s.add_argument('--notes', default='62,67,72,77')
    s.add_argument('--soft', action='store_true', help='the fixed soft-strike set (0.07, 0.12, 0.2, 0.3 at 0.4)')
    s = sub.add_parser('pack')
    s.add_argument('config')
    s.add_argument('out')
    s.add_argument('--renderer', action='append', default=[])
    a = p.parse_args(argv)
    try:
        if a.cmd == 'params':
            from .render import Renderer
            for k, v in Renderer().params(a.engine, a.preset, kv(a.set)).items():
                print(f'{k} {v.group} {v.lo:g} {v.hi:g} {v.value:g}')
        elif a.cmd == 'phrases':
            from .phrases import write_all
            for k, v in write_all(a.dir).items():
                print(k, v)
        elif a.cmd == 'sources':
            cmd_sources(a)
        elif a.cmd == 'segment':
            cmd_segment(a)
        elif a.cmd == 'measure':
            cmd_measure(a)
        elif a.cmd == 'loudness':
            from .features.loudness import lufs
            for f in a.files:
                x, sr = load_audio(f)
                print(f'{lufs(x, sr):7.2f} LUFS  {f}')
        elif a.cmd == 'robust':
            cmd_robust(a)
        elif a.cmd == 'backtest':
            from .verdicts import run
            res = run(a.metrics.split(',') if a.metrics else None)
            if a.json:
                with open(a.json, 'w', encoding='utf-8') as f:
                    json.dump(res, f, indent=1, default=float)
            if a.require:
                bad = [m for m in a.require.split(',') if not res['gate'].get(m, {}).get('passes')]
                if bad:
                    env.die('the back-test fails for ' + ', '.join(bad))
        elif a.cmd == 'stimuli':
            from . import stimuli
            notes = [int(n) for n in a.notes.split(',')]
            if a.soft:
                st, sched = stimuli.strike_set(notes)
            else:
                d = stimuli.playing()
                print({k: {q: round(float(v), 3) for q, v in qs.items()} for k, qs in d.held().quantiles().items()})
                st, sched = stimuli.note_set(notes, d)
            os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
            st.write(a.out, notes=False)
            with open(a.out.replace('.csv', '.notes.json'), 'w', encoding='utf-8') as f:
                json.dump(sched, f, indent=0)
            print(len(sched), 'notes ->', a.out)
        elif a.cmd == 'pack':
            from .pack import build
            build(a.config, a.out, dict(r.split('=', 1) for r in a.renderer))
    except env.EnvError as ex:
        env.die(str(ex))


if __name__ == '__main__':
    main()
