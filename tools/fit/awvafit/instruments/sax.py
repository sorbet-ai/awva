# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The sax: the sax engine's presets (0 soprano, 1 alto, 2 tenor, 3 baritone) fitted to recordings,
note by note (terms.notewise, the harmonic-sum analysis). The soprano and alto to the Iowa MIS (three
dynamics with their real levels), the tenor and baritone to MTG Solo Saxophones; the soprano and alto
are then checked against MTG, which the fit didn't see (the hold-out).

Work directory: $AWVA_FIT_WORK/sax/{ref (dec: decoded recordings, refs.pkl), inputs, tmp, fits}.
Recordings: sources set 'sax' ($AWVA_REFS). Decoded with ffmpeg to 48 kHz mono (what the fit used).

  python -m awvafit sax refs                    the recordings' features -> ref/refs.pkl
  python -m awvafit sax keys                    the key templates -> inputs/t_<set>.csv, .meta.json
  python -m awvafit sax score <preset> [name=value ...]          fit and hold-out components
  python -m awvafit sax fit <preset> <run> [--gens N] [--pop N] [--sigma s] [--workers N] [--start best.json] [--chroma]
  python -m awvafit sax chroma <preset> [best.json]              every semitone at three dynamics
  python -m awvafit sax levelmatch <preset> [best.json]          the gain of the wind engine's sax
  python -m awvafit sax bake P=best.json:gain ...                every parameter into voice_sax.h

The templates: t_iowa (pp mf ff = pressure 0.2 0.5 1.0, 12 presses each), t_mtg2 (p f = 0.5 0.9),
t_mtg3 (p m f = 0.5 0.7 0.9: the baritone), t_strike (strikes 0 .. 0.95 at 0.5, 3 x 5 presses). Every
press: strike 0.35, held 1.3 s, a gap of 0.45 s, on one key: its note comes from the preset's note
list (awva_render --notes).
"""
import json
import os
import pickle
import re
import sys

import numpy as np
from ..audio import read_render

from .. import env, labels, sources
from ..audio import decode_ffmpeg
from ..bake import param_fields, write_switch
from ..controls import held_runs
from ..features.notes import harmonic_sum as sfeat
from ..fit import Fit, Space
from ..objective import Objective
from ..render import Renderer, voice_controls
from ..segment import plateau
from ..terms import notewise
from ..keys import Stream

BLOCK = 32
SETTLE, TAIL = 1.0, 1.5
OBJECTIVE = Objective([
    ('spec', 3.0, 2.0), ('harm', 4.0, 1.0), ('bright', 2.5, 2.0), ('hnr', 6.0, 0.6), ('dyn', 2.5, 2.0), ('attack', 3.0, 0.7),
    ('pitch', 10.0, 1.0), ('robust', 0.03, 2.0), ('strike', 1.0, 0.5), ('reg', 0.02, 1.5)])
# preset -> (instrument, recording source, template, the 12 notes)
PRESETS = {
    0: ('soprano', 'iowa', 'iowa', np.linspace(56, 87, 12).round().astype(int)),
    1: ('alto', 'iowa', 'iowa', np.linspace(49, 80, 12).round().astype(int)),
    2: ('tenor', 'mtg', 'mtg2', np.linspace(44, 76, 12).round().astype(int)),
    3: ('baritone', 'mtg', 'mtg3', np.linspace(38, 69, 12).round().astype(int)),
}
NAMES = ['soprano', 'alto', 'tenor', 'baritone']
VALID = {0: ('soprano', 'mtg', 'mtg2'), 1: ('alto', 'mtg', 'mtg2')}  # the hold-out: the other source
LOUD = ('ff', 'f')
RANGE = {0: (56, 87), 1: (49, 80), 2: (44, 75), 3: (37, 68)}  # the written ranges, concert
WIND = {0: 2, 1: 3, 2: 4, 3: 5}  # the wind engine's soprano, alto, tenor, baritone sax
IOWA_DIRS = {'soprano': 'Woodwinds/sopranosaxophone', 'alto': 'Woodwinds/altosaxophone'}
MTG_LAYERS = 'MTG Solo Saxophones'

# keys
KEY = 23  # a key of the default layout (the note is replaced anyway)
N_SLOTS, V_REF, HOLD, GAP = 12, 0.35, 1.3, 0.45
DYN = dict(iowa=(('pp', 0.2), ('mf', 0.5), ('ff', 1.0)), mtg2=(('p', 0.5), ('f', 0.9)), mtg3=(('p', 0.5), ('m', 0.7), ('f', 0.9)))
STRIKES = (0.0, 0.1, 0.3, 0.6, 0.95)

SPACE = {
    'pres_lin': (0.0, 1.0, False), 'pres_sq': (0.0, 1.5, False), 'g_soft': (0.95, 1.5, False), 'g_loud': (0.3, 1.6, True),
    'g_reg': (-0.6, 0.6, False), 'g_curve': (0.5, 3.5, True), 'reed_hz': (500.0, 6000.0, True), 'reed_reg': (-0.5, 1.2, False),
    'reed_q': (1.0, 25.0, True), 'h0': (0.15, 2.5, True), 'p_close': (0.8, 20.0, True), 'reed_w': (4.0, 28.0, False),
    'lay': (0.0, 8.0, False), 'emb_dyn': (-1.0, 3.0, False), 'reed_area': (0.0, 3.0, False), 'mp_vol': (0.3, 2.5, True),
    'pop_hz': (300.0, 6000.0, True), 'apex': (40.0, 700.0, True), 'apex_reg': (-0.5, 1.0, False), 'bore_d': (5.0, 30.0, True),
    'bore_decay': (0.01, 0.4, True), 'bore_reg': (-0.5, 1.5, False), 'loss_exp': (0.0, 1.2, False), 'reg_loss': (0.0, 0.5, False),
    'reg_hole': (-0.8, 0.8, False), 'hole_hz': (400.0, 5000.0, True), 'hole_q': (0.5, 3.0, True), 'hole_reg': (-0.6, 0.6, False),
    'rad_hz': (30.0, 2000.0, True), 'tone_hz': (1500.0, 15000.0, True), 'tone_q': (0.5, 2.5, True), 'low_comp': (0.0, 1.5, False),
    'breath': (0.0, 0.7, False), 'hiss': (0.0, 0.25, False), 'tongue': (0.3, 2.0, False), 'acc_level': (0.0, 1.2, False),
    'acc_decay': (0.01, 0.2, True), 'tongue_t': (0.001, 0.04, True),
}


def work(*p):
    return env.work('sax', *p)


def workdir(*p):
    return env.workdir('sax', *p)


# --- the references ------------------------------------------------------------------------------


def decode(src):
    return decode_ffmpeg(src, 48000, cache=work('ref', 'dec', os.path.splitext(os.path.basename(src))[0] + '.f32'))


def refs_iowa(log=print):
    out = {}
    for inst in ('soprano', 'alto'):
        d = sources.path('uiowa_mis', IOWA_DIRS[inst])
        for f in sorted(os.listdir(d)):
            m = re.match(r'(Sop|Alto)Sax\.NoVib\.(pp|mf|ff)\.([A-G]b?\d)([A-G]b?\d)\.aiff', f)
            if not m:
                continue
            lab = labels.parse_iowa(f)
            x = decode(os.path.join(d, f))
            segs = plateau(x)
            if len(segs) != len(lab['notes']):
                log(f'  {f}: {len(segs)} segments for {len(lab["notes"])} notes - skipped')
                continue
            for n, (a, b) in zip(lab['notes'], segs):
                r = sfeat.note_feats(x[a:b], n)
                if r is not None:
                    out[(inst, 'iowa', lab['dyn'], n)] = r
    return out


def refs_mtg():
    out = {}
    base = sources.path('mtg_solosax', MTG_LAYERS)
    data = os.path.join(base, 'Data')
    for f in sorted(os.listdir(data)):
        for inst, dyn, key, smp in labels.mtg_regions(open(os.path.join(data, f)).read(), f):
            x = decode(os.path.join(base, 'Samples', smp + '.flac'))
            r = sfeat.note_feats(x, key)
            if r is not None:
                out[(inst, 'mtg', dyn, key)] = r
    return out


def refs(log=print):
    R = {}
    R.update(refs_iowa(log))
    R.update(refs_mtg())
    with open(work('ref', 'refs.pkl'), 'wb') as f:
        pickle.dump(R, f)
    for s in sorted({k[:3] for k in R}):
        rows = [R[k] for k in sorted(k for k in R if k[:3] == s)]
        log(f"{s[0]:9s} {s[1]:4s} {s[2]:2s} n={len(rows):2d} lvl {np.mean([r['lvl'] for r in rows]):6.1f} bright {np.mean([r['bright'] for r in rows]):6.1f}")
    return R


_refs = None


def load_refs():
    global _refs
    if _refs is None:
        with open(work('ref', 'refs.pkl'), 'rb') as f:
            _refs = pickle.load(f)
    return _refs


# --- the key templates -----------------------------------------------------------------------------


def _grid(dyn):
    s, t, meta = Stream(), 0.0, []
    for d, P in dyn:
        for k in range(N_SLOTS):
            meta.append(dict(dyn=d, slot=k, P=P, v=V_REF))
            t = s.press(t, V_REF, P, HOLD, rel=0.03, kid=KEY) + GAP
    return s, meta


def _strike():
    s, t, meta = Stream(), 0.0, []
    for k in range(3):
        for v in STRIKES:
            meta.append(dict(dyn='mf', slot=k, P=0.5, v=v))
            t = s.press(t, v if v > 0 else 0.06, 0.5, 0.7, rel=0.03, kid=KEY) + GAP
    return s, meta


def keys(log=print):
    out = workdir('inputs')
    sets = {name: _grid(dyn) for name, dyn in DYN.items()}
    sets['strike'] = _strike()
    for name, (s, meta) in sets.items():
        s.write(os.path.join(out, f't_{name}.csv'), notes=False)
        json.dump(meta, open(os.path.join(out, f't_{name}.meta.json'), 'w'))
        log(name, len(meta), 'presses', f"{s.rows[-1][0]:.1f} s")


def meta(template):
    return json.load(open(work('inputs', f't_{template}.meta.json')))


def note_list(template, notes):
    return [int(notes[m['slot'] % len(notes)]) for m in meta(template)]


# --- rendering and comparing -----------------------------------------------------------------------


def render(exe, preset, params, jobs, outdir, engine='sax'):
    """jobs: [(template, notes)] -> [(output, controls)]"""
    os.makedirs(outdir, exist_ok=True)
    r = Renderer(exe)
    outs = []
    for t, notes in jobs:
        o, c = os.path.join(outdir, f'p{preset}_{t}.f32'), os.path.join(outdir, f'p{preset}_{t}.ctl')
        r.render(engine, preset, rec=work('inputs', f't_{t}.csv'), notes=note_list(t, notes), sets=params, settle=SETTLE, tail=TAIL,
                 out=o, ctl_out=c)
        outs.append((o, c))
    return outs


def model_notes(out, template, notes):
    """per press: dict(dyn, slot, note, v, f: the features)"""
    x = read_render(out[0])
    ev = held_runs(voice_controls(out[1]))
    res = []
    for (v, a, b), m in zip(ev, meta(template)):
        n = int(notes[m['slot'] % len(notes)])
        seg = x[a * BLOCK:min(b * BLOCK + int(0.3 * sfeat.SR), len(x))]
        f = sfeat.note_feats(seg, n) if len(seg) and np.all(np.isfinite(seg)) and np.max(np.abs(seg)) > 1e-6 else None
        res.append(dict(dyn=m['dyn'], slot=m['slot'], note=n, v=m['v'], f=f))
    return res


def chroma(exe, preset, params, tag='chroma', log=None):
    """every semitone of the range and 5 beyond each end at the Iowa template's dynamics: the bad
    notes; returns (bad in range, bad, total)"""
    lo, hi = RANGE[preset]
    notes = list(range(lo - 5, hi + 6))
    chunks = []
    for c in range(0, len(notes), 12):
        ch = notes[c:c + 12]
        chunks.append(ch + [ch[-1]] * (12 - len(ch)))
    bad, tot = [], 0
    outdir = workdir('tmp', f'{tag}_chroma_p{preset}')
    for k, ch in enumerate(chunks):
        out = render(exe, preset, params, [('iowa', ch)], os.path.join(outdir, str(k)))[0]
        seen = set()
        for m in model_notes(out, 'iowa', ch):
            key = (m['dyn'], m['note'])
            if key in seen:
                continue
            seen.add(key)
            tot += 1
            f = m['f']
            if notewise.bad_note(f) or (f is not None and f['H'][0] < -35):
                why = 'none' if f is None else f"f0c {f['f0c']:+.0f} H1 {f['H'][0]:.0f} sub {f['sub']:.0f} per {f['per']:.2f}"
                bad.append((m['note'], m['dyn'], lo <= m['note'] <= hi, why))
    nin = sum(1 for b in bad if b[2])
    if log:
        for b in bad:
            log(f"  {'IN ' if b[2] else 'out'} {b[0]} {b[1]}: {b[3]}")
        log(f'preset {preset}: {len(bad)} bad of {tot} ({nin} in the instrument range)')
    return nin, len(bad), tot


def evaluate(exe, preset, params, outdir, validate=False, with_chroma=False):
    inst, src, tpl, notes = PRESETS[preset]
    jobs = [(tpl, notes), ('strike', notes)]
    if validate and preset in VALID:
        jobs.append((VALID[preset][2], notes))
    outs = render(exe, preset, params, jobs, outdir)
    R = load_refs()
    mm = model_notes(outs[0], tpl, notes)
    c = notewise.compare(mm, R, inst, src, OBJECTIVE, src == 'iowa', LOUD)
    c['strike'] = notewise.strike_term(model_notes(outs[1], 'strike', notes))
    c['reg'] = 0.0
    if with_chroma:
        nin, nall, tot = chroma(exe, preset, params, tag=os.path.basename(outdir))
        c['reg'] = (nin + 0.3 * (nall - nin)) / max(tot, 1)
    c['total'] = OBJECTIVE.total(c)
    res = {'fit': c, 'notes': mm}
    if validate and preset in VALID:
        vi, vs, vt = VALID[preset]
        cv = notewise.compare(model_notes(outs[2], vt, notes), R, vi, vs, OBJECTIVE, False, LOUD)
        cv['strike'] = cv['reg'] = 0.0
        cv['total'] = OBJECTIVE.total(cv)
        res['valid'] = cv
    return res


class Scorer:
    def __init__(self, exe, preset, with_chroma):
        self.exe, self.preset, self.with_chroma = exe, preset, with_chroma

    def __call__(self, params, wid):
        r = evaluate(self.exe, self.preset, params, workdir('tmp', f'p{self.preset}w{wid}'), with_chroma=self.with_chroma)
        return r['fit']['total'], r['fit']


def fit(preset, run, gens=100, pop=16, sigma=0.12, workers=5, start=None, with_chroma=False):
    exe = env.renderer()
    base = {k: p.value for k, p in Renderer(exe).params('sax', preset).items()}
    if start:
        base.update(json.load(open(start))['params'])
    space = Space(SPACE)
    base = {k: base[k] for k in space.names}
    best = Fit(workdir('fits', run), space, Scorer(exe, preset, with_chroma), base, sigma=sigma, pop=pop, seed=7 + preset,
               workers=workers, fmt=OBJECTIVE.fmt).run(gens)
    r = evaluate(exe, preset, best[1], workdir('tmp', f'p{preset}final'), validate=True)
    json.dump({'total': best[0], 'params': best[1], 'comps': r['fit'], 'valid': r.get('valid')},
              open(os.path.join(workdir('fits', run), 'best.json'), 'w'), indent=1)
    return best, r


def level(exe, engine, preset, params, tpl, notes):
    out = render(exe, preset, params, [(tpl, notes)], workdir('tmp', f'level_{engine}'), engine=engine)[0]
    return float(np.mean([m['f']['lvl'] for m in model_notes(out, tpl, notes) if m['f'] and m['dyn'] in LOUD]))


def levelmatch(preset, params=None):
    """the gain that puts the preset at the wind engine's level for the same instrument: the mean
    sustain level of the template's loudest dynamic, both engines on the same keys"""
    exe = env.renderer()
    params = dict(params or {})
    inst, src, tpl, notes = PRESETS[preset]
    gain = params.get('gain', Renderer(exe).params('sax', preset)['gain'].value)
    ls = level(exe, 'sax', preset, dict(params, gain=gain), tpl, notes)
    lw = level(exe, 'wind', WIND[preset], {}, tpl, notes)
    return ls, lw, gain * 10 ** ((lw - ls) / 20)


def bake(header, entries):
    """entries {preset: (best.json, gain)}: every live parameter of each preset into preset_params()"""
    exe = env.renderer()
    src = open(header, encoding='utf-8').read()
    field = param_fields(src)
    rng = {m[0]: (float(m[1].rstrip('f')), float(m[2].rstrip('f'))) for m in re.findall(r'\{\{"(\w+)", "\w+", ([-0-9.e]+f), ([-0-9.e]+f), C_\}', src)}
    cases = {}
    for pr in range(4):
        bj, gain = entries[pr]
        vals = {k: p.value for k, p in Renderer(exe).params('sax', pr).items()}
        vals.update(json.load(open(bj))['params'])
        vals['gain'] = float(gain)
        for k, v in vals.items():
            lo, hi = rng[k]
            if not lo <= v <= hi:
                raise ValueError(f'preset {pr}: {k} = {v} outside {lo}..{hi}')
        cases[pr] = {field[k]: v for k, v in vals.items()}
    write_switch(header, '[[nodiscard]] static inline Params preset_params(int i) {', cases, dict(enumerate(NAMES)), default=1)


def main_score(preset, params):
    r = evaluate(env.renderer(), preset, params, workdir('tmp', 'eval'), validate=True)
    print('fit  ', OBJECTIVE.fmt(r['fit']))
    if 'valid' in r:
        print('valid', OBJECTIVE.fmt(r['valid']))
    return r


if __name__ == '__main__':
    sys.exit('python -m awvafit sax ...')
