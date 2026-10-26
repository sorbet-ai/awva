# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""A recording's room, measured blind from the music, and the same room put on a dry render.

  measure(x, sr) -> Room(rt60_s, drr_db, decays): from the track's own free decays (a note's end into a
     gap of >= 0.3 s whose level falls >= 25 dB under the note):
       rt60  Schroeder's backward integration of the decay's energy from the note's end, the line fitted
             from -5 to -25 dB under its start (or to 5 dB over the gap's floor), -60 / slope
       drr   the drop at the note's end (where the 5 ms level falls fastest: the direct sound stops at
             once): the note's level just before L_s against the tail's level just after, taken back to
             that instant along the decay's slope, L_r; the direct
             and reverberant powers add while the note sounds, so DRR = 10 log10(10^((L_s - L_r)/10) - 1)
     the medians over the decays (at least MIN_DECAYS, else None)
  apply(x, room, sr, seed) -> x through the room: an impulse response of the direct sound (1 at 0) and a
     reverberant tail from 5 ms (white noise, seeded, decaying 60 dB in rt60), scaled so the direct over
     the tail's energy is the DRR; one algorithm for every file, nothing tuned per instrument

The measure is blind (no impulse response): it reads only rooms long enough to show in a decay
(roughly rt60 >= 0.1 s) and assumes the player stops cleanly at a note's end.
"""
import math
from dataclasses import dataclass, field

import numpy as np

SR = 48000
HOP = 0.01
GAP_S = 0.3
DROP_DB = 25.0
MIN_DECAYS = 3
PREDELAY = 0.005


@dataclass
class Room:
    rt60_s: float = None
    drr_db: float = None
    decays: list = field(default_factory=list)


def _env(x, sr):
    h = int(HOP * sr)
    n = len(x) // h
    return 10 * np.log10(np.mean(x[:n * h].reshape(n, h) ** 2, 1) + 1e-20)


def decays(x, sr=SR):
    """[(note end s, gap end s, sustained level dB)]: the free decays"""
    e = _env(x, sr)
    floor = float(np.percentile(e, 5))
    out = []
    n = len(e)
    k = int(0.3 / HOP)
    while k < n - int(GAP_S / HOP):
        sus = e[k - int(0.3 / HOP):k]
        Ls = float(np.median(e[k - int(0.1 / HOP):k]))
        steady = sus.max() - sus.min() < 9 and Ls > floor + DROP_DB + 10
        if steady and e[k] < Ls - 3:
            j = k
            while j < n and e[j] < Ls - 3:
                j += 1
            if (j - k) * HOP >= GAP_S and e[k:j].min() < Ls - DROP_DB:
                out.append((k * HOP, j * HOP, Ls))
                k = j + int(0.3 / HOP)
                continue
        k += 1
    return out, floor


def measure(x, sr=SR):
    x = np.asarray(x, np.float64)
    if x.ndim > 1:
        x = x.mean(1)
    ds, floor = decays(x, sr)
    rts, drrs, used = [], [], []
    w5 = int(0.005 * sr)
    for t0, t1, _ in ds:
        a = max(int((t0 - 0.06) * sr), 0)
        b = int(t1 * sr)
        y = x[a:b] ** 2
        lvl = 10 * np.log10(np.convolve(y, np.ones(w5) / w5, mode='same') + 1e-20)
        # the source stops where the level falls fastest (the direct sound leaves at once), in the
        # 60 ms before the level is 3 dB down
        d1 = np.diff(lvl[: int(0.08 * sr)])
        ws = int(0.002 * sr)
        sm = np.convolve(d1, np.ones(ws) / ws, mode='same')
        s0 = int(np.argmin(sm[w5:])) + w5
        Ls = float(np.median(lvl[max(s0 - int(0.05 * sr), 0):max(s0 - w5, 1)]))
        tail = y[s0 + w5 // 2:]
        lt = lvl[s0 + w5 // 2:]
        tt = np.arange(len(tail)) / sr
        edc = np.cumsum(tail[::-1])[::-1]
        edc_db = 10 * np.log10(edc / edc[0] + 1e-30)
        fl = float(np.median(lt[-int(0.05 * sr):])) + 5
        lo = max(-25.0, fl - float(lt[:w5].mean()))
        m = (edc_db <= -5) & (edc_db >= lo)
        if m.sum() < int(0.03 * sr) or lo > -12:
            continue
        slope, _ = np.polyfit(tt[m], edc_db[m], 1)
        if slope >= 0:
            continue
        rt = -60.0 / slope
        # the reverberant level at the stop: the tail's level, its decay line (the EDC's slope) through
        # its first 30 ms after the stop, back to the stop
        k30 = int(0.03 * sr)
        Lr = float(np.median(lt[w5:k30] - slope * tt[w5:k30]))
        d = Ls - Lr
        if d <= 0.2:
            continue
        rts.append(rt)
        drrs.append(10 * math.log10(10 ** (d / 10) - 1))
        used.append(dict(at_s=round((a + s0) / sr, 3), rt60_s=round(rt, 3), drop_db=round(d, 1)))
    if len(rts) < MIN_DECAYS:
        return Room(None, None, used)
    return Room(round(float(np.median(rts)), 3), round(float(np.median(drrs)), 1), used)


def impulse(room, sr=SR, seed=0):
    n = int((PREDELAY + 1.5 * room.rt60_s) * sr)
    h = np.zeros(n)
    h[0] = 1.0
    t = np.arange(n - int(PREDELAY * sr)) / sr
    tail = np.random.default_rng(seed).standard_normal(len(t)) * np.exp(-6.907755 * t / room.rt60_s)
    g = math.sqrt(10 ** (-room.drr_db / 10) / np.sum(tail ** 2))
    h[int(PREDELAY * sr):] += g * tail
    return h


def apply(x, room, sr=SR, seed=0):
    from scipy.signal import fftconvolve
    if room.rt60_s is None:
        return np.asarray(x, np.float64)
    return fftconvolve(np.asarray(x, np.float64), impulse(room, sr, seed))[:len(x) + int(1.5 * room.rt60_s * sr)]
