# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""A model render (M) against a reference render (H) on the same controls, frame by frame
(features.frames). Components (each in its own unit; the Objective scales and weighs them):

  gain     |global level offset| (dB): the median of M - H over H's loud held frames
  lvl      level envelope error after the offset (dB, loudness weighted)
  silence  level where H is quiet and nothing is held but M sounds (dB above H's floor): tails
  spec_a/s/r  spectral shape (log-mel minus frame level, floored at -60), RMS over bands (dB), on
           attack (first ATK frames of a note) / sustain / release (REL frames after) frames, the
           mean of the short and long windows
  pitch    |f0 difference| on monophonic periodic frames (cents, clipped 100)
  voicing  fraction of held monophonic sustain frames periodic in H, not in M (or the reverse)
  hnr_a/s  |HNR difference| (dB) on monophonic attack / sustain frames (HNR from the periodicity)
  robust   fraction of sustained monophonic frames off pitch (> 60 c), period doubled, or broken
  curve    per isolated note: attack peak, sustain level, attack and sustain brightness, rise
           time, blat (attack peak - sustain): mean |M - H| (levels after the offset; 10 ms ~ 1 dB)
  flat     insensitivity: per feature |log(std_M / std_H)| over a file's notes (punishes a model
           that wins by flattening the dynamics or the brightness response)
  tilt, lowmid  with tilt=True: the level-dependent spectral tilt (FrameAnalysis.tilt_bins), mean
           |M - H| over H's level quartiles: the slope (dB/oct) and the low-mid re presence (dB)
Private: _g (the offset, dB), _notes (isolated notes compared).
"""
import numpy as np

ATK = 8      # attack frames (80 ms)
REL = 20     # release frames (200 ms)
CURVE_FEATS = ('a_pk', 's_lvl', 'a_br', 's_br', 't_rise', 'blat')


def hnr(per):
    """harmonic-to-noise ratio (dB) from a normalised ACF peak, clipped to -5 .. 30"""
    p = np.clip(per, 1e-3, 0.999)
    return np.clip(10 * np.log10(p / (1 - p)), -5, 30)


def note_feats(F, e, nfr):
    """an isolated note's expression features: attack peak and brightness, sustain level and
    brightness (from 250 ms), blat, rise (ms to within 3 dB of the first 300 ms's peak)"""
    a, b = e['f_on'], min(e['f_off'], nfr)
    if b - a < 6:
        return None
    L, B = F['lvl'], F['bright']
    w = slice(a, min(a + ATK, b))
    r = {}
    r['a_pk'] = float(L[w].max())
    lw = 10 ** (L[w] / 10)
    r['a_br'] = float((B[w] * lw).sum() / lw.sum())
    if b - a >= 35:
        r['s_lvl'] = float(np.median(L[a + 25:b]))
        r['s_br'] = float(np.median(B[a + 25:b]))
        r['blat'] = r['a_pk'] - r['s_lvl']
    else:
        r['s_lvl'] = r['s_br'] = r['blat'] = np.nan
    seg = L[a:min(a + 30, b)]
    pk = seg.max()
    r['t_rise'] = float(10.0 * np.argmax(seg >= pk - 3.0))
    return r


def isolated(evs, i):
    """no other voice held from 50 ms before the onset to the end of the attack window"""
    e = evs[i]
    for j, o in enumerate(evs):
        if j == i:
            continue
        if o['f_on'] < e['f_on'] + ATK and o['f_off'] > e['f_on'] - 5:
            return False
    return True


def compare(H, M, objective, tilt=False, analysis=None, max_peak=8.0):
    """the components of M against H; a render that blew up (non-finite, or a peak over max_peak)
    gets objective.failed()"""
    c = {}
    nfr = min(len(H['lvl']), len(M['lvl']))
    LH, LM = H['lvl'][:nfr], M['lvl'][:nfr]
    if not M['finite'] or M['peak'] > max_peak:
        return objective.failed()
    top = LH.max()
    loud = (LH > top - 40) & H['held'][:nfr]
    g = float(np.median((LM - LH)[loud])) if loud.any() else 0.0
    c['gain'] = abs(g)
    wH = np.clip((LH - (top - 45)) / 25, 0, 1)
    wM = np.clip((LM - g - (top - 45)) / 25, 0, 1)
    w = np.maximum(wH, wM)
    dL = np.clip(LM - g - LH, -30, 30)
    c['lvl'] = float((w * np.abs(dL)).sum() / (w.sum() + 1e-9))
    quiet = (LH < top - 50) & ~H['held'][:nfr]
    c['silence'] = float(np.mean(np.clip(LM[quiet] - g - (top - 50), 0, 40))) if quiet.any() else 0.0
    # segments: 0 attack, 1 sustain, 2 release
    seg = np.full(nfr, 1)
    for e in H['events']:
        seg[e['f_on']:min(e['f_on'] + ATK, nfr)] = 0
    held = H['held'][:nfr]
    after_rel = np.zeros(nfr, bool)
    for e in H['events']:
        after_rel[e['f_off']:min(e['f_off'] + REL, nfr)] = True
    seg[(~held) & after_rel] = 2
    spec = []
    for key in ('mel_s', 'mel_l'):
        SH = H[key][:nfr] - H['lvl'][:nfr, None]
        SM = M[key][:nfr] - M['lvl'][:nfr, None]
        SH = np.maximum(SH, -60)
        SM = np.maximum(SM, -60)
        spec.append(np.sqrt(np.mean((SM - SH) ** 2, axis=1)))
    sd = 0.5 * (spec[0] + spec[1])
    for k, name in ((0, 'spec_a'), (1, 'spec_s'), (2, 'spec_r')):
        m = (seg == k) & (w > 0)
        c[name] = float((w[m] * sd[m]).sum() / (w[m].sum() + 1e-9)) if m.any() else 0.0
    # pitch, voicing, HNR, robustness (monophonic frames)
    mono = H['mono'][:nfr] & (wH > 0.2)
    pH, pM = H['per'][:nfr], M['per'][:nfr]
    fH, fM = H['f0c'][:nfr], M['f0c'][:nfr]
    both = mono & (pH > 0.6) & (pM > 0.3) & np.isfinite(fH) & np.isfinite(fM)
    c['pitch'] = float(np.mean(np.minimum(np.abs(fM[both] - fH[both]), 100))) if both.any() else 0.0
    sus = mono & (seg == 1)
    vm = sus & (((pH > 0.7) & (pM < 0.5)) | ((pM > 0.7) & (pH < 0.5)))
    c['voicing'] = float(vm.sum() / max(sus.sum(), 1))
    hH, hM = hnr(pH), hnr(pM)
    for k, name in ((0, 'hnr_a'), (1, 'hnr_s')):
        m = mono & (seg == k)
        c[name] = float(np.mean(np.abs(hM[m] - hH[m]))) if m.any() else 0.0
    bad = sus & ((pH > 0.7) & ((np.abs(np.nan_to_num(fM, nan=999)) > 60) | (M['sub'][:nfr] > 0.15) | (pM < 0.4)))
    c['robust'] = float(bad.sum() / max(sus.sum(), 1))
    # expression curves on isolated notes
    evs = H['events']
    dev, fl = [], []
    rows = []
    for i, e in enumerate(evs):
        if not isolated(evs, i):
            continue
        nh, nm = note_feats(H, e, nfr), note_feats(M, e, nfr)
        if nh is None or nm is None:
            continue
        rows.append((nh, nm))
    if len(rows) >= 3:
        for f in CURVE_FEATS:
            h = np.array([r[0][f] for r in rows])
            m = np.array([r[1][f] for r in rows])
            ok = np.isfinite(h) & np.isfinite(m)
            if ok.sum() < 3:
                continue
            off = g if f in ('a_pk', 's_lvl') else 0.0
            sc = 0.1 if f == 't_rise' else 1.0  # 10 ms of rise time ~ 1 dB
            dev.append(np.mean(np.abs(m[ok] - off - h[ok])) * sc)
            sh, sm = np.std(h[ok]), np.std(m[ok])
            floor = 3.0 if f == 't_rise' else 0.5
            fl.append(abs(np.log((sm + floor) / (sh + floor))))
    c['curve'] = float(np.mean(dev)) if dev else 0.0
    c['flat'] = float(np.mean(fl)) if fl else 0.0
    if tilt:
        th, tm = analysis.tilt_bins(H, H), analysis.tilt_bins(M, H)
        if th is None or tm is None:
            c['tilt'] = c['lowmid'] = 0.0
        else:
            d = np.abs(np.array(tm) - np.array(th))
            d = np.where(np.isfinite(d), d, 0.0)
            c['tilt'], c['lowmid'] = float(d[:, 0].mean()), float(d[:, 1].mean())
    c['_g'] = g
    c['_notes'] = len(rows)
    return c


def sustain_pitch(F, H, skip=25, min_per=0.5):
    """per note event of H held >= skip + 15 frames: (note, the median pitch in cents of F's periodic
    monophonic frames from `skip` frames after the onset (250 ms) to the release)"""
    out = []
    nfr = min(len(F['f0c']), len(H['f0c']))
    for e in H['events']:
        a, b = e['f_on'] + skip, min(e['f_off'], nfr)
        if b - a < 15:
            continue
        m = H['mono'][a:b] & (F['per'][a:b] > min_per) & np.isfinite(F['f0c'][a:b])
        if m.sum() < 10:
            continue
        out.append((e['note'], float(np.median(F['f0c'][a:b][m]))))
    return out
