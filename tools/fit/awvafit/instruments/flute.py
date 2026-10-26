# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The flute: the flute engine's presets (0 flute, 1 alto flute, 2 piccolo) against recordings, note by
note (terms.notewise, the windowed analysis): the Iowa MIS flute (non-vibrato) and alto flute (pp mf
ff chromatic scales), and the VSCO-2 CE piccolo (one dynamic). Every recorded note is rendered as one
held note on voice 0 (1.0 s) at its dynamic's key pressure, with a moderate tongued strike.

Work directory: $AWVA_FIT_WORK/flute/{ref (<name>.f32, index.json, feats.pkl), ctl, takes, tmp, fits}.
Recordings: sources set 'flute'. The Iowa files are cut into notes (segment 'runs'), each note's pitch
checked (dropped > 60 cents off), resampled 44.1 -> 48 kHz; the piccolo is one note per file, its
sounding octave checked (it sounds an octave above the written note).

  python -m awvafit flute refs
  python -m awvafit flute score <flute|alto|piccolo> [--every n] [name=value ...]
  python -m awvafit flute fit <inst> <run> [--gens N] [--pop N] [--sigma s] [--every n] [--workers N] [--start best.json]
  python -m awvafit flute robust <preset> [best.json]     notes at three pressures that don't speak
  python -m awvafit flute rises <preset> [best.json]      the tone's rise from the key on the takes (AWVA_CAPTURES)

The piccolo's fit (fit_picc) weighs the tone's rise x3 and not the level or the dynamics (one dynamic,
the samples' gains unknown), and adds the takes' rise and the in-range robustness as penalties.
"""
import json
import os
import pickle
import re

import numpy as np

from .. import env, labels, sources
from ..audio import read, resample
from ..constraints import MaxRise
from ..controls import Ctl
from ..features.notes import windowed as feat
from ..fit import Fit, Space
from ..objective import Objective
from ..render import Renderer
from ..segment import runs, single
from ..terms import notewise

PRESET = dict(flute=0, alto=1, piccolo=2)
IOWA_DIRS = {'flute': 'Woodwinds/flute', 'alto': 'Woodwinds/altoflute'}
VSCO_DIR = 'Woodwinds/Piccolo'
DYN_P = dict(pp=0.25, mf=0.55, ff=1.0)
STRIKE = 0.8  # the recorded strikes: vpres peaks 0.5-1.0, the key rising in ~30-40 ms (the recorded takes)
HOLD = 1.0
# s of silence before every render, as every fit (the overrides' glide arrives); 0 renders as the
# lane's own renderer did and reproduces its numbers (tools/fit/regress/flute.py --lane)
SETTLE = 1.0
OBJECTIVE = Objective([('trise', 0.5, 0.0), ('dyn', 4.0, 2.0), ('harm', 4.0, 3.0), ('lvl', 3.0, 1.5), ('noise', 5.0, 1.0),
                       ('atk', 6.0, 1.0), ('rise', 0.7, 0.5), ('pitch', 10.0, 0.5), ('bad', 0.05, 3.0)])
PICCOLO = Objective([(n, s, {'trise': 3.0, 'lvl': 0.0, 'dyn': 0.0}.get(n, w)) for n, s, w in OBJECTIVE.terms])
TAKES = ['play_2026-10-22_a.ctl', 'play_2026-10-22_legato.ctl', 'play_2026-10-22_moderate.ctl', 'play_2026-10-22_staccato.ctl', 'syn_strike.ctl']
ROBUST_NOTES = {2: list(range(74, 104, 2))}
# listener-set (constraints.py): the attack wanted, which the recordings can't show (the Iowa flute
# rises as slowly as flute v1, tagged slow-attack); flute v4's targets
CONSTRAINTS = [MaxRise(strikes=(0.4, 1.0), ms=50.0, levels=(0.4, 0.6, 0.9)), MaxRise(strikes=(0.07, 0.4), ms=120.0, tol=20.0)]

SPACE = {
    'p_soft': (150.0, 40.0, 600.0, True), 'p_loud': (900.0, 200.0, 4000.0, True), 'p_reg': (0.6, 0.0, 1.6, False),
    'p_curve': (1.0, 0.4, 2.5, True), 'pres_hz': (14.0, 4.0, 40.0, True), 'theta': (0.5, 0.3, 0.8, False),
    'theta_reg': (0.0, -0.4, 0.4, False), 'w_track': (0.6, 0.0, 1.0, False), 'jet_h': (1.0, 0.4, 2.0, True),
    'h_loud': (0.3, -0.5, 1.5, False), 'jet_w': (10.0, 4.0, 18.0, True), 'g_soft': (1.3, 1.05, 6.0, True),
    'amp_loud': (0.5, -1.5, 2.5, False), 'y0': (0.4, -3.0, 3.0, False), 'jet_st': (0.08, 0.02, 0.5, True),
    'vortex': (0.6, 0.2, 3.0, True), 'bore_decay': (0.025, 0.006, 0.12, True), 'bore_reg': (0.5, -0.3, 1.5, False),
    'cut_hz': (2000.0, 600.0, 12000.0, True), 'wall': (4.0, 1.0, 30.0, True), 'disp': (0.0, -0.6, 0.6, False),
    'end': (0.5, 0.0, 3.0, False), 'dipole': (0.3, 0.0, 6.0, False), 'tone_hz': (9000.0, 800.0, 16000.0, True),
    'tone_q': (0.6, 0.5, 1.5, False), 'turb': (0.02, 0.0, 0.5, False), 'turb_hz': (2000.0, 200.0, 10000.0, True),
    'hiss': (0.002, 0.0, 0.05, False), 'hiss_st': (0.15, 0.03, 0.6, True), 't_noise': (0.3, 0.0, 2.0, False),
    'tongue': (1.2, 0.3, 2.0, False), 'acc_level': (0.4, 0.0, 1.5, False), 'acc_decay': (0.03, 0.008, 0.2, True),
    'tongue_t': (0.012, 0.001, 0.08, True),
}


def work(*p):
    return env.work('flute', *p)


def workdir(*p):
    return env.workdir('flute', *p)


def objective(inst):
    return PICCOLO if inst == 'piccolo' else OBJECTIVE


# --- the references ------------------------------------------------------------------------------


def f0_acf(x, sr, f_exp):
    n = min(len(x), 8192)
    seg = x[len(x) // 2 - n // 2: len(x) // 2 + n // 2] * np.hanning(n)
    r = np.fft.irfft(np.abs(np.fft.rfft(seg, 2 * n)) ** 2)[:n]
    T = sr / f_exp
    lo, hi = int(T / 1.12), int(T * 1.12) + 2
    k = lo + int(np.argmax(r[lo:hi]))
    a, b, c = r[k - 1], r[k], r[k + 1]
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) < 0 else 0
    return sr / (k + d)


def segment_iowa(tag, out, log=print):
    src = sources.path('uiowa_mis', IOWA_DIRS[tag])
    idx = []
    for fn in sorted(os.listdir(src)):
        lab = labels.parse_iowa(fn)
        if lab is None:
            continue
        vib = lab['vib']
        notes = lab['notes']
        x, sr = read(os.path.join(src, fn))
        rr, hop, msg = runs(x, sr, len(notes))
        nfr = len(x) // hop
        if len(rr) != len(notes):
            log(f'{fn}: {msg} -- MISMATCH, skipped')
            continue
        for (a, b), nt in zip(rr, notes):
            a0 = max(a - 5, 0) * hop  # 50 ms before the onset
            b0 = min(b + 30, nfr) * hop
            seg = x[a0:b0]
            fexp = 440 * 2 ** ((nt - 69) / 12)
            mid = x[a * hop + (b - a) * hop // 4: a * hop + 3 * (b - a) * hop // 4]
            cents = 1200 * np.log2(f0_acf(mid, sr, fexp) / fexp)
            if abs(cents) > 60:
                log(f'  note {nt}: {cents:+.0f} c off, dropped')
                continue
            y = resample(seg, sr, 48000).astype(np.float32)
            name = f"{tag}_{'vib_' if vib else ''}{lab['dyn']}_{nt}"
            y.tofile(os.path.join(out, name + '.f32'))
            idx.append(dict(name=name, inst=tag, dyn=lab['dyn'], vib=vib, note=nt, cents=float(cents), dur=float((b - a) * 0.01),
                            onset=float((a * hop - a0) / sr), off=float((b * hop - a0) / sr), file=fn))
    return idx


def acf_cents(x, sr, f):
    n = min(len(x), 8192) // 2 * 2
    c = len(x) // 2
    s = x[c - n // 2:c + n // 2] * np.hanning(n)
    r = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * n)) ** 2)[:n]
    T = sr / f
    lo, hi = int(T / 1.06), int(T * 1.06) + 2
    k = lo + int(np.argmax(r[lo:hi]))
    return r[k] / r[0], 1200 * np.log2(T / k)


def segment_vsco(out, log=print):
    idx = []
    found = []
    for sub in ('Sus', 'Stac'):
        d = sources.path('vsco2ce', f'{VSCO_DIR}/{sub}')
        if os.path.isdir(d):
            found += [(fn, os.path.join(d, fn)) for fn in os.listdir(d)]
    for fn, p in sorted(found):
        m = re.fullmatch(r'piccolo_([A-G]#?)(\d)_(sustain|staccato)\d\.wav', fn)
        if not m:
            continue
        written = labels.midi(m.group(1) + m.group(2))
        x, sr = read(p)
        a, b, hop, nL = single(x, sr)
        mid = x[(a + (b - a) // 4) * hop:(a + 3 * (b - a) // 4) * hop]
        cand = [(acf_cents(mid, sr, 440 * 2 ** ((nt - 69) / 12)), nt) for nt in (written, written + 12)]
        (per, cents), note = max(cand, key=lambda c: c[0][0] - (0.02 if c[1] == written else 0.0))
        kind = 'mf' if m.group(3) == 'sustain' else 'st'
        a0, b0 = max(a - 5, 0) * hop, min(b + 30, nL) * hop
        y = resample(x[a0:b0], sr, 48000).astype(np.float32)
        name = f'piccolo_{kind}_{note}'
        y.tofile(os.path.join(out, name + '.f32'))
        idx.append(dict(name=name, inst='piccolo', dyn=kind, vib=False, note=note, cents=float(cents), dur=float((b - a) * 0.01),
                        onset=float((a * hop - a0) / sr), off=float((b * hop - a0) / sr), file=fn))
    return idx


def refs(log=print):
    """every recorded note -> ref/<name>.f32, ref/index.json, ref/feats.pkl"""
    out = workdir('ref')
    idx = segment_iowa('flute', out, log) + segment_iowa('alto', out, log) + segment_vsco(out, log)
    json.dump(idx, open(os.path.join(out, 'index.json'), 'w'), indent=0)
    F = {}
    for r in idx:
        x = np.fromfile(os.path.join(out, r['name'] + '.f32'), np.float32)
        F[r['name']] = dict(r, **feat.note_feats(x, r['note'], r['onset'], r['off']))
    pickle.dump(F, open(os.path.join(out, 'feats.pkl'), 'wb'))
    return F


_REF = None


def ref():
    global _REF
    if _REF is None:
        _REF = pickle.load(open(work('ref', 'feats.pkl'), 'rb'))
    return _REF


# --- rendering and comparing -----------------------------------------------------------------------


def fit_set(inst, every=1):
    rows = [r for r in ref().values() if r['inst'] == inst and not r['vib'] and r['dyn'] in DYN_P]
    rows.sort(key=lambda r: (r['dyn'], r['note']))
    return [r for r in rows if r['note'] % every == 0]


def make_ctl(rows, path):
    gap = 0.45
    c = Ctl(len(rows) * (HOLD + 0.1 + gap) + 0.3)
    t = 0.1
    evs = []
    for r in rows:
        t_end = c.note(0, t, r['note'], DYN_P[r['dyn']], STRIKE, HOLD)
        evs.append((t, t_end - 0.02))
        t = t_end + gap
    c.write(path)
    return evs


def render(exe, inst, params, outdir, every=1):
    os.makedirs(outdir, exist_ok=True)
    rows = fit_set(inst, every)
    cp = work('ctl', f'{inst}_e{every}.ctl')
    evp = cp + '.npy'
    if not os.path.exists(evp):
        np.save(evp, np.array(make_ctl(rows, cp)))
    evs = np.load(evp)
    op = os.path.join(outdir, f'{inst}.f32')
    res = Renderer(exe).render('flute', PRESET[inst], ctl=cp, sets=params, settle=SETTLE, out=op)
    return rows, evs, np.fromfile(op, np.float32), res


def evaluate(exe, inst, params, outdir, every=1):
    rows, evs, x, _ = render(exe, inst, params, outdir, every)
    if not np.all(np.isfinite(x)) or np.abs(x).max() > 8:
        return None, None, rows
    feats = [feat.note_feats(x, r['note'], t0, t1, from_key=True) for r, (t0, t1) in zip(rows, evs)]
    comp, per = notewise.flute_combine(list(zip(rows, feats)), objective(inst))
    return comp, list(zip(rows, feats, per)), rows


def probe(exe, preset, plays, params, strike=0.5, hold=1.0, tag='probe'):
    """held notes on voice 0, every (note, pressure) of plays: the render and the events"""
    c = Ctl(len(plays) * (hold + 0.65) + 1.0)
    t = 0.2
    evs = []
    for (n, P) in plays:
        t_end = c.note(0, t, n, P, strike, hold)
        evs.append((n, P, t, t_end - 0.02))
        t = t_end + 0.4
    d = workdir('tmp', tag)
    cp, op = os.path.join(d, 'p.ctl'), os.path.join(d, 'p.f32')
    c.write(cp)
    Renderer(exe).render('flute', preset, ctl=cp, sets=params, settle=SETTLE, out=op)
    return np.fromfile(op, np.float32), evs


def robust(exe, preset, params, notes, pres=(0.04, 0.4, 1.0), tag='rb'):
    """the fraction of plays (every note at three pressures) that don't speak: the strongest peak not
    the note's harmonic within 25 cents, the fundamental 20 dB under it, or < 85 % harmonic energy"""
    SR = 48000
    plays = [(n, P) for n in notes for P in pres]
    x, evs = probe(exe, preset, plays, params, 0.6, 0.7, tag=tag)
    N = 8192
    nb, bad = 0, []
    for (n, P, t0, t1) in evs:
        a = int((t0 + 0.35) * SR)
        seg = x[a:a + N] * np.hanning(N)
        S = np.abs(np.fft.rfft(seg)) ** 2
        f = np.arange(len(S)) * SR / N
        f0 = 440 * 2 ** ((n - 69) / 12)
        tot = S[f > 50].sum() + 1e-30
        harm = sum(S[(f > k * f0 * 0.985) & (f < k * f0 * 1.015)].sum() for k in range(1, int(20000 / f0) + 1))
        i = int(np.argmax(S * (f > 50)))
        r = f[i] / f0
        m = max(1, int(round(r)))
        c = 1200 * np.log2(f[i] / (m * f0)) if r > 0.7 else 9999
        h1 = S[(f > f0 * 0.985) & (f < f0 * 1.015)].sum()
        b = r < 0.7 or abs(c) > 25 or h1 < S[i] * 0.01 or harm / tot < 0.85
        nb += b
        if b:
            bad.append((n, P))
    return nb / len(evs), bad


def rises(exe, preset, params, tag):
    """the tone's rise on the recorded takes (engine controls captured from an embedded host, AWVA_CAPTURES):
    per note from silence (>= 100 ms) held >= 250 ms, (strike, s from the key until harmonics 1-3 are
    within 3 dB of their level 150-400 ms in)"""
    SR, BLK = 48000, 32
    capd = env.captures()
    d = workdir('tmp', tag)
    rows = []
    for fn in TAKES:
        cp, op = os.path.join(capd, fn), os.path.join(d, fn + '.f32')
        Renderer(exe).render('flute', preset, ctl=cp, sets=params, settle=SETTLE, out=op)
        x = np.fromfile(op, np.float32)
        c = np.fromfile(cp, np.float32).reshape(-1, 58)[:, :50].reshape(-1, 10, 5)
        held = c[:, :, 1] > 0
        anyh = held.any(1)
        for b in range(1, len(anyh)):
            if anyh[b] and not anyh[max(b - 150, 0):b].any():
                v = int(np.argmax(held[b]))
                dur = int(np.argmax(~held[b:, v])) if (~held[b:, v]).any() else 300
                if dur < 375:
                    continue
                strike = float(c[b:b + 60, v, 2].max())
                note = float(np.median(c[b + 10:b + 60, v, 0]))
                t, _ = feat.tonal_rise(x, note, b * BLK, int(min(dur * BLK, 0.4 * SR)))
                rows.append((strike, t))
    r = np.array(rows)
    hard, soft = r[r[:, 0] >= 0.4, 1], r[r[:, 0] < 0.4, 1]
    return (float(np.median(hard)) if len(hard) else 0.0, float(np.median(soft)) if len(soft) else 0.0, len(hard), len(soft))


class Scorer:
    def __init__(self, exe, inst, every, penalties=False):
        self.exe, self.inst, self.every, self.penalties = exe, inst, every, penalties

    def __call__(self, params, wid):
        comp, _, _ = evaluate(self.exe, self.inst, params, workdir('tmp', f'w{wid}'), self.every)
        if comp is None:
            return 1e3, {'error': 'non-finite'}
        if self.penalties:  # the piccolo: the takes' rise and the in-range robustness
            p = PRESET[self.inst]
            h, s, _, _ = rises(self.exe, p, params, f'pt{wid}')
            bf, _ = robust(self.exe, p, params, ROBUST_NOTES[p], tag=f'pr{wid}')
            comp['takes_hard'], comp['takes_soft'], comp['robust_bad'] = h, s, bf
            comp['total'] += max(0.0, h - 0.05) / 0.01 + max(0.0, s - 0.12) / 0.02 + 3.0 * bf / 0.05
        return comp['total'], comp


def fit(inst, run, gens=200, pop=20, sigma=0.2, workers=5, every=2, start=None, seed=7, only=None):
    base = {k: s[0] for k, s in SPACE.items()}
    if start:
        base.update({k: v for k, v in json.load(open(start))['params'].items() if k in SPACE})
    return Fit(workdir('fits', run), Space(SPACE), Scorer(env.renderer(), inst, every, inst == 'piccolo'), base, sigma=sigma, pop=pop,
               seed=seed, workers=workers, only=only, fmt=objective(inst).fmt).run(gens)


def bake(header, flute_json, alto_json, gain_flute, gain_alto):
    """the fitted values into voice_flute.h: flute_params() every fitted field; alto_params() the
    flute's, its geometry (bore_mm, low_note) and its own fit"""
    from ..bake import literal, param_fields
    s = open(header, encoding='utf-8').read()
    table = param_fields(s)
    fp = json.load(open(flute_json))['params']
    fp['gain'] = float(gain_flute)
    ap = json.load(open(alto_json))['params']
    ap['gain'] = float(gain_alto)
    m = re.search(r'(static inline Params flute_params\(\) \{\n\tParams p;\n)(.*?)(\n\treturn p;\n\})', s, re.S)
    fields = {table[k]: v for k, v in fp.items()}
    new = []
    for ln in m.group(2).split('\n'):
        mm = re.match(r'\tp\.(\w+) = .*;$', ln)
        new.append(f"\tp.{mm.group(1)} = {literal(fields[mm.group(1)], 'g6')};" if mm and mm.group(1) in fields else ln)
    s = s[:m.start(2)] + '\n'.join(new) + s[m.end(2):]
    m = re.search(r'(static inline Params alto_params\(\) \{\n\tParams p = flute_params\(\);\n)(.*?)(\n\treturn p;\n\})', s, re.S)
    keep = [ln for ln in m.group(2).split('\n') if re.match(r'\tp\.(bore_mm|low_note) = ', ln)]
    s = s[:m.start(2)] + '\n'.join(keep + [f"\tp.{table[k]} = {literal(v, 'g6')};" for k, v in ap.items()]) + s[m.end(2):]
    open(header, 'w', encoding='utf-8', newline='\n').write(s)
