# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Loudness by ITU-R BS.1770-4: K-weighting (a high shelf, then a high pass), mean square over
400 ms blocks with 75 % overlap, gated at -70 LUFS absolute and -10 LU relative. Mono or [n, ch]
(channel weights 1: no surround channels here).

  lufs(x, sr)                 integrated loudness (LUFS); -inf for silence
  momentary(x, sr)            per 100 ms step, the loudness of the last 400 ms (LUFS)
  match_gain(x, ref, sr)      the linear gain that gives x the loudness of ref
  k_weighting(sr)             the two biquads' (b, a) at sr

The filter design follows the standard's 48 kHz coefficients at any rate (the analogue prototypes as
libebur128 derives them): at 48 kHz it gives the published coefficients exactly enough that a 0 dBFS
997 Hz sine on one channel reads -3.01 LUFS.
"""
import math

import numpy as np
from scipy.signal import lfilter

ABS_GATE = -70.0
REL_GATE = -10.0


def k_weighting(sr):
    """((b1, a1), (b2, a2)): the high shelf (+4 dB above ~1.7 kHz) and the high pass (~38 Hz)"""
    f0, G, Q = 1681.974450955533, 3.999843853973347, 0.7071752369554196
    K = math.tan(math.pi * f0 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1.0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    f0, Q = 38.13547087602444, 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    d = 1 + K / Q + K * K
    b2 = [1.0, -2.0, 1.0]
    a2 = [1.0, 2 * (K * K - 1) / d, (1 - K / Q + K * K) / d]
    return (np.array(b1), np.array(a1)), (np.array(b2), np.array(a2))


def _weighted(x, sr):
    x = np.asarray(x, np.float64)
    if x.ndim == 1:
        x = x[:, None]
    (b1, a1), (b2, a2) = k_weighting(sr)
    return lfilter(b2, a2, lfilter(b1, a1, x, axis=0), axis=0)


def _block_ms(y, sr, block=0.4, step=0.1):
    n, s = int(round(block * sr)), int(round(step * sr))
    if len(y) < n:
        return np.zeros(0)
    nb = 1 + (len(y) - n) // s
    c = np.concatenate([np.zeros((1, y.shape[1])), np.cumsum(y ** 2, axis=0)])
    starts = np.arange(nb) * s
    z = (c[starts + n] - c[starts]) / n  # mean square per block and channel
    return z.sum(1)


def _lufs_of(z):
    return -0.691 + 10 * np.log10(z) if z > 0 else -math.inf


def momentary(x, sr):
    z = _block_ms(_weighted(x, sr), sr)
    with np.errstate(divide='ignore'):
        return -0.691 + 10 * np.log10(z)


def lufs(x, sr):
    z = _block_ms(_weighted(x, sr), sr)
    if not len(z):
        return -math.inf
    with np.errstate(divide='ignore'):
        lj = -0.691 + 10 * np.log10(z)
    z1 = z[lj > ABS_GATE]
    if not len(z1):
        return -math.inf
    rel = _lufs_of(z1.mean()) + REL_GATE
    z2 = z[(lj > ABS_GATE) & (lj > rel)]
    return _lufs_of(z2.mean()) if len(z2) else -math.inf


def match_gain(x, ref, sr, sr_ref=None):
    """the linear gain for x to read ref's integrated loudness (1.0 if either is silent)"""
    a, b = lufs(x, sr), lufs(ref, sr_ref or sr)
    if not (math.isfinite(a) and math.isfinite(b)):
        return 1.0
    return 10 ** ((b - a) / 20)
