# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Model renders of the reference grid (notes x dynamics) and their features.

DYN: the key performance standing for each recorded dynamic (the user's takes: sustained pressure
0.1-0.75, strikes 0.3-1, rises 20-80 ms).
"""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
# the work directory: refs.pkl, bin/<tag>/ (build.sh), tmp/, fits/ (outside the repository)
ROOT = os.environ.get('CLARINET_WORK', os.path.abspath('clarinet_work'))
EXE = '.exe' if os.name == 'nt' else ''
sys.path.insert(0, HERE)
import notefeats as nf  # noqa: E402

SR = 48000
# dyn: (pres, strike, rise)
DYN = {'pp': (0.12, 0.15, 0.08), 'mf': (0.35, 0.4, 0.035), 'ff': (0.7, 0.7, 0.025)}
HOLD, TAIL, PRE = 1.0, 0.35, 0.05


def render(bintag, preset, params, jobs, outdir):
    """jobs: list of (name, note, dyn[, tilt]). Returns {name: float32 array}."""
    os.makedirs(outdir, exist_ok=True)
    exe = os.path.join(ROOT, 'bin', bintag, 'clar_render' + EXE)
    cmd = [exe, '--preset', str(preset)]
    for k, v in params.items():
        cmd += ['--set', f'{k}={v}']
    outs = {}
    for jb in jobs:
        name, note, dyn = jb[:3]
        tilt = jb[3] if len(jb) > 3 else 0.0
        P, S, R = DYN[dyn]
        o = os.path.join(outdir, name + '.f32')
        cmd += ['--job', o, str(note), str(P), str(S), str(R), str(HOLD), str(TAIL), str(tilt)]
        outs[name] = o
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr + r.stdout)
    return {k: np.fromfile(v, np.float32) for k, v in outs.items()}


def features(x, note, dyn):
    P, S, R = DYN[dyn]
    f0 = 440.0 * 2 ** ((note - 69) / 12)
    return nf.note_features(x, SR, f0, PRE, PRE + R + HOLD)
