# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The harmonic-sum note analysis (the sax's fit): one sustained note, recording or model render,
mono float32 at 48 kHz. The sustain is found as the steadiest plateau of the note, the pitch by a
harmonic-sum search around the nominal note, refined on the first harmonics' peaks.

note_feats(x, note, i0=None) -> dict
  onset   sample of the onset (first 5 ms frame within 35 dB of the note's sustain level)
  lvl     sustain level, dB (mean square, window SUS_A..SUS_B after the onset)
  f0c     sustain pitch re the nominal note (cents), from a harmonic-sum search
  per     strength of the harmonic structure: harmonic power / total power in the sustain window
  H       harmonics 1..NH, dB re the total harmonic power (-80 where absent)
  band    harmonic power per log band (NB bands, 100 Hz..12 kHz), dB re total (nan: no harmonic)
  hnr     harmonic-to-noise per broad band (<1k, 1-3k, 3-6k, 6-12k), dB
  bright  harmonic power above 1.5 kHz re total, dB
  t_rise  ms from -30 dB to -3 dB re the sustain level
  over    attack overshoot: max level in the first 200 ms minus sustain, dB
  a_br    attack brightness (first 40 ms from -20 dB) minus sustain brightness, dB
  sub     subharmonic / non-harmonic strength: power at (k+1/2) f0 re harmonic power, dB
"""
import numpy as np

SR = 48000
HOP = 240  # 5 ms
WIN = 480
SUS_A, SUS_B = 0.25, 0.95  # sustain window after the onset (s)
NH = 10
FMAX = 12000.0
BANDS = np.geomspace(100.0, 12000.0, 21)
NB = len(BANDS) - 1
BROAD = [(0, 1000), (1000, 3000), (3000, 6000), (6000, 12000)]


def note_hz(n):
    return 440.0 * 2 ** ((n - 69) / 12)


def env_db(x):
    n = max((len(x) - WIN) // HOP, 1)
    idx = np.arange(n)[:, None] * HOP + np.arange(WIN)[None, :]
    idx = np.minimum(idx, len(x) - 1)
    e = np.mean(x[idx] ** 2, axis=1)
    return 10 * np.log10(e + 1e-14)


def segments(x, gap=0.12, floor_db=None, min_len=0.9):
    """Notes in a file of notes separated by silence: (start, end) samples."""
    e = env_db(x)
    top = np.percentile(e, 99.5)
    fl = np.percentile(e, 5) if floor_db is None else floor_db
    th = max(fl + 12, top - 45)
    on = e > th
    segs = []
    i = 0
    n = len(on)
    ng = int(gap * SR / HOP)
    while i < n:
        if not on[i]:
            i += 1
            continue
        j = i
        last = i
        while j < n and (j - last) < ng:
            if on[j]:
                last = j
            j += 1
        if (last - i) * HOP / SR >= min_len:
            segs.append((max(i * HOP - 2400, 0), min((last + 1) * HOP + WIN + 9600, len(x))))
        i = last + 1
    return segs


def _hsum(P, df, f, kmax=6):
    s = 0.0
    for k in range(1, kmax + 1):
        b = k * f / df
        i = int(b)
        if i + 2 >= len(P):
            break
        s += np.log(P[i - 1:i + 3].max() + 1e-20)
    return s


def harmonic_analysis(seg, f_nom):
    N = len(seg)
    w = np.hanning(N)
    nfft = 1 << int(np.ceil(np.log2(N)) + 1)
    X = np.fft.rfft(seg * w, nfft)
    P = np.abs(X) ** 2 / (w ** 2).sum()
    df = SR / nfft
    # pitch: harmonic-sum search +-90 cents around the nominal, then a finer step
    best, bc = -1e30, 0.0
    for c in np.arange(-90, 91, 3.0):
        v = _hsum(P, df, f_nom * 2 ** (c / 1200))
        if v > best:
            best, bc = v, c
    for c in np.arange(bc - 3, bc + 3.01, 0.5):
        v = _hsum(P, df, f_nom * 2 ** (c / 1200))
        if v > best:
            best, bc = v, c
    f0 = f_nom * 2 ** (bc / 1200)
    # refine with the first harmonics' peak positions
    kk = []
    for k in range(1, 5):
        b = k * f0 / df
        lo, hi = int(b - 0.2 * f0 / df), int(b + 0.2 * f0 / df) + 1
        if hi + 1 >= len(P):
            break
        i = lo + int(np.argmax(P[lo:hi]))
        a, bb, cc = np.log(P[i - 1] + 1e-20), np.log(P[i] + 1e-20), np.log(P[i + 1] + 1e-20)
        den = a - 2 * bb + cc
        d = 0.5 * (a - cc) / den if den < 0 else 0.0
        kk.append(((i + d) * df / k, P[i]))
    if kk:
        ws = np.array([p for _, p in kk])
        f0 = float(np.sum([f for f, _ in kk] * ws) / ws.sum())
    hb = max(int(0.5 * f0 / df), 2)
    harm = []
    noise = []
    k = 1
    while k * f0 < FMAX and k <= 120:
        b = int(round(k * f0 / df))
        if b + hb + 2 >= len(P):
            break
        lo, hi = b - max(int(0.15 * f0 / df), 2), b + max(int(0.15 * f0 / df), 2) + 1
        hl = int(np.ceil(2.0 * nfft / N)) + 1
        i = lo + int(np.argmax(P[lo:hi]))
        hp = P[max(i - hl, 0):i + hl + 1].sum()
        nb = P[int((k + 0.3) * f0 / df):int((k + 0.7) * f0 / df) + 1]
        nz = float(np.median(nb)) if len(nb) else 0.0
        harm.append(hp)
        noise.append(nz)
        k += 1
    harm = np.array(harm)
    noise = np.array(noise)
    return f0, harm, noise, P, df


def note_feats(x, note, i0=None, seg_end=None):
    x = np.asarray(x, np.float64)
    if seg_end is not None:
        x = x[:seg_end]
    if i0 is not None:
        x = x[i0:]
    e = env_db(x)
    if len(e) < 10 or not np.all(np.isfinite(x)):
        return None
    # the sustain level from the steadiest part, then the onset re it
    fr_a, fr_b = int(SUS_A * SR / HOP), int(SUS_B * SR / HOP)
    pk = int(np.argmax(e))
    # first frame within 35 dB of the 80th percentile level of the note
    ref = np.percentile(e, 80)
    on = np.where(e > ref - 35)[0]
    if len(on) == 0:
        return None
    o = int(on[0])
    # the sustain: the longest run within 6 dB of the note's 90th percentile level, its middle
    # (at most SUS_B - SUS_A long)
    l90 = np.percentile(e[o:], 90)
    plat = e > l90 - 6
    plat[:o + int(0.1 * SR / HOP)] = False
    best, bs, i = 0, 0, 0
    n = len(plat)
    while i < n:
        if plat[i]:
            j = i
            while j < n and plat[j]:
                j += 1
            if j - i > best:
                best, bs = j - i, i
            i = j
        else:
            i += 1
    if best < 40:
        return None
    span = min(best, fr_b - fr_a)
    a = bs + (best - span) // 2
    b = a + span
    s0, s1 = a * HOP, b * HOP + WIN
    seg = x[s0:s1]
    lvl = 10 * np.log10(np.mean(seg ** 2) + 1e-14)
    f_nom = note_hz(note)
    f0, harm, noise, P, df = harmonic_analysis(seg, f_nom)
    tot = harm.sum() + 1e-20
    fr = f0 * np.arange(1, len(harm) + 1)
    r = {}
    r['onset'] = o * HOP
    r['lvl'] = float(lvl)
    r['f0c'] = float(1200 * np.log2(f0 / f_nom))
    totp = P[int(50 / df):int(FMAX / df)].sum() + 1e-20
    r['per'] = float(tot / totp)
    H = np.full(NH, -80.0)
    n = min(NH, len(harm))
    H[:n] = np.maximum(10 * np.log10(harm[:n] / tot + 1e-12), -80)
    r['H'] = H
    band = np.full(NB, np.nan)
    for j in range(NB):
        m = (fr >= BANDS[j]) & (fr < BANDS[j + 1])
        if m.any():
            band[j] = max(10 * np.log10(harm[m].sum() / tot + 1e-12), -80)
    r['band'] = band
    hnr = np.full(len(BROAD), np.nan)
    for j, (lo, hi) in enumerate(BROAD):
        m = (fr >= lo) & (fr < hi)
        if m.any():
            nb = int((hi - lo) / df)
            nz = np.mean(noise[m]) * nb + 1e-20
            hnr[j] = float(np.clip(10 * np.log10(harm[m].sum() / nz), -20, 60))
    r['hnr'] = hnr
    r['bright'] = float(10 * np.log10(harm[fr > 1500].sum() / tot + 1e-9))
    # attack
    sus = 10 * np.log10(np.mean(10 ** (e[a:b] / 10)))
    w0 = np.where(e[:a] > sus - 30)[0]
    w3 = np.where(e[:a] > sus - 3)[0]
    t30 = w0[0] if len(w0) else o
    t3 = w3[0] if len(w3) else a
    r['t_rise'] = float(max(t3 - t30, 0) * HOP / SR * 1000)
    r['over'] = float(e[t30:t30 + int(0.2 * SR / HOP)].max() - sus)
    w20 = np.where(e[:a] > sus - 20)[0]
    t20 = int(w20[0]) if len(w20) else o
    at = x[t20 * HOP:t20 * HOP + int(0.04 * SR)]
    if len(at) > 256:
        A = np.abs(np.fft.rfft(at * np.hanning(len(at)))) ** 2
        fa = np.arange(len(A)) * SR / (2 * (len(A) - 1))
        ab = 10 * np.log10(A[fa > 1500].sum() / (A[fa > 50].sum() + 1e-20) + 1e-9)
        # the sustain's brightness measured the same way (all energy, not only harmonics)
        Ps = P
        fs = np.arange(len(Ps)) * df
        sb = 10 * np.log10(Ps[(fs > 1500) & (fs < FMAX)].sum() / (Ps[(fs > 50) & (fs < FMAX)].sum() + 1e-20) + 1e-9)
        r['a_br'] = float(ab - sb)
    else:
        r['a_br'] = 0.0
    # half-harmonics (period doubling, multiphonics)
    sp = []
    for k in range(1, min(8, len(harm))):
        bb = int(round((k + 0.5) * f0 / df))
        hl2 = int(np.ceil(2.0 * len(P) * 2 / len(seg))) + 1
        sp.append(P[bb - hl2:bb + hl2 + 1].sum())
    r['sub'] = float(10 * np.log10((np.sum(sp) + 1e-20) / (harm[:8].sum() + 1e-20))) if sp else -60.0
    return r
