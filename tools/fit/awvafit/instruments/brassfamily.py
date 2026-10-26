# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The brass family: the trumpet engine's presets 1-5 (flugelhorn, trombone, horn, euphonium, tuba)
fitted to the brass engine's preset of the same name. Both engines play identical inputs in the
instrument's register (the trombone, horn, euphonium an octave below the trumpet, the tuba two), and
the trumpet engine is compared frame by frame with the brass engine on the controls it got
(terms.framewise). Real recordings are a sanity check only.

Work directory: $AWVA_FIT_WORK/brassfamily/{inputs, cap/<inst>, cache, tmp, fits}.
The inputs: the phrases (awvafit.phrases, syn_<name>) and the recorded takes ($AWVA_REC). With
AWVA_CAPTURES (a directory of controls captured from an embedded host, <input>.ctl) the references play those
instead of the key path: the path the published numbers were measured on.

  python -m awvafit brassfamily refs [inst ...]
  python -m awvafit brassfamily score <inst> [--preset N] [--set fit|hold|all] [name=value ...]
  python -m awvafit brassfamily finish <inst> --preset | <run>
  python -m awvafit brassfamily fit <inst> <run> [--gens N] [--pop N] [--sigma s] [--workers N] [--start best.json]
  python -m awvafit brassfamily pitch <inst> [--set fit|all]
  python -m awvafit brassfamily bake <inst> <final.json> [comment]
"""
import json
import os
import pickle
from concurrent.futures import ProcessPoolExecutor

import numpy as np

from .. import env, phrases
from ..bake import bake_case
from ..features.frames import FrameAnalysis
from ..fit import Fit, Space
from ..objective import Objective
from ..render import Renderer, voice_controls
from ..terms import framewise

# hyb: the brass engine's preset (the reference); phys: the trumpet engine's; T: semitones the
# trumpet-register inputs move; T_regs: for the register sweep; mel_lo: the analysis' lowest mel
# edge (the low brass lower, so their fundamentals count); tilt: the level-dependent tilt terms
INST = {
    'flugelhorn': dict(hyb=1, phys=1, T=0),
    'trombone': dict(hyb=2, phys=2, T=-12, mel_lo=40, tilt=True),
    'horn': dict(hyb=3, phys=3, T=-12, mel_lo=40),
    'euphonium': dict(hyb=4, phys=4, T=-12, mel_lo=40),
    # the core inputs two octaves down; the register sweep (41..83) one octave down (29..71: two
    # would reach below the delay lines' lowest note)
    'tuba': dict(hyb=5, phys=5, T=-24, T_regs=-12, mel_lo=30),
}
FIT_BASE = ['syn_strike', 'syn_pressure', 'syn_grid', 'syn_regs', 'syn_cresc', 'syn_stacc', 'syn_slurs', 'syn_tilts',
            'play_2026-10-22_staccato', 'play_2026-10-22_moderate']
HOLD = ['play_2026-10-22_legato', 'play_2026-10-22_a']
ALL_INPUTS = FIT_BASE + HOLD
SETTLE = 1.0   # s of silence before every render: the overrides' glide arrives
TAIL = 1.5     # s after a key stream

OBJECTIVE = Objective([
    ('gain', 3.0, 0.3), ('lvl', 2.5, 1.5), ('spec_a', 3.0, 1.0), ('spec_s', 2.0, 1.5), ('spec_r', 4.0, 0.5),
    ('pitch', 8.0, 1.0), ('voicing', 0.05, 1.0), ('hnr_a', 5.0, 0.5), ('hnr_s', 4.0, 1.0), ('robust', 0.02, 1.5),
    ('curve', 2.5, 2.5), ('flat', 0.25, 2.5), ('silence', 6.0, 0.5)])
TILT_TERMS = [('tilt', 1.5, 1.5), ('lowmid', 4.0, 1.5)]

# the fit's space: name -> (start, lo, hi, log); clipped to the engine's ranges at run time
SPACE = {
    'gain': (10.0, 1.0, 300.0, True), 'pres_lin': (0.3, 0.0, 1.0, False), 'pres_sq': (0.7, 0.0, 1.5, False),
    'pres_hz': (15.0, 3.0, 60.0, True), 'release': (0.03, 0.008, 0.15, True), 'p_soft': (1.0, 0.95, 1.25, False),
    'p_loud': (6.0, 1.5, 20.0, True), 'p_reg': (0.0, -0.5, 0.8, False), 'p_curve': (2.0, 0.5, 4.0, True),
    'lip_tune': (-400.0, -750.0, -100.0, False), 'lip_treg': (0.0, -250.0, 250.0, False), 'lip_q': (8.0, 2.0, 40.0, True),
    'lip_mass': (4.0, 0.3, 30.0, True), 'mass_reg': (0.5, -0.3, 1.5, False), 'lip_open': (0.2, 0.003, 0.8, True),
    'open_reg': (0.3, -0.5, 1.5, False), 'lip_width': (8.0, 4.0, 34.0, False), 'contact': (10.0, 0.3, 40.0, True),
    'lip_nl': (0.3, 0.02, 10.0, True), 'embouch': (1.0, 0.0, 1.4, False), 'lip_tilt': (0.3, -0.5, 1.2, False),
    'jitter': (6.0, 0.0, 25.0, False), 'cup': (0.7, 0.3, 10.0, True), 'pop_hz': (900.0, 150.0, 2800.0, True),
    'pop_q': (2.0, 0.7, 8.0, True), 'bore_decay': (0.025, 0.008, 0.3, True), 'bore_reg': (0.0, -0.5, 1.5, False),
    'bell_hz': (1400.0, 100.0, 4000.0, True), 'brassiness': (200.0, 10.0, 1500.0, True), 'rad_hz': (350.0, 20.0, 1200.0, True),
    'tone_hz': (5500.0, 400.0, 14000.0, True), 'tone_q': (0.8, 0.5, 1.6, False), 'low_comp': (0.75, 0.0, 1.5, False),
    'breath': (0.08, 0.0, 0.108717, False),  # at most the trumpet's (breathiness was rejected by ear)
    't_noise': (0.15, 0.0, 0.8, False), 'tongue': (1.0, 0.3, 2.0, False), 'acc_level': (0.35, 0.0, 1.5, False),
    'acc_decay': (0.05, 0.01, 0.2, True), 'tongue_t': (0.01, 0.001, 0.04, True), 'tongue_lip': (0.3, 0.0, 2.0, False),
    'slur': (0.25, 0.0, 1.0, False), 'pitch_t': (0.04, 0.003, 0.15, True), 'bore_mm': (11.7, 8.0, 25.0, True),
    'hand': (0.0, -1.5, 1.5, False),
}


def objective(inst):
    return OBJECTIVE.with_terms(TILT_TERMS) if INST[inst].get('tilt') else OBJECTIVE


def analysis(inst):
    return FrameAnalysis(INST[inst].get('mel_lo', 80))


def fit_set(inst):
    return FIT_BASE + (['syn_regs0'] if INST[inst]['T'] != 0 else [])


def sets(inst):
    f = fit_set(inst)
    return dict(fit=f, hold=HOLD, all=f + HOLD)


def inputs(inst):
    """every input of an instrument: (name, the key stream's base name, transposition)"""
    cfg = INST[inst]
    out = []
    for name in ALL_INPUTS + (['syn_regs0'] if cfg['T'] != 0 else []):
        base = 'syn_regs' if name == 'syn_regs0' else name
        T = 0 if name == 'syn_regs0' else cfg['T']
        if name == 'syn_regs' and 'T_regs' in cfg:
            T = cfg['T_regs']
        out.append((name, base, T))
    return out


def work(*p):
    return env.work('brassfamily', *p)


def workdir(*p):
    return env.workdir('brassfamily', *p)


def key_stream(base):
    if base.startswith('syn_'):
        p = os.path.join(workdir('inputs'), base + '.csv')
        if not os.path.exists(p):
            phrases.write_all(workdir('inputs'))
        return p
    return os.path.join(env.rec_dir(), base + '.csv')


def cap(inst, name, ext):
    """the reference's controls (.ctl) or render (.eng.f32) of an input"""
    return work('cap', inst, name + ext)


def refs(insts=None, renderer=None, log=print):
    """the references: per instrument every input in its register on the brass engine's preset;
    writes cap/<inst>/<input>.ctl (the controls the engine got) and .eng.f32 (its render)"""
    r = renderer or Renderer()
    capd = env.captures()
    for inst in insts or list(INST):
        cfg = INST[inst]
        for name, base, T in inputs(inst):
            ctl, eng = cap(inst, name, '.ctl'), cap(inst, name, '.eng.f32')
            if capd:
                c = np.fromfile(os.path.join(capd, base + '.ctl'), np.float32).reshape(-1, 58).copy()
                c[:, 0:50:5] += T
                c.tofile(ctl)
                res = r.render('brass', cfg['hyb'], ctl=ctl, settle=SETTLE, out=eng)
            else:
                res = r.render('brass', cfg['hyb'], rec=key_stream(base), transpose=T, settle=SETTLE, tail=TAIL, out=eng, ctl_out=ctl)
            log(inst, name, f'T={T:+d}', f"{res.manifest['seconds']:.2f} s, peak {res.manifest['peak']:.3f}")


class Evaluator:
    """an instrument's renders and their comparison with the references (picklable for workers)"""

    def __init__(self, inst, exe=None):
        self.inst = inst
        self.cfg = INST[inst]
        self.obj = objective(inst)
        self.A = analysis(inst)
        self.exe = exe or env.renderer()
        self.sets = sets(inst)

    def ref_feats(self, name):
        pth = work('cache', f"{self.inst}.{name}.mel{self.A.mel_lo:g}.pkl")
        src = cap(self.inst, name, '.eng.f32')
        if os.path.exists(pth) and os.path.getmtime(pth) > os.path.getmtime(src):
            with open(pth, 'rb') as f:
                return pickle.load(f)
        F = self.A.analyse(np.fromfile(src, np.float32), voice_controls(cap(self.inst, name, '.ctl')))
        with open(pth, 'wb') as f:
            pickle.dump(F, f)
        return F

    def compare(self, name, out):
        H = self.ref_feats(name)
        M = self.A.analyse(np.fromfile(out, np.float32), voice_controls(cap(self.inst, name, '.ctl')))
        return framewise.compare(H, M, self.obj, tilt=bool(self.cfg.get('tilt')), analysis=self.A)

    def render(self, engine, preset, params, names, outdir):
        os.makedirs(outdir, exist_ok=True)
        r = Renderer(self.exe)
        outs = []
        for n in names:
            o = os.path.join(outdir, n + '.f32')
            r.render(engine, preset, ctl=cap(self.inst, n, '.ctl'), sets=params, settle=SETTLE, out=o)
            outs.append(o)
        return outs

    def _one(self, job):
        name, out = job
        return name, self.compare(name, out)

    def evaluate(self, engine, preset, params, names, outdir, pool=None):
        outs = self.render(engine, preset, params, names, outdir)
        jobs = list(zip(names, outs))
        res = dict(pool.map(self._one, jobs)) if pool else dict(map(self._one, jobs))
        return res, self.obj.combine(res)

    # the fit's score: one candidate, every fit input (in a worker)
    def score(self, params, wid):
        names = self.sets['fit']
        outs = self.render('trumpet', self.cfg['phys'], params, names, workdir('tmp', f'{self.inst}_w{wid}'))
        res = {n: self.compare(n, o) for n, o in zip(names, outs)}
        comb = self.obj.combine(res)
        return comb['total'], comb


def score(inst, preset=None, params=None, which='fit', engine='trumpet', workers=5, tag='eval'):
    ev = Evaluator(inst)
    names = ev.sets[which]
    with ProcessPoolExecutor(max_workers=min(len(names), workers)) as pool:
        return ev.evaluate(engine, ev.cfg['phys'] if preset is None else preset, params or {}, names, workdir('tmp', f'{inst}_{tag}'), pool)


def finish(inst, run=None, workers=5):
    """the preset as it is (run None: the published numbers), or a fit's best with the level offset
    removed (the output is linear in gain), on the fit and the hold-out sets; writes final.json"""
    ev = Evaluator(inst)
    k = ev.cfg['phys']
    tmp = workdir('tmp', inst + '_fin')
    with ProcessPoolExecutor(max_workers=workers) as pool:
        if run is None:
            res, comb = ev.evaluate('trumpet', k, {}, ev.sets['fit'], tmp, pool)
            hres, hcomb = ev.evaluate('trumpet', k, {}, ev.sets['hold'], tmp, pool)
            return dict(inst=inst, fit_total=comb['total'], hold_total=hcomb['total'], fit=comb, hold=hcomb, params={})
        best = json.load(open(work('fits', run, 'best.json')))
        P = dict(best['params'])
        res, comb = ev.evaluate('trumpet', k, P, ev.sets['fit'], tmp, pool)
        g = float(np.median([c['_g'] for c in res.values()]))
        P2 = dict(P)
        P2['gain'] = round(min(max(P['gain'] * 10 ** (-g / 20), 0.05), 60.0), 6)
        res2, comb2 = ev.evaluate('trumpet', k, P2, ev.sets['fit'], tmp, pool)
        if comb2['total'] < comb['total']:
            P, res, comb = P2, res2, comb2
        hres, hcomb = ev.evaluate('trumpet', k, P, ev.sets['hold'], tmp, pool)
    out = dict(inst=inst, fit_total=comb['total'], hold_total=hcomb['total'], level_offset_db=g, fit=comb, hold=hcomb, params=P,
               start_total=best['total'])
    json.dump(out, open(work('fits', run, 'final.json'), 'w'), indent=1)
    return out


def fit(inst, run, gens=40, pop=24, sigma=0.15, workers=5, start=None, only=None):
    ev = Evaluator(inst)
    r = Renderer(ev.exe)
    table = r.params('trumpet', ev.cfg['phys'])
    space = Space(SPACE).clip_to(table)
    base = {k: table[k].value for k in SPACE}
    if start:
        base.update(json.load(open(start))['params'])
    for n in ev.sets['fit']:
        ev.ref_feats(n)  # cached before the workers start
    return Fit(workdir('fits', run), space, ev.score, base, sigma=sigma, pop=pop, seed=1, workers=workers, only=only,
               fmt=ev.obj.fmt).run(gens)


def pitch(inst, which='fit', params=None, skip=25):
    """the sustain pitch per note of the trumpet preset and of its reference: (model, reference)
    arrays of (note, cents)"""
    ev = Evaluator(inst)
    names = ev.sets[which]
    outs = ev.render('trumpet', ev.cfg['phys'], params or {}, names, workdir('tmp', inst + '_pitch'))
    m, h = [], []
    for n, o in zip(names, outs):
        H = ev.ref_feats(n)
        M = ev.A.analyse(np.fromfile(o, np.float32), voice_controls(cap(inst, n, '.ctl')))
        m += framewise.sustain_pitch(M, H, skip)
        h += framewise.sustain_pitch(H, H, skip)
    return np.array(m), np.array(h)


def bake(inst, src, comment=None, root=None):
    """the fitted parameters (final.json or best.json) into the trumpet family's preset case"""
    root = root or env.ROOT
    k = INST[inst]['phys']
    params = json.load(open(src))['params']
    dsp = os.path.join(root, 'dsp', 'include', 'awva')
    return bake_case(os.path.join(dsp, 'trumpet_family.h'), k, params, os.path.join(dsp, 'voice_trumpet.h'), 'trumpet_params', comment)
