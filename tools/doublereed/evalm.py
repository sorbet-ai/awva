# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Render the model on the fit set of an instrument and compare it with the recordings.

  python -I evalm.py <bintag> <inst flute|alto> [name=value ...] [--every n] [--tag t] [--notes]
"""
import os
import pickle
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT, replay  # noqa: E402
sys.path.insert(0, HERE)
import ctl  # noqa: E402
import feat  # noqa: E402
import objective as O  # noqa: E402

PRESET = dict(oboe=0, cor=1, bassoon=2)
_REF = None


def ref():
    global _REF
    if _REF is None:
        _REF = pickle.load(open(os.path.join(ROOT, 'ref', 'feats.pkl'), 'rb'))
    return _REF


def fit_set(inst, every=1):
    rows = [r for r in ref().values() if r['inst'] == inst and not r['vib'] and r['dyn'] in O.DYN_P]
    rows.sort(key=lambda r: (r['dyn'], r['note']))
    # every n-th pitch (the same pitches at every dynamic)
    return [r for r in rows if r['note'] % every == 0]


def make_ctl(rows, path):
    gap = 0.45
    c = ctl.Ctl(len(rows) * (O.HOLD + 0.1 + gap) + 0.3)
    t = 0.1
    evs = []
    for r in rows:
        t_end = c.note(0, t, r['note'], O.DYN_P[r['dyn']], O.STRIKE, O.HOLD)
        evs.append((t, t_end - 0.02))
        t = t_end + gap
    c.write(path)
    return evs


def render(bintag, inst, params, outdir, every=1, extra=None):
    os.makedirs(outdir, exist_ok=True)
    rows = fit_set(inst, every)
    cp = os.path.join(ROOT, 'ctl', f'{inst}_e{every}.ctl')
    evp = cp + '.npy'
    if not os.path.exists(evp):
        evs = make_ctl(rows, cp)
        np.save(evp, np.array(evs))
    evs = np.load(evp)
    op = os.path.join(outdir, f'{inst}.f32')
    cmd = [replay(bintag), '--engine', os.environ.get('REPLAY_ENGINE', 'doublereed'), '--preset', str(PRESET[inst])]
    for k, v in params.items():
        cmd += ['--set', f'{k}={v}']
    cmd += ['--job', cp, op]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0 and 'NON-FINITE' not in r.stderr:
        raise RuntimeError(r.stderr + r.stdout)
    x = np.fromfile(op, np.float32)
    return rows, evs, x, r.stderr


def evaluate(bintag, inst, params, outdir, every=1):
    rows, evs, x, err = render(bintag, inst, params, outdir, every)
    if not np.all(np.isfinite(x)) or np.abs(x).max() > 8:
        return None, None, rows
    feats = [feat.note_feats(x, r['note'], t0, t1, from_key=True) for r, (t0, t1) in zip(rows, evs)]
    comp, per = O.combine(list(zip(rows, feats)))
    comp['_guard'] = err.count('guard')
    return comp, list(zip(rows, feats, per)), rows


def main():
    a = sys.argv[1:]
    bintag, inst = a[0], a[1]
    params, every, tag, show = {}, 1, 'eval', False
    i = 2
    while i < len(a):
        if a[i] == '--every':
            every = int(a[i + 1]); i += 2
        elif a[i] == '--tag':
            tag = a[i + 1]; i += 2
        elif a[i] == '--notes':
            show = True; i += 1
        else:
            k, v = a[i].split('='); params[k] = float(v); i += 1
    comp, det, rows = evaluate(bintag, inst, params, os.path.join(ROOT, 'tmp', tag), every)
    if comp is None:
        print('non-finite or too loud')
        return
    if show:
        for r, M, c in det:
            print(f"{r['dyn']} {r['note']:3d} lvl R {r['lvl']:6.1f} M {M['lvl'] - comp['_off']:6.1f} f0c {M['f0c']:+6.1f} per {M['per']:.2f} rise R {r['rise']*1e3:4.0f} M {M['rise']*1e3:4.0f} | "
                  f"H R {' '.join(f'{v:5.0f}' for v in r['harm'][:6])} M {' '.join(f'{v:5.0f}' for v in M['harm'][:6])} | N R {' '.join(f'{v:4.0f}' for v in r['noise'])} M {' '.join(f'{v:4.0f}' for v in M['noise'])}{' BAD' if c['bad'] else ''}")
    print(f"TOTAL {comp['total']:.3f} | {O.fmt(comp)} off={comp['_off']:.1f} guard={comp['_guard']}")


if __name__ == '__main__':
    main()
