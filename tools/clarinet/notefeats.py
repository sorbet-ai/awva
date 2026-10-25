# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Per-note features of a monophonic note (recording or model render), the same code for both.

  note_features(x, sr, f0_nominal, t_on=None, t_off=None) -> dict
    lvl      sustain RMS level (dB re full scale 1.0)
    harm     harmonic amplitudes 1..NH re the total sustain level (dB), NaN above 11 kHz
    nhr      noise (between the harmonics) re the harmonics, 4 bands (dB)
    cents    sustain pitch re the nominal (cents)
    rise     attack time 10 % -> 90 % of the sustain RMS (ms)
    a_cent   attack centroid (first 40 ms after the 10 % point) re the sustain centroid (octaves)
    a_harm   harmonic amplitudes 1..8 in the attack window re its level (dB)
    s_cent   sustain spectral centroid (Hz)
    rel      release time from the note end (90 % -> -40 dB of the sustain) (ms)
"""
import numpy as np

NH = 24
BANDS = ((0, 1500), (1500, 4000), (4000, 8000), (8000, 14000))
FMAX = 11000.0


def env(x, sr, ms=2.0):
    n = max(int(sr * ms / 1000), 1)
    m = len(x) // n
    e = np.sqrt(np.mean(x[:m * n].reshape(m, n) ** 2, axis=1) + 1e-20)
    return e, n


def segments(x, sr, floor_db=-50.0, min_gap=0.06, min_len=0.25):
    """Note regions of a scale recording: envelope above the floor (re the file's 99th percentile)."""
    e, n = env(x, sr, 10.0)
    ref = np.percentile(e, 99)
    on = 20 * np.log10(e / ref) > floor_db
    regs = []
    i = 0
    while i < len(on):
        if on[i]:
            j = i
            while j < len(on) and on[j]:
                j += 1
            regs.append([i, j])
            i = j
        else:
            i += 1
    # merge across short gaps
    out = []
    for r in regs:
        if out and (r[0] - out[-1][1]) * 0.01 < min_gap:
            out[-1][1] = r[1]
        else:
            out.append(r)
    out = [(a * n / sr, b * n / sr) for a, b in out if (b - a) * 0.01 >= min_len]
    return out


def _spec(seg, sr, nfft):
    w = np.hanning(nfft)
    hop = nfft // 4
    if len(seg) < nfft:
        seg = np.concatenate([seg, np.zeros(nfft - len(seg))])
    nfr = 1 + (len(seg) - nfft) // hop
    S = np.zeros(nfft // 2 + 1)
    for k in range(nfr):
        S += np.abs(np.fft.rfft(seg[k * hop:k * hop + nfft] * w)) ** 2
    S /= nfr * (w ** 2).sum()
    return S


def refine_f0(S, sr, nfft, f0):
    """Pitch from the strongest of the first 6 harmonics near h f0 (+-4 %), parabolic peaks,
    weighted by their energy (the fundamental's search first places the comb)."""
    df = sr / nfft
    est, wts = [], []
    for h in range(1, 7):
        lo, hi = int(h * f0 * 0.96 / df), int(h * f0 * 1.04 / df) + 1
        if hi + 2 >= len(S) or lo < 2:
            break
        k = lo + int(np.argmax(S[lo:hi]))
        a, b, c = np.log(S[k - 1:k + 2] + 1e-30)
        den = a - 2 * b + c
        d = 0.5 * (a - c) / den if den < 0 else 0.0
        est.append((k + max(min(d, 0.5), -0.5)) * df / h)
        wts.append(S[k])
    if not est:
        return f0
    est, wts = np.array(est), np.array(wts)
    # the dominant harmonics only (a weak one's peak is mostly noise)
    m = wts >= 0.01 * wts.max()
    return float(np.sum(est[m] * wts[m]) / np.sum(wts[m]))


def harmonics(S, sr, nfft, f0, nh=NH):
    df = sr / nfft
    amp = np.full(nh, np.nan)
    for h in range(1, nh + 1):
        f = h * f0
        if f > FMAX:
            break
        k = int(round(f / df))
        r = max(2, int(0.03 * f / df))
        lo, hi = max(k - r, 1), min(k + r + 1, len(S))
        amp[h - 1] = S[lo:hi].sum()  # energy of the harmonic's lobe
    return amp


def noise_ratio(S, sr, nfft, f0, total=None):
    """Noise between the harmonics (the middle 50 % of each gap, as a density over the band) re the
    note's total power, per band (dB). total: the note's power in the same units (default S's)."""
    df = sr / nfft
    f = np.arange(len(S)) * df
    ph = (f / f0) % 1.0
    between = (ph > 0.25) & (ph < 0.75) & (f > 0.6 * f0)
    if total is None:
        total = S[(f >= 60) & (f <= 14000)].sum()
    out = []
    for lo, hi in BANDS:
        b = (f >= lo) & (f < hi)
        nb = S[b & between].mean() * b.sum() if (b & between).any() else 1e-30
        out.append(10 * np.log10(nb / (total + 1e-30) + 1e-12))
    return np.array(out)


def centroid(S, sr, nfft, lo=60.0, hi=FMAX):
    f = np.arange(len(S)) * sr / nfft
    m = (f >= lo) & (f <= hi)
    return float((S[m] * f[m]).sum() / (S[m].sum() + 1e-30))


def note_features(x, sr, f0, t_on=None, t_off=None, nfft=None):
    x = np.asarray(x, np.float64)
    if t_on is None:
        t_on = 0.0
    if t_off is None:
        t_off = len(x) / sr
    a, b = int(t_on * sr), min(int(t_off * sr), len(x))
    note = x[a:b]
    e, n = env(note, sr, 2.0)
    dur = len(note) / sr
    # sustain: from 35 % to 85 % of the note (at least 0.15 s after the start)
    s0 = int(sr * max(0.15, 0.35 * dur))
    s1 = int(sr * 0.85 * dur)
    if s1 - s0 < int(0.1 * sr):
        s0, s1 = int(0.3 * len(note)), int(0.9 * len(note))
    sus = note[s0:s1]
    if nfft is None:
        nfft = 8192 if sr > 40000 else 4096
        while nfft > len(sus) and nfft > 1024:
            nfft //= 2
    S = _spec(sus, sr, nfft)
    fr = refine_f0(S, sr, nfft, f0)
    rms = np.sqrt(np.mean(sus ** 2) + 1e-20)
    out = {'lvl': 20 * np.log10(rms), 'f0': fr, 'cents': 1200 * np.log2(fr / f0)}
    fq = np.arange(len(S)) * sr / nfft
    out['_tot'] = float(S[(fq >= 60) & (fq <= 14000)].sum())
    H = harmonics(S, sr, nfft, fr)
    out['_H'] = H
    out['hshare'] = float(np.nansum(H) / (out['_tot'] + 1e-30)) if '_tot' in out else 1.0
    out['nfft'] = nfft
    out['harm'] = 10 * np.log10(H / (np.nansum(H) + 1e-30) + 1e-12)
    out['nhr'] = noise_ratio(S, sr, nfft, fr)
    out['s_cent'] = centroid(S, sr, nfft)
    # attack: 10 % and 90 % of the sustain RMS on the 2 ms envelope
    lv = rms
    i10 = int(np.argmax(e >= 0.1 * lv))
    i90 = int(np.argmax(e >= 0.9 * lv))
    if i90 < i10:
        i90 = i10
    out['rise'] = (i90 - i10) * 2.0
    out['t10'] = t_on + i10 * n / sr
    aw = note[i10 * n:i10 * n + int(0.04 * sr)]
    if len(aw) >= 512:
        SA = _spec(aw, sr, 1024)
        out['a_cent'] = float(np.log2(centroid(SA, sr, 1024) / out['s_cent']))
        HA = harmonics(SA, sr, 1024, fr, 8)
        out['a_harm'] = 10 * np.log10(HA / (np.nansum(HA) + 1e-30) + 1e-12)
    else:
        out['a_cent'] = 0.0
        out['a_harm'] = np.full(8, np.nan)
    # release: after t_off, time to fall 40 dB below the sustain
    tail = x[b:b + int(1.0 * sr)]
    if len(tail) > n * 4:
        et, _ = env(tail, sr, 2.0)
        k = np.where(et < lv * 0.01)[0]
        out['rel'] = float(k[0] * 2.0) if len(k) else 1000.0
    else:
        out['rel'] = np.nan
    return out
