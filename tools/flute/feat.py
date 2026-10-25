# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Per-note features of a single flute note (mono float32 at 48 kHz), the same code for the
recordings and the model.

  note_feats(x, note, onset_s, off_s) -> dict
    lvl      sustain level (dB, RMS)
    harm     harmonics 1..NH of the sounding pitch, dB re the sustain level (sustain, averaged spectrum)
    noise    the floor between the harmonics in bands BANDS (dB per bin re the sustain level)
    f0c      sustain pitch re the note (cents); per = sustain periodicity (normalised ACF)
    reg      the sounding register: the strongest of f0/2, f0, 2 f0 (0.5, 1, 2), 1 = right
    rise     time from the onset (first frame within 30 dB of the sustain) to within 3 dB (s)
    atk      harmonics 1..4 and the noise floor in the attack windows ATK (dB re the sustain level)
    env      the level envelope (5 ms frames, dB re sustain) over the first 0.3 s after the onset
"""
import numpy as np

SR = 48000
NH = 10
BANDS = [(150, 800), (800, 2000), (2000, 4000), (4000, 8000), (8000, 14000)]
ATK = [(0.0, 0.03), (0.03, 0.06), (0.06, 0.12)]


def _spec(seg, nfft):
    w = np.hanning(len(seg))
    # one-sided power per bin: a harmonic's power is the sum over its main lobe
    return np.abs(np.fft.rfft(seg * w, nfft)) ** 2 * 2 / (nfft * (w ** 2).sum())


def _avg_spec(x, n=4096, hop=2048):
    if len(x) < n:
        x = np.concatenate([x, np.zeros(n - len(x), np.float32)])
    fr = [x[i:i + n] for i in range(0, len(x) - n + 1, hop)]
    return np.mean([_spec(f, n) for f in fr], axis=0), n


def harmonics(P, n, f0, nh=NH, nwin=4096):
    df = SR / n
    lobe = 2.0 * SR / nwin
    out = []
    for k in range(1, nh + 1):
        fk = k * f0
        if fk > 0.45 * SR:
            out.append(-120.0)
            continue
        c = int(round(fk / df))
        w = max(2, int(min(max(0.03 * fk, lobe), 0.45 * f0) / df))
        out.append(10 * np.log10(P[max(c - w, 1):c + w + 1].sum() + 1e-14))
    return np.array(out)


def floor_bands(P, n, f0):
    df = SR / n
    f = np.arange(len(P)) * df
    ph = (f / f0) % 1.0
    away = (ph > 0.3) & (ph < 0.7)  # between harmonics
    res = []
    for lo, hi in BANDS:
        m = away & (f >= lo) & (f < hi)
        res.append(10 * np.log10(np.median(P[m]) / df + 1e-14) if m.sum() > 3 else -120.0)
    return np.array(res)


def note_feats(x, note, onset, off, from_key=False):
    x = np.asarray(x, np.float32)
    f0n = 440 * 2 ** ((note - 69) / 12)
    a = int(onset * SR)
    b = int(off * SR)
    s0, s1 = a + int(0.3 * SR), b - int(0.12 * SR)
    if s1 - s0 < int(0.25 * SR):
        s0 = a + int(0.5 * (b - a))
        s1 = b - int(0.05 * SR)
    sus = x[s0:s1]
    r = {}
    rms = np.sqrt(np.mean(sus.astype(np.float64) ** 2) + 1e-20)
    r['lvl'] = 20 * np.log10(rms)
    P, n = _avg_spec(sus)
    r['harm'] = harmonics(P, n, f0n) - r['lvl']
    r['noise'] = floor_bands(P, n, f0n) - r['lvl']
    # register: power at f0/2, f0, 2 f0 (+-3 %)
    df = SR / n

    def bandp(f):
        c = int(round(f / df))
        w = max(2, int(0.03 * f / df))
        return P[max(c - w, 1):c + w + 1].sum()
    hs = harmonics(P, n, f0n / 2, 2 * NH)
    odd = 10 * np.log10(np.sum(10 ** (hs[0::2] / 10)) + 1e-14)  # f0/2, 3f0/2, ...
    even = 10 * np.log10(np.sum(10 ** (hs[1::2] / 10)) + 1e-14)  # f0, 2f0, ...
    r['sub'] = odd - even  # > -10: a period doubling / the lower register
    p1, p2, p3 = bandp(f0n), bandp(2 * f0n), bandp(3 * f0n)
    r['oct_up'] = 10 * np.log10((p2 + 1e-14) / (p1 + 1e-14))
    # pitch (ACF)
    m = min(len(sus), 8192)
    seg = sus[(len(sus) - m) // 2:(len(sus) + m) // 2] * np.hanning(m)
    R = np.fft.irfft(np.abs(np.fft.rfft(seg, 2 * m)) ** 2)[:m]
    wr = np.fft.irfft(np.abs(np.fft.rfft(np.hanning(m), 2 * m)) ** 2)[:m]
    Rn = R / (R[0] + 1e-20) / (wr / wr[0] + 1e-9)
    T = SR / f0n
    lo, hi = int(T / 1.06), int(T * 1.06) + 2
    k = lo + int(np.argmax(Rn[lo:hi]))
    aa, bb, cc = Rn[k - 1], Rn[k], Rn[k + 1]
    den = aa - 2 * bb + cc
    d = min(max(0.5 * (aa - cc) / den, -1), 1) if den < 0 else 0
    r['f0c'] = 1200 * np.log2(T / (k + d))
    r['per'] = float(min(max(bb, 0), 1))
    # envelope (5 ms) and attack
    hop = 240
    e = np.array([np.mean(x[i:i + hop].astype(np.float64) ** 2) for i in range(max(a - int(0.05 * SR), 0), min(b, len(x) - hop), hop)])
    L = 10 * np.log10(e + 1e-20) - r['lvl']
    st = np.argmax(L > -30) if (L > -30).any() else 0
    t_on = max(a - int(0.05 * SR), 0) + st * hop  # the attack's start (sample)
    j3 = np.argmax(L[st:] > -3) if (L[st:] > -3).any() else len(L) - st
    r['rise'] = j3 * hop / SR
    env = L[st:st + 60]
    r['env'] = np.concatenate([env, np.full(60 - len(env), env[-1] if len(env) else -60.0)])
    atk = []
    for (u0, u1) in ATK:
        seg = x[t_on + int(u0 * SR): t_on + int(u1 * SR)]
        nf = 4096
        Pa = _spec(seg, nf)
        h = harmonics(Pa, nf, f0n, 4, len(seg)) - r['lvl']
        fl = floor_bands(Pa, nf, f0n)[1:4] - r['lvl']
        atk.append(np.concatenate([h, fl]))
    r['atk'] = np.array(atk)
    # the tone's rise (harmonics 1-3): from the key (the model) or from the first sound (a recording)
    t0 = a if from_key else t_on
    r['trise'], _ = tonal_rise(x, note, t0, int(0.4 * SR))
    return r


def tonal_rise(x, note, start, nmax, hop=96):
    """Time from sample `start` until the tone (harmonics 1-3 of the note, 1024-sample causal frames
    every 2 ms) is within 3 dB of its level 150-400 ms in (s); also the tone's envelope (dB)."""
    fk = 440 * 2 ** ((note - 69) / 12)
    win = np.hanning(1024)
    fr = np.fft.rfftfreq(4096, 1 / SR)
    mask = np.zeros(len(fr), bool)
    for k in (1, 2, 3):
        mask |= np.abs(fr - k * fk) < max(0.04 * k * fk, 30)
    seg = []
    for i in range(0, nmax, hop):
        e = start + i
        a = e - 1024
        f = x[max(a, 0):e]
        if len(f) < 1024:
            f = np.concatenate([np.zeros(1024 - len(f), np.float32), f])
        P = np.abs(np.fft.rfft(f * win, 4096)) ** 2
        seg.append(10 * np.log10(P[mask].sum() + 1e-20))
    seg = np.array(seg)
    i0 = int(0.15 * SR / hop)
    ref = np.median(seg[i0:]) if len(seg) > i0 + 10 else seg.max()
    return np.argmax(seg >= ref - 3) * hop / SR, seg - ref
