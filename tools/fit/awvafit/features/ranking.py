# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Distances for ranking builds against a reference, tested against listener verdicts (verdicts.py,
the back-test; the research's Step 0, docs/fitting.md). Each takes two Notes (note()): lower is closer.

What to use, as the back-test found it:
  attack    the 1 ms attack envelope: the first 150 ms in dB re the sustain (2 ms RMS, 1 ms hop,
            floored at -50), mean |difference|. Decisive at soft strikes (10-12 of 12 notes
            on the trumpet verdicts). Against real recordings at medium and hard strikes it prefers the
            rejected build: use it against an approved reference, or at soft strikes.
  jtfs      joint time-frequency scattering (kymatio, BSD-3; J=12, J_fr=3, Q=(8, 2), global averaging):
            RMS difference of the log coefficients. The best timbre ranker against real recordings
            (all four trumpet pairs right against the Iowa trumpet, the stage-2 margins small).
  mrstft    multi-resolution STFT (256..4096): spectral convergence + mean |log magnitude| difference.
            Secondary: right on the old-trumpet pairs, blind to stage 2.
  noise     noise per pitch period: nhr_hp (the residual above 1.5 kHz re the note, dB) and psm (how
            pulsed it is within the period). A per-dynamic diagnostic, not a ranker: report it, gated on
            the recording's noise floor (noise_floor(); a residual at the floor is the room's hiss).
Not rankers against real recordings (they rank the listener verdicts wrong):
  lanes     the lanes' frame-wise objective terms on a note: they prefer the rejected old trumpet
            (closer on 10-11 of 12 notes), though against the hybrid they rank it last.
  atk_noise attack HF noise (1.5-10 kHz share in the first 40 ms re the sustain's): it runs opposite to
            the verdicts (the approved trumpets have an HF burst at pp the real trumpet lacks).
  (an audio-codec embedding distance, DAC: 5-8 of 12 against recordings; not implemented here)

  n = note(x, onset, midi, dyn)          a Note, cut at the onset and normalised on its sustain
  attack(a, b); jtfs(a, b); mrstft(a, b); noise(a, b); lanes(a, b); atk_noise_d(a, b)
  noise_per_period(n) -> (nhr_hp dB, psm, the phase profile)
"""
import numpy as np
from scipy.signal import butter, sosfiltfilt

SR = 48000
PRE = int(0.02 * SR)
SUS0, SUS1 = 0.35, 1.25   # the sustain window, s after the onset
LEN = 1.4                 # how much of a note is kept after its onset, s
SOS_HP = butter(6, 1500, 'hp', fs=SR, output='sos')
SOS_BP = butter(4, [1500, 10000], 'bp', fs=SR, output='sos')


def env_db(x, win=96, hop=48):
    """RMS envelope in dB, 2 ms windows, 1 ms hop"""
    n = 1 + max(0, (len(x) - win) // hop)
    idx = np.arange(n)[:, None] * hop + np.arange(win)[None, :]
    return 10 * np.log10(np.mean(x[idx] ** 2, 1) + 1e-14)


def onset(x, a, b):
    """the first sample in [a, b) where the 2 ms RMS is within 40 dB of the region's peak"""
    e = env_db(x[a:b])
    return a + int(np.argmax(e > e.max() - 40)) * 48


def f0_of(x, f_exp):
    """f0 of a steady segment: normalised ACF peak near the expected period, parabolic refinement"""
    x = x - x.mean()
    n = len(x)
    r = np.fft.irfft(np.abs(np.fft.rfft(x, 2 * n)) ** 2)[:n]
    r /= r[0] + 1e-12
    t = SR / f_exp
    lo, hi = int(t / 1.06), int(t * 1.06) + 2
    k = lo + int(np.argmax(r[lo:hi]))
    a, b, c = r[k - 1], r[k], r[k + 1]
    den = a - 2 * b + c
    d = 0.5 * (a - c) / den if den < 0 else 0.0
    return SR / (k + d), float(b)


def note(x, on, midi, dyn):
    """a Note: dict(x: from 20 ms before the onset, the sustain at RMS 0.1; on; midi; dyn; f0; per;
    cents; clip: the fraction of |x| > 0.99 before normalising), or None if too short"""
    a = on - PRE
    y = np.asarray(x[max(a, 0):a + PRE + int(LEN * SR)], np.float64).copy()
    if a < 0:
        y = np.concatenate([np.zeros(-a), y])
    if len(y) < PRE + int(SUS1 * SR):
        return None
    clip = float(np.mean(np.abs(y) > 0.99))
    s = y[PRE + int(SUS0 * SR):PRE + int(SUS1 * SR)]
    g = 0.1 / (np.sqrt(np.mean(s ** 2)) + 1e-12)
    y *= g
    f_exp = 440.0 * 2 ** ((midi - 69) / 12)
    f0, per = f0_of(s * g, f_exp)
    return dict(x=y, on=PRE, midi=midi, dyn=dyn, f0=f0, per=per, cents=1200 * np.log2(f0 / f_exp), clip=clip)


# --- attack ------------------------------------------------------------------------------------------


def attack_curve(n):
    x = n['x']
    a, b = n['on'] + int(SUS0 * SR), n['on'] + int(SUS1 * SR)
    sus = 10 * np.log10(np.mean(x[a:b] ** 2) + 1e-14)
    e = env_db(x[n['on'] - 96:n['on'] + int(0.152 * SR)])[:150] - sus
    return np.maximum(e, -50.0)


def attack(a, b):
    return float(np.mean(np.abs(cached(a, 'ac', attack_curve) - cached(b, 'ac', attack_curve))))


def rise_ms(n):
    """ms from the onset to within 3 dB of the sustain (150 if it doesn't get there)"""
    e = cached(n, 'ac', attack_curve)
    k = int(np.argmax(e >= -3.0))
    return float(k) if e[k] >= -3.0 else 150.0


def early_level(n, ms=80):
    """the first `ms` ms's level re the sustain (dB)"""
    c = cached(n, 'ac', attack_curve)
    return float(10 * np.log10(np.mean(10 ** (c[:ms] / 10))))


def atk_noise(n):
    x = sosfiltfilt(SOS_BP, n['x'])
    t = n['x']
    o = n['on']
    a, b = o + int(SUS0 * SR), o + int(SUS1 * SR)
    w = slice(o, o + int(0.04 * SR))
    share_a = np.sum(x[w] ** 2) / (np.sum(t[w] ** 2) + 1e-14)
    share_s = np.sum(x[a:b] ** 2) / (np.sum(t[a:b] ** 2) + 1e-14)
    return float(10 * np.log10(share_a + 1e-12) - 10 * np.log10(share_s + 1e-12))


def atk_noise_d(a, b):
    return abs(cached(a, 'an', atk_noise) - cached(b, 'an', atk_noise))


# --- MR-STFT -----------------------------------------------------------------------------------------


def _stft(x, n):
    hop = n // 4
    w = np.hanning(n)
    nfr = 1 + (len(x) - n) // hop
    idx = np.arange(nfr)[:, None] * hop + np.arange(n)[None, :]
    return np.abs(np.fft.rfft(x[idx] * w, axis=1))


def mrstft(a, b):
    L = int(1.25 * SR)
    xa, xb = a['x'][a['on'] - 480:a['on'] + L], b['x'][b['on'] - 480:b['on'] + L]
    d = 0.0
    for n in (256, 512, 1024, 2048, 4096):
        A, B = _stft(xa, n), _stft(xb, n)
        m = min(len(A), len(B))
        A, B = A[:m], B[:m]
        d += np.linalg.norm(A - B) / (np.linalg.norm(B) + 1e-9) + np.mean(np.abs(np.log(A + 1e-5) - np.log(B + 1e-5)))
    return d / 5


# --- noise per pitch period ------------------------------------------------------------------------


def f0_track(x, f_exp, hop=240, win=1920):
    out = []
    t = SR / f_exp
    lo, hi = int(t / 1.08), int(t * 1.08) + 2
    for i in range(0, len(x) - win, hop):
        s = x[i:i + win] * np.hanning(win)
        r = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * win)) ** 2)[:win]
        k = lo + int(np.argmax(r[lo:hi]))
        a_, b_, c_ = r[k - 1], r[k], r[k + 1]
        den = a_ - 2 * b_ + c_
        d = 0.5 * (a_ - c_) / den if den < 0 else 0.0
        out.append((i + win / 2, SR / (k + d)))
    tt, ff = np.array(out).T
    return np.interp(np.arange(len(x)), tt, ff)


def period_matrix(y, f0s, nb=64):
    ph = np.cumsum(f0s) / SR  # cycles
    K = int(ph[-1]) - 1
    grid = (np.arange(1, K)[:, None] + (np.arange(nb)[None, :] + 0.5) / nb).ravel()
    return np.interp(grid, ph, y).reshape(K - 1, nb)


def noise_per_period(n):
    """(nhr_hp dB, psm, the residual's energy profile over the period) on the sustain window: the
    residual above 1.5 kHz after removing a moving average over +-4 periods, its energy re the note,
    and its coefficient of variation over 64 phase bins minus the same with phases scrambled (0 =
    steady noise, higher = pulsed). It mixes turbulence with cycle-to-cycle variation of the high
    harmonics: both are aperiodicity, not the same thing."""
    x = n['x']
    a, b = n['on'] + int(SUS0 * SR), n['on'] + int(SUS1 * SR)
    seg = x[a:b]
    f0s = f0_track(seg, n['f0'])
    yh = sosfiltfilt(SOS_HP, x)[a:b]
    P = period_matrix(seg, f0s)
    H = period_matrix(yh, f0s)
    k = 4
    ker = np.ones(2 * k + 1) / (2 * k + 1)
    D = np.apply_along_axis(lambda c: np.convolve(c, ker, 'same'), 0, H)
    R = (H - D)[k:-k]
    Pm = P[k:-k]
    nhr = 10 * np.log10(np.mean(R ** 2) / (np.mean(Pm ** 2) + 1e-14) + 1e-14)
    e = np.mean(R ** 2, 0)
    cv = np.std(e) / (np.mean(e) + 1e-20)
    rng = np.random.default_rng(0)
    Rs = np.stack([np.roll(r, rng.integers(R.shape[1])) for r in R])
    es = np.mean(Rs ** 2, 0)
    cv0 = np.std(es) / (np.mean(es) + 1e-20)
    return float(nhr), float(cv - cv0), e / (np.mean(e) + 1e-20)


def noise(a, b):
    fa, fb = cached(a, 'psn', noise_per_period), cached(b, 'psn', noise_per_period)
    return abs(fa[0] - fb[0]) / 3.0 + abs(fa[1] - fb[1]) / 0.1


def noise_floor(x, segs, sr=SR, skip=0.6, lead=0.1):
    """a recording's HF (> 1.5 kHz) floor in the gaps between its notes (dB, re full scale): segs are
    the notes' (start, end) samples; each gap skips `skip` s of decay and stops `lead` s before the
    next note. None if there are no gaps of 0.2 s."""
    h = sosfiltfilt(butter(6, 1500, 'hp', fs=sr, output='sos'), x)
    gaps = []
    for (a0, b0), (a1, b1) in zip(segs[:-1], segs[1:]):
        g0, g1 = b0 + int(skip * sr), a1 - int(lead * sr)
        if g1 - g0 > int(0.2 * sr):
            gaps.append(h[g0:g1])
    return float(10 * np.log10(np.mean(np.concatenate(gaps) ** 2) + 1e-20)) if gaps else None


# --- JTFS (optional: torch and kymatio) ------------------------------------------------------------

_jt = {}


def jtfs_vec(n):
    import torch
    from kymatio.torch import TimeFrequencyScattering
    N = 2 ** 16
    if 'S' not in _jt:
        dev = 'cuda' if torch.cuda.is_available() else 'cpu'
        _jt['S'] = TimeFrequencyScattering(J=12, J_fr=3, Q=(8, 2), shape=(N,), T='global', F='global', format='time').to(dev)
        _jt['dev'] = dev
    x = n['x'][n['on'] - 480:n['on'] - 480 + N]
    x = np.pad(x, (0, N - len(x)))
    with torch.no_grad():
        S = _jt['S'](torch.tensor(x, dtype=torch.float32, device=_jt['dev'])[None])
    v = S.squeeze().flatten().cpu().numpy()
    return np.log(np.maximum(v, 0.0) + 1e-6)


def jtfs(a, b):
    va, vb = cached(a, 'jt', jtfs_vec), cached(b, 'jt', jtfs_vec)
    return float(np.sqrt(np.mean((va - vb) ** 2)))


def jtfs_available():
    try:
        import kymatio.torch  # noqa: F401
        from kymatio.torch import TimeFrequencyScattering  # noqa: F401
        return True
    except Exception:
        return False


# --- the lanes' objective terms on a note (not a ranker against recordings) ---------------------------


def lanes_feats(n):
    from ..instruments import brassfamily
    from ..terms import framewise
    from .frames import BLOCK, FrameAnalysis
    if 'A' not in _jt:
        _jt['A'] = FrameAnalysis(80.0)
    nb = len(n['x']) // BLOCK + 1
    vc = np.zeros((nb, 10, 5), np.float32)
    b0, b1 = n['on'] // BLOCK, (n['on'] + int(SUS1 * SR)) // BLOCK
    vc[b0:b1, 0, 0] = n['midi']
    vc[b0:b1, 0, 1] = 0.5
    F = _jt['A'].analyse(n['x'].astype(np.float32), vc)
    e = F['events'][0]
    a, b = e['f_on'], e['f_off']
    spec_l = F['mel_l'] - F['lvl'][:, None]
    spec_s = F['mel_s'] - F['lvl'][:, None]
    r = dict(sa=spec_s[a:a + framewise.ATK].mean(0), ss=spec_l[a + 25:b].mean(0),
             ha=np.nanmean(framewise.hnr(F['per'][a:a + framewise.ATK])), hs=np.nanmedian(framewise.hnr(F['per'][a + 25:b])),
             f0=np.nanmedian(F['f0c'][a + 25:b]))
    r.update(framewise.note_feats(F, e, len(F['lvl'])))
    r['_obj'] = brassfamily.OBJECTIVE
    return r


def lanes(a, b):
    A, B = cached(a, 'lanes', lanes_feats), cached(b, 'lanes', lanes_feats)
    S, W = A['_obj'].scale, A['_obj'].weight
    terms = dict(spec_a=np.sqrt(np.mean((A['sa'] - B['sa']) ** 2)), spec_s=np.sqrt(np.mean((A['ss'] - B['ss']) ** 2)),
                 hnr_a=abs(A['ha'] - B['ha']), hnr_s=abs(A['hs'] - B['hs']), pitch=min(abs(A['f0'] - B['f0']), 100.0))
    curve = np.nanmean([abs(A[k] - B[k]) for k in ('a_br', 's_br', 'blat')] + [abs(A['t_rise'] - B['t_rise']) / 10.0])
    num = sum(W[k] * terms[k] / S[k] for k in terms) + W['curve'] * curve / S['curve']
    den = sum(W[k] for k in terms) + W['curve']
    return float(num / den)


def cached(n, key, f):
    if key not in n:
        n[key] = f(n)
    return n[key]


METRICS = {'attack': attack, 'jtfs': jtfs, 'mrstft': mrstft, 'noise': noise, 'lanes': lanes, 'atk_noise': atk_noise_d}
