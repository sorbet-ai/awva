# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The standard per-note measures: the same code for a recording and a render, at any sample rate.
A note is the samples x, its sounding note (MIDI, fractional), its onset and release times (s), and
for a render the time of the key (t_key: the control's onset). docs/fitting.md defines each.

  harmonics(x, sr, f0, nh)          harmonic amplitudes 1..nh (dB re the harmonic total), the spectral
                                    envelope at the harmonics; NaN above the band limit
  envelope(x, sr, bands)            the spectral envelope in log bands (dB re the total)
  hnr(x, sr, f0)                    harmonic-to-noise ratio (dB): overall and per band
  brightness(x, sr, f=1500)         energy above f re the total (dB); centroid(x, sr) (Hz)
  tilt(amps_db, f0)                 spectral tilt: slope of the harmonic amplitudes over log2 frequency
                                    (dB per octave), harmonics from 1.5 f0 to 8 kHz
  pitch_track(x, sr, note)          per 10 ms frame: pitch in cents re the note, periodicity (0..1)
  sustain_pitch(track, ...)         the median cents of the periodic frames from `skip` s on
  settle_time(track, ...)           s from the onset until the pitch stays within tol cents of its median
  attack(x, sr, onset, t_key)       latency (key to first sound, -40 dB re the sustain), rise (-30 dB to
                                    -3 dB re the sustain), the overshoot (dB)
  note_measures(x, sr, note, onset, off, t_key=None)    every one of them, a dict
  dynamics_curve(levels)            {dynamic: level re the loudest dynamic, per note} and the means
"""
import numpy as np

HOP_S = 0.01
FMAX = 12000.0
BANDS = np.geomspace(100.0, 12000.0, 21)
HNR_BANDS = ((0, 1000), (1000, 3000), (3000, 6000), (6000, 12000))


def note_hz(n):
    return 440.0 * 2 ** ((np.asarray(n, float) - 69) / 12)


def power_spectrum(x, sr, nfft=None):
    """averaged Hann power spectrum (frames of nfft, hop nfft/4), and its bin spacing"""
    x = np.asarray(x, np.float64)
    if nfft is None:
        nfft = 1 << int(np.clip(np.ceil(np.log2(0.085 * sr)), 10, 15))  # ~85 ms
        while nfft > max(len(x), 1024) and nfft > 1024:
            nfft //= 2
    if len(x) < nfft:
        x = np.concatenate([x, np.zeros(nfft - len(x))])
    w = np.hanning(nfft)
    hop = nfft // 4
    nfr = 1 + (len(x) - nfft) // hop
    S = np.zeros(nfft // 2 + 1)
    for k in range(nfr):
        S += np.abs(np.fft.rfft(x[k * hop:k * hop + nfft] * w)) ** 2
    return S / (nfr * (w ** 2).sum()), sr / nfft


def _lobe(S, df, f, f0):
    k = int(round(f / df))
    r = max(2, int(0.03 * f / df), int(min(0.25 * f0, 2.5 * df) / df))
    return S[max(k - r, 1):min(k + r + 1, len(S))].sum()


def harmonics(x, sr, f0, nh=24, fmax=FMAX, S=None):
    """harmonic amplitudes 1..nh in dB re the sum of the measured harmonics (NaN above fmax)"""
    if S is None:
        S, df = power_spectrum(x, sr)
    else:
        S, df = S
    amp = np.full(nh, np.nan)
    for h in range(1, nh + 1):
        f = h * f0
        if f > min(fmax, 0.45 * sr):
            break
        amp[h - 1] = _lobe(S, df, f, f0)
    return 10 * np.log10(amp / (np.nansum(amp) + 1e-30) + 1e-12)


def envelope(x, sr, bands=BANDS, S=None):
    """energy per log band re the total in bands[0]..bands[-1] (dB)"""
    S, df = S if S is not None else power_spectrum(x, sr)
    f = np.arange(len(S)) * df
    tot = S[(f >= bands[0]) & (f < bands[-1])].sum() + 1e-30
    return np.array([10 * np.log10(S[(f >= a) & (f < b)].sum() / tot + 1e-12) for a, b in zip(bands[:-1], bands[1:])])


def hnr(x, sr, f0, bands=HNR_BANDS, S=None):
    """harmonic-to-noise ratio (dB): harmonic lobes vs the median floor between harmonics (0.3-0.7 of
    the way), overall and per band (NaN where no harmonic falls)"""
    S, df = S if S is not None else power_spectrum(x, sr)
    f = np.arange(len(S)) * df
    ph = (f / f0) % 1.0
    away = (ph > 0.3) & (ph < 0.7) & (f > 0.6 * f0)
    hz = np.zeros(len(S), bool)
    k = 1
    while k * f0 < min(FMAX, 0.45 * sr):
        hz |= np.abs(f - k * f0) <= max(0.03 * k * f0, 2 * df)
        k += 1

    def one(lo, hi):
        b = (f >= lo) & (f < hi)
        if not (b & hz).any() or not (b & away).any():
            return np.nan
        noise = np.median(S[b & away]) * (b & ~hz).sum()
        return float(np.clip(10 * np.log10(S[b & hz].sum() / (noise + 1e-30) + 1e-30), -20, 60))
    return one(50, min(FMAX, 0.45 * sr)), np.array([one(lo, hi) for lo, hi in bands])


def brightness(x, sr, f=1500.0, S=None):
    S, df = S if S is not None else power_spectrum(x, sr)
    fr = np.arange(len(S)) * df
    return float(10 * np.log10(S[(fr > f) & (fr < FMAX)].sum() / (S[(fr > 50) & (fr < FMAX)].sum() + 1e-30) + 1e-12))


def centroid(x, sr, S=None):
    S, df = S if S is not None else power_spectrum(x, sr)
    fr = np.arange(len(S)) * df
    m = (fr > 50) & (fr < FMAX)
    return float((S[m] * fr[m]).sum() / (S[m].sum() + 1e-30))


def tilt(amps_db, f0, lo=1.5, hi=8000.0):
    """slope (dB/octave) of harmonic amplitudes over log2 frequency, harmonics in lo*f0 .. hi"""
    a = np.asarray(amps_db, float)
    fr = f0 * np.arange(1, len(a) + 1)
    m = np.isfinite(a) & (fr >= lo * f0) & (fr <= hi)
    if m.sum() < 3:
        return np.nan
    return float(np.polyfit(np.log2(fr[m]), a[m], 1)[0])


def pitch_track(x, sr, note, win_s=0.0427, hop_s=HOP_S, span=1.07):
    """per frame (hop_s): (time of the frame centre s, cents re the note, periodicity 0..1), by a
    window-corrected normalised ACF searched within the note's period / span .. x span"""
    x = np.asarray(x, np.float64)
    n = 1 << int(np.round(np.log2(win_s * sr)))
    hop = int(round(hop_s * sr))
    w = np.hanning(n)
    wr = np.fft.irfft(np.abs(np.fft.rfft(w, 2 * n)) ** 2)[:n]
    wr = wr / wr[0] + 1e-9
    T = sr / float(note_hz(note))
    lo, hi = int(T / span), int(T * span) + 2
    out = []
    for a in range(0, max(len(x) - n, 0) + 1, hop):
        seg = x[a:a + n] * w
        R = np.fft.irfft(np.abs(np.fft.rfft(seg, 2 * n)) ** 2)[:n]
        if R[0] <= 1e-20 or hi + 1 >= n or lo < 2:
            out.append((a / sr + n / (2 * sr), np.nan, 0.0))
            continue
        Rn = R / R[0] / wr
        k = lo + int(np.argmax(Rn[lo:hi]))
        p, q, r = Rn[k - 1], Rn[k], Rn[k + 1]
        den = p - 2 * q + r
        d = min(max(0.5 * (p - r) / den, -1.0), 1.0) if den < 0 else 0.0
        out.append((a / sr + n / (2 * sr), float(1200 * np.log2(T / (k + d))), float(min(max(q, 0.0), 1.0))))
    return np.array(out).reshape(-1, 3)


def sustain_pitch(track, onset=0.0, off=None, skip=0.25, min_per=0.5):
    """the median cents of the periodic frames from onset + skip to off (NaN if fewer than 5)"""
    t, c, p = track[:, 0], track[:, 1], track[:, 2]
    m = (t >= onset + skip) & (p > min_per) & np.isfinite(c)
    if off is not None:
        m &= t <= off
    return float(np.median(c[m])) if m.sum() >= 5 else np.nan


def settle_time(track, onset=0.0, off=None, tol=10.0, skip=0.25, min_per=0.5):
    """s from the onset until the pitch stays within tol cents of its sustained median (to off)"""
    med = sustain_pitch(track, onset, off, skip, min_per)
    if not np.isfinite(med):
        return np.nan
    t, c, p = track[:, 0], track[:, 1], track[:, 2]
    m = (t >= onset) & ((t <= off) if off is not None else True)
    t, c, p = t[m], c[m], p[m]
    bad = ~((p > min_per) & np.isfinite(c) & (np.abs(c - med) <= tol))
    if not bad.any():
        return 0.0
    last = int(np.where(bad)[0][-1])
    return float(t[last + 1] - onset) if last + 1 < len(t) else np.nan


def level_env(x, sr, hop_s=0.005):
    """the RMS level per hop (dB) and the hop's length (samples)"""
    x = np.asarray(x, np.float64)
    h = max(int(hop_s * sr), 1)
    m = len(x) // h
    e = np.mean(x[:m * h].reshape(m, h) ** 2, axis=1) if m else np.zeros(0)
    return 10 * np.log10(e + 1e-14), h


def attack(x, sr, onset, off=None, t_key=None, sus_from=0.25):
    """latency: from the key (t_key, else the onset) to the first sound (-40 dB re the sustain); rise:
    from -30 dB to -3 dB re the sustain; overshoot: the first 200 ms's peak re the sustain (dB)"""
    L, h = level_env(x, sr)
    a = int(onset * sr / h)
    b = int((off if off is not None else len(x) / sr) * sr / h)
    s0 = min(a + int(sus_from * sr / h), max(b - 2, a))
    sus = 10 * np.log10(np.mean(10 ** (L[s0:b] / 10)) + 1e-30) if b > s0 else np.nan
    start = int((t_key if t_key is not None else onset) * sr / h)
    seg = L[start:b]
    if not np.isfinite(sus) or not len(seg):
        return dict(latency=np.nan, rise=np.nan, over=np.nan, sus_lvl=sus)

    def first(th):
        w = np.where(seg > sus + th)[0]
        return int(w[0]) if len(w) else None
    i40, i30, i3 = first(-40), first(-30), first(-3)
    lat = i40 * h / sr if i40 is not None else np.nan
    rise = (i3 - i30) * h / sr if (i3 is not None and i30 is not None and i3 >= i30) else np.nan
    o = i30 if i30 is not None else 0
    over = float(seg[o:o + int(0.2 * sr / h)].max() - sus)
    return dict(latency=float(lat), rise=float(rise), over=over, sus_lvl=float(sus))


def note_measures(x, sr, note, onset, off, t_key=None, nh=24):
    """every standard measure of one note: a dict"""
    a, b = int(onset * sr), int(off * sr)
    dur = (b - a) / sr
    s0, s1 = a + int(max(0.25, 0.35 * dur) * sr), a + int(0.85 * dur * sr)
    if s1 - s0 < int(0.1 * sr):
        s0, s1 = a + int(0.3 * (b - a)), a + int(0.9 * (b - a))
    sus = np.asarray(x[s0:s1], np.float64)
    tr = pitch_track(x, sr, note)
    med = sustain_pitch(tr, onset, off)
    f0 = float(note_hz(note)) * 2 ** ((med if np.isfinite(med) else 0.0) / 1200)
    S = power_spectrum(sus, sr)
    amps = harmonics(sus, sr, f0, nh, S=S)
    h_all, h_band = hnr(sus, sr, f0, S=S)
    r = dict(note=float(note), lvl=float(10 * np.log10(np.mean(sus ** 2) + 1e-20)), cents=med,
             settle=settle_time(tr, onset, off), harm=amps, env=envelope(sus, sr, S=S), hnr=h_all, hnr_bands=h_band,
             bright=brightness(sus, sr, S=S), centroid=centroid(sus, sr, S=S), tilt=tilt(amps, f0))
    r.update(attack(x, sr, onset, off, t_key))
    return r


def dynamics_curve(levels):
    """levels {(dynamic, note): dB}, dynamics ordered soft to loud in the keys' first appearance:
    {dynamic: [(note, level re the loudest dynamic at that note)]}, {dynamic: mean}"""
    dyns = list(dict.fromkeys(d for d, _ in levels))
    loud = dyns[-1]
    per = {d: [] for d in dyns}
    for (d, n), v in levels.items():
        if (loud, n) in levels:
            per[d].append((n, v - levels[(loud, n)]))
    return per, {d: float(np.mean([v for _, v in r])) if r else np.nan for d, r in per.items()}
