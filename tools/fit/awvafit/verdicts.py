# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The back-test: every distance re-scored on the pairs of builds listeners judged by ear
(verdicts.json), so a metric ranks them right before it enters an objective.

  python -m awvafit backtest [--metrics attack,jtfs,...] [--require attack,jtfs] [--json out.json]

For each pair (the approved build, the rejected one, a reference: the recordings or an approved
build, a stimulus set) and each metric: on every note both builds have, is the approved build closer
to the reference? Wins out of the notes, and the effect: (median distance of the rejected - of the
approved) in natural-variation units (the median distance between a recording and the recording one
semitone up, at the same dynamic). The gate: a ranker (verdicts.json "metrics", role "ranker")
passes when the approved build wins on more than half the notes of every pair in its declared use; a
new metric (not listed there) must be right on every pair; secondary, diagnostic and "not a ranker"
metrics are reported, never passed. --require exits 1 unless each named metric passes: run it before
adding a term to an objective, and list a metric there (with its use) once it has passed.

The renders: $AWVA_VERDICTS/<set>/<inst>__<build>.mono.f32 and <set>/<inst>.notes.json. The
recordings: the sources registry ($AWVA_REFS; decoded with ffmpeg at 48 kHz). A sign test: 10 or more
wins of 12 is p <= 0.02 one-sided, taking the notes as independent.
"""
import json
import math
import os

import numpy as np
from .audio import read_render

from . import env, labels, sources
from .audio import decode_ffmpeg
from .features import ranking as R
from .segment import peak

HERE = os.path.dirname(os.path.abspath(__file__))
SR = 48000


def load():
    with open(os.path.join(HERE, 'verdicts.json'), encoding='utf-8') as f:
        return json.load(f)


def data_root():
    v = os.environ.get('AWVA_VERDICTS')
    if not v:
        raise env.EnvError('set AWVA_VERDICTS: the verdict renders (tools/fit/awvafit/verdicts.json says the layout)')
    return v


def render_notes(set_dir, inst, build):
    x = read_render(os.path.join(set_dir, f'{inst}__{build}.mono.f32')).astype(np.float64)
    sched = json.load(open(os.path.join(set_dir, f'{inst}.notes.json')))
    out = {}
    t0 = sched[0]['t_on']  # the audio starts at the first note
    for s in sched:
        a = max(int((s['t_on'] - t0 - 0.05) * SR), 0)
        b = int((s['t_on'] - t0 + 0.4) * SR)  # the onset search: up to 0.4 s into the note
        n = R.note(x, R.onset(x, a, b), s['note'], s['dyn'])
        if n:
            out[(n['midi'], n['dyn'])] = n
    return out


def ref_notes(ref, wanted, cache=None):
    """{(midi, dyn): Note} of a reference's recordings (the notes' f0 within 50 cents), with each
    file's HF floor: {(midi, dyn): floor dB re the note}"""
    d = sources.path(ref['source'], ref['dir'])
    out, floors = {}, {}
    for f in sorted(os.listdir(d)):
        lab = labels.parse_iowa(f)
        if lab is None or lab['vib'] or lab['dyn'] not in ('pp', 'mf', 'ff'):
            continue
        x = decode_ffmpeg(os.path.join(d, f), SR, cache=os.path.join(cache, f + '.f32') if cache else None).astype(np.float64)
        segs = peak(x, SR, min_s=0.25, db=-24)
        fl = R.noise_floor(x, segs)
        for j, (a, b) in enumerate(segs):
            mm = lab['lo'] + j
            if mm not in wanted or (mm, lab['dyn']) in out:
                continue
            on = R.onset(x, max(a - int(0.1 * SR), 0), min(b, a + int(0.5 * SR)))
            n = R.note(x, on, mm, lab['dyn'])
            if n and abs(n['cents']) < 50:
                out[(mm, lab['dyn'])] = n
                if fl is not None:
                    s = x[on + int(R.SUS0 * SR):on + int(R.SUS1 * SR)]
                    floors[(mm, lab['dyn'])] = fl - 10 * np.log10(np.mean(s ** 2) + 1e-20)
    return out, floors


def sign_p(wins, n):
    """one-sided sign test: P(at least `wins` of n by chance)"""
    return sum(math.comb(n, k) for k in range(wins, n + 1)) / 2 ** n


def run(metrics=None, log=print, V=None, root=None):
    V = V or load()
    root = root or data_root()
    metrics = metrics or [m for m in R.METRICS if m != 'jtfs' or R.jtfs_available()]
    cache = env.workdir('verdicts', 'dec') if os.environ.get('AWVA_FIT_WORK') else None
    refs, floors, nat, renders = {}, {}, {}, {}
    out = dict(pairs=[], gate={}, noise={})
    for p in V['pairs']:
        inst = p['inst']
        if inst not in refs:
            rc = V['references'].get(inst)
            refs[inst], floors[inst] = ref_notes(rc, set(rc['notes']) | {m + 1 for m in rc['notes']}, cache) if rc else ({}, {})
            rc = rc or {'notes': []}
            nat[inst] = {}
            for m in metrics:
                v = [R.METRICS[m](refs[inst][(mm + 1, d)], refs[inst][(mm, d)]) for (mm, d) in refs[inst]
                     if mm in rc['notes'] and (mm + 1, d) in refs[inst]]
                nat[inst][m] = float(np.median(v)) if v else float('nan')
        for b in (p['better'], p['worse']) + ((p['ref'],) if p['ref'] != 'rec' else ()):
            key = (p['set'], inst, b)
            if key not in renders:
                renders[key] = render_notes(os.path.join(root, p['set']), inst, b)
        G, W = renders[(p['set'], inst, p['better'])], renders[(p['set'], inst, p['worse'])]
        keys = sorted(k for k in G if k in W and (not p.get('dyns') or k[1] in p['dyns']))
        if p['ref'] == 'rec':
            ref = {k: refs[inst].get((k[0], p.get('ref_dyn', k[1]))) for k in keys}
        else:
            H = renders[(p['set'], inst, p['ref'])]
            ref = {k: H.get(k) for k in keys}
        keys = [k for k in keys if ref[k] is not None]
        row = dict(pair=f"{inst}: {p['better']} > {p['worse']}", set=p['set'], ref=p['ref'], notes=len(keys), metrics={})
        for m in metrics:
            dg = np.array([R.METRICS[m](G[k], ref[k]) for k in keys])
            dw = np.array([R.METRICS[m](W[k], ref[k]) for k in keys])
            wins = int(np.sum(dg < dw))
            eff = float((np.median(dw) - np.median(dg)) / nat[inst][m]) if nat[inst][m] > 0 else float('nan')
            row['metrics'][m] = dict(wins=wins, effect=eff, p=sign_p(wins, len(keys)))
        out['pairs'].append(row)
        log(f"{row['pair']:28s} {p['set']:4s} vs {p['ref']:6s} " + ' '.join(
            f"{m} {row['metrics'][m]['wins']:2d}/{len(keys)} ({row['metrics'][m]['effect']:+.2f})" for m in metrics))
    for m in metrics:
        spec = V['metrics'].get(m, {})
        use, role = spec.get('use', []), spec.get('role', 'new')
        rows = [r for r in out['pairs'] if f"{r['set']}/{r['ref']}" in use]
        on_use = bool(rows) and all(r['metrics'][m]['wins'] * 2 > r['notes'] for r in rows)
        right = sum(r['metrics'][m]['wins'] * 2 > r['notes'] for r in out['pairs'])
        # a ranker passes on its declared use; a new metric (not in verdicts.json) must be right on every pair
        ok = on_use if role == 'ranker' else (right == len(out['pairs']) if role == 'new' else False)
        out['gate'][m] = dict(passes=ok, role=role, use=use, right=right, pairs=len(out['pairs']), why=spec.get('why', ''))
        state = 'PASSES' if ok else ('fails ' if role in ('ranker', 'new') else 'n/a   ')
        log(f"gate {m:9s} {state} ({role}; use {use or '-'}): right on {right} of {len(out['pairs'])} pairs - {spec.get('why', '')}")
    # the noise per period: a diagnostic per dynamic, the recording's floor shown with it
    for inst in refs:
        for (mm, d), n in sorted(refs[inst].items()):
            if mm in V['references'].get(inst, {}).get('notes', []):
                nh, psm, _ = R.cached(n, 'psn', R.noise_per_period)
                out['noise'].setdefault(f'{inst}/rec', []).append(dict(note=mm, dyn=d, nhr_hp=nh, psm=psm, floor=floors[inst].get((mm, d))))
    for (st, inst, b), notes in renders.items():
        if st == 'main':
            for (mm, d), n in sorted(notes.items()):
                nh, psm, _ = R.cached(n, 'psn', R.noise_per_period)
                out['noise'].setdefault(f'{inst}/{b}', []).append(dict(note=mm, dyn=d, nhr_hp=nh, psm=psm))
    log('noise per period (median nhr_hp dB / psm; the recordings\' HF floor re the note): a diagnostic, not in any score')
    for k, rows in out['noise'].items():
        cells = []
        for d in ('pp', 'mf', 'ff'):
            r = [x for x in rows if x['dyn'] == d]
            if r:
                fl = [x['floor'] for x in r if x.get('floor') is not None]
                cells.append(f"{d} {np.median([x['nhr_hp'] for x in r]):6.1f}/{np.median([x['psm'] for x in r]):.2f}" +
                             (f" (floor {np.median(fl):5.1f})" if fl else ''))
        log(f'  {k:18s} ' + ' | '.join(cells))
    return out
