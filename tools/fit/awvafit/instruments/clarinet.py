# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The clarinet: the clarinet engine's presets (0 Bb, 1 bass) against the Iowa MIS clarinet and bass
clarinet scales (pp mf ff), note by note (terms.notewise, the lobes analysis). The fit grid: every
other recorded note (the hold-out: the others) at each recorded dynamic, one note on one voice, its
key performance standing for the dynamic (DYN: from the recorded takes).

Work directory: $AWVA_FIT_WORK/clarinet/{refs.pkl, ctl, tmp, fits}. Recordings: sources set
'clarinet' (with the ambient silence: the recording's noise floor).

  python -m awvafit clarinet refs
  python -m awvafit clarinet score <preset> [fit|hold] [name=value ...]
  python -m awvafit clarinet fit <preset> <run> [--gens N] [--pop N] [--sigma s] [--workers N] [--start best.json]

The renders: job_controls() writes the controls the lane's clar_render generated in C++ (the same
float arithmetic, so the same values), played by awva_render --ctl --nv 1, a fresh engine per note, after a second of silence (SETTLE);
with SETTLE 0 the renders are clar_render's bit for bit.
"""
import json
import os
import pickle

import numpy as np
from ..audio import read_render

from .. import env, labels, sources
from ..audio import read
from ..controls import GLOB
from ..features.notes import lobes as nf
from ..fit import Fit, Space
from ..objective import Objective
from ..render import CTL_WIDTH, Renderer
from ..segment import envelope
from ..terms import notewise

SR = 48000
DIRS = {'bb_clarinet': 'Woodwinds/Bbclarinet', 'bass_clarinet': 'Woodwinds/bassclarinet'}
INST = {0: 'bb_clarinet', 1: 'bass_clarinet'}
LOW = {0: 50.0, 1: 34.0}
# dyn: (pres, strike, rise)
DYN = {'pp': (0.12, 0.15, 0.08), 'mf': (0.35, 0.4, 0.035), 'ff': (0.7, 0.7, 0.025)}
HOLD, TAIL, PRE = 1.0, 0.35, 0.05
# s of silence before every render, as every fit (the overrides' glide arrives); 0 renders as the
# lane's clar_render did and reproduces its numbers (tools/fit/regress/clarinet.py --lane)
SETTLE = 1.0
OBJECTIVE = Objective([('even', 4.0, 1.5), ('spec', 3.0, 3.0), ('lvl', 2.5, 2.0), ('noise', 6.0, 0.6), ('rise', 0.5, 1.0),
                       ('acent', 0.5, 0.7), ('aharm', 4.0, 1.0), ('pitch', 10.0, 0.5), ('robust', 0.05, 3.0)])
SPACE = {
    'pres_lin': (0.05, 1.0, False), 'pres_sq': (0.0, 1.5, False), 'pres_hz': (3.0, 40.0, True), 'p_soft': (1.1, 1.6, False),
    'g_loud': (0.4, 1.2, False), 'g_reg': (-0.3, 0.3, False), 'p_curve': (0.4, 2.5, True), 'zeta': (0.05, 0.8, True),
    'zeta_reg': (-1.0, 1.0, False), 'reed_hz': (1000.0, 10000.0, True), 'reed_q': (0.08, 1.0, True), 'lay': (0.0, 10.0, False),
    'wall': (0.3, 5.0, True), 'lat_hz': (500.0, 8000.0, True), 'lat_q': (0.3, 1.0, False), 'reg_pos': (0.08, 0.4, False),
    'reg_tau': (0.2, 2.0, True), 'reg_r': (0.1, 2.0, True), 'alt_rel': (19.0, 36.0, False), 'tone_hz': (2500.0, 18000.0, True),
    'breath': (0.0, 0.05, False), 'tongue': (0.3, 2.0, False), 'acc_level': (0.0, 0.8, False), 'acc_decay': (0.01, 0.2, True),
    'tongue_t': (0.002, 0.012, True), 'tongue_f': (0.0, 3.0, False),
}


def work(*p):
    return env.work('clarinet', *p)


def workdir(*p):
    return env.workdir('clarinet', *p)


# --- the references ------------------------------------------------------------------------------


def floor_feats(amb, sr, F, nfft):
    """the recording's noise floor in the note's terms: per harmonic lobe re the note's harmonic total
    (dB), and per band re the note's power (dB)"""
    S = nf._spec(amb, sr, nfft)
    H = nf.harmonics(S, sr, nfft, F['f0'])
    return 10 * np.log10(H / (np.nansum(F['_H']) + 1e-30) + 1e-12), nf.noise_ratio(S, sr, nfft, F['f0'], F['_tot'])


def refs(log=print):
    amb, _ = read(sources.path('uiowa_mis', DIRS['bb_clarinet'] + '/BbClar.ambient.silence.aiff'), np.float64)
    res = {}
    for inst, rel in DIRS.items():
        d = sources.path('uiowa_mis', rel)
        notes = []
        for fn in sorted(os.listdir(d)):
            lab = labels.parse_iowa(fn)
            if lab is None or lab['dyn'] not in DYN:
                continue
            dyn, lo, hi = lab['dyn'], lab['lo'], lab['hi']
            x, sr = read(os.path.join(d, fn), np.float64)
            segs = envelope(x, sr)
            want = hi - lo + 1
            msg = f'{inst} {fn}: {len(segs)} segments for {want} notes'
            if len(segs) != want:
                if len(segs) > want:  # the longest `want` in time order
                    keep = sorted(sorted(range(len(segs)), key=lambda i: segs[i][1] - segs[i][0])[-want:])
                    segs = [segs[i] for i in keep]
                    msg += ' (kept the longest)'
                else:
                    log(msg + ' SKIPPED')
                    continue
            ok = 0
            for k, (t0, t1) in enumerate(segs):
                nt = lo + k
                F = nf.note_features(x, sr, 440.0 * 2 ** ((nt - 69) / 12), t0, t1)
                if abs(F['cents']) > 80:
                    continue
                F['hfloor'], F['nfloor'] = floor_feats(amb, sr, F, F['nfft'])
                F.update(inst=inst, dyn=dyn, note=nt, dur=t1 - t0, src=fn, sr=sr)
                notes.append(F)
                ok += 1
            log(msg + f', {ok} kept')
        res[inst] = notes
    with open(work('refs.pkl'), 'wb') as f:
        pickle.dump(res, f)
    return res


# --- the controls of one note (clar_render's, float for float) -----------------------------------


def _smooth(s):
    f = np.float32
    return f(0) if s <= 0 else (f(1) if s >= 1 else s * s * (f(3) - f(2) * s))


def job_controls(note, pres, strike, rise, hold, tail, tilt=0.0, note2=None, hold2=0.0):
    """[blocks, 58] controls of one note on voice 0, as clar_render built them: the pressure rises to
    pres over rise s (smoothstep) with vpres a pulse peaking at strike, holds, falls in 20 ms (vpres
    -0.3); 50 ms of silence first; after hold the note changes to note2 for hold2 (a legato)"""
    f = np.float32
    n1 = f(note)
    n2 = f(note if note2 is None else note2)
    pres, strike, rise, hold1, hold2, tail, tilt = (f(v) for v in (pres, strike, rise, hold, hold2, tail, tilt))
    T = 0.05 + float(rise) + float(hold1) + float(hold2) + 0.02 + float(tail)
    nb = int(T * SR / 32)
    c = np.zeros((nb, CTL_WIDTH), np.float32)
    for v in range(10):
        c[:, 5 * v] = 69
    c[:, 50:58] = GLOB
    t_off = float(rise + hold1 + hold2)  # (float sums, as in C++)
    t_n2 = float(rise + hold1)
    for b in range(nb):
        t = b * 32.0 / SR - 0.05
        p, vp = f(0), f(0)
        if 0 <= t < float(rise):
            s = f(t / float(rise))
            p = pres * _smooth(s)
            vp = strike * f(4.0) * s * (f(1.0) - s) * f(1.0)
        elif float(rise) <= t < t_off:
            p = pres
        elif t_off <= t < t_off + 0.02:
            p = pres * (f(1.0) - f((t - t_off) / 0.02))
            vp = f(-0.3)
        if p <= 0 and 0 < t < t_off:
            p = f(1e-3)
        c[b, 0:5] = (n1 if t < t_n2 else n2, p, vp, 0.0, tilt)
    return c


def render(exe, preset, params, jobs, outdir):
    """jobs: [(name, note, dyn[, tilt])] -> {name: float32 array}; one fresh engine per note"""
    os.makedirs(outdir, exist_ok=True)
    r = Renderer(exe)
    out = {}
    for jb in jobs:
        name, note, dyn = jb[:3]
        tilt = jb[3] if len(jb) > 3 else 0.0
        P, S, R = DYN[dyn]
        cp = work('ctl', f'{note}_{dyn}_{tilt:g}.ctl')
        if not os.path.exists(cp):
            job_controls(note, P, S, R, HOLD, TAIL, tilt).tofile(cp)
        o = os.path.join(outdir, name + '.f32')
        r.render('clarinet', preset, ctl=cp, nv=1, sets=params, settle=SETTLE, out=o)
        out[name] = read_render(o)
    return out


def features(x, note, dyn):
    P, S, R = DYN[dyn]
    return nf.note_features(x, SR, 440.0 * 2 ** ((note - 69) / 12), PRE, PRE + R + HOLD)


def grid(preset, which='fit'):
    R = pickle.load(open(work('refs.pkl'), 'rb'))[INST[preset]]
    ref = {(F['note'], F['dyn']): F for F in R}
    notes = sorted({F['note'] for F in R})
    notes = notes[::2] if which == 'fit' else notes[1::2]
    return [(n, d) for n in notes for d in ('pp', 'mf', 'ff') if (n, d) in ref], ref


def evaluate(exe, preset, params, keys, ref, wid):
    X = render(exe, preset, params, [(f'n{n}{d}', n, d) for n, d in keys], workdir('tmp', f'w{preset}_{wid}'))
    pairs = []
    for (n, d) in keys:
        x = X[f'n{n}{d}']
        if not np.all(np.isfinite(x)) or np.abs(x).max() > 8.0:
            return 1e3, {'error': 'non-finite or blow-up'}
        pairs.append((features(x, n, d), ref[(n, d)]))
    c = notewise.clarinet_compare(pairs, OBJECTIVE, LOW[preset])
    return OBJECTIVE.total(c), c


class Scorer:
    def __init__(self, exe, preset):
        self.exe, self.preset = exe, preset
        self.keys, self.ref = grid(preset)

    def __call__(self, params, wid):
        return evaluate(self.exe, self.preset, params, self.keys, self.ref, wid)


def fit(preset, run, gens=100, pop=20, sigma=0.2, workers=3, start=None, only=None):
    base = json.load(open(start))['params'] if start else {}
    space = Space(SPACE)
    for k in space.names:  # no start value: the middle of the range (as the lane)
        base.setdefault(k, space.from_unit(k, 0.5))
    return Fit(workdir('fits', run), space, Scorer(env.renderer(), preset), base, sigma=sigma, pop=pop, seed=1, workers=workers,
               only=only, fmt=OBJECTIVE.fmt, start_score=bool(start)).run(gens)
