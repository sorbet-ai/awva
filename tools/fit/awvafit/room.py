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
ENDS_GAP_S = 0.22  # an annotated phrase end needs this much breath after it
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


def measure(x, sr=SR, ends=None):
    """ends: [(note end s, next sound s)] where the score says the player stops (a phrase's last note
    into a breath); without, the free decays are found blind"""
    x = np.asarray(x, np.float64)
    if x.ndim > 1:
        x = x.mean(1)
    if ends is None:
        ds, _ = decays(x, sr)
        ds = [(t0, t1, 0.06, 0.08) for t0, t1, _ in ds]
    else:  # an annotated end may be off by tens of ms: the stop is searched 100 ms either side
        ds = [(t0, t1 - 0.05, 0.1, 0.2) for t0, t1 in ends if t1 - t0 >= ENDS_GAP_S]  # (the next sound may lead its onset)
    rts, drrs, used = [], [], []
    w5 = int(0.005 * sr)
    for t0, t1, back, span in ds:
        a = max(int((t0 - back) * sr), 0)
        b = int(t1 * sr)
        y = x[a:b] ** 2
        if len(y) < int((span + GAP_S) * sr):
            continue
        lvl = 10 * np.log10(np.convolve(y, np.ones(w5) / w5, mode='same') + 1e-20)
        # the source stops where the level falls fastest (the direct sound leaves at once)
        d1 = np.diff(lvl[: int(span * sr)])
        ws = int(0.002 * sr)
        sm = np.convolve(d1, np.ones(ws) / ws, mode='same')
        s0 = int(np.argmin(sm[w5:])) + w5
        Ls = float(np.median(lvl[max(s0 - int(0.05 * sr), 0):max(s0 - w5, 1)]))
        tail = y[s0 + w5 // 2:]
        lt = lvl[s0 + w5 // 2:]
        # the background (the gap's last 30 %): its power taken out, and the integration stopped where
        # the decay meets it (+3 dB), so a noisy room doesn't read as a long one
        h10 = int(0.01 * sr)
        nb = len(tail) // h10
        noise = float(np.min(np.mean(tail[:nb * h10].reshape(nb, h10), 1))) if nb else 0.0  # the gap's quietest 10 ms
        nf = 10 * math.log10(noise + 1e-20)
        above = np.where(lt < nf + 3)[0]
        cut = int(above[0]) if len(above) else len(tail)
        tail, lt = np.maximum(tail[:cut] - noise, 0.0), lt[:cut]
        if len(tail) < int(0.03 * sr):
            continue
        tt = np.arange(len(tail)) / sr
        edc = np.cumsum(tail[::-1])[::-1]
        edc_db = 10 * np.log10(edc / (edc[0] + 1e-30) + 1e-30)
        m = (edc_db <= -5) & (edc_db >= -25)
        if m.sum() < int(0.03 * sr) or edc_db[m].min() > -12:
            continue
        slope, icpt = np.polyfit(tt[m], edc_db[m], 1)
        if slope >= 0:
            continue
        fit = icpt + slope * tt[m]
        r2 = 1 - np.sum((edc_db[m] - fit) ** 2) / (np.sum((edc_db[m] - edc_db[m].mean()) ** 2) + 1e-20)
        rt = -60.0 / slope
        if r2 < 0.9 or not 0.05 <= rt <= 4.0:
            continue
        # the reverberant level at the stop: the tail's level, its decay line (the EDC's slope) through
        # its first 30 ms after the stop, back to the stop
        k30 = int(0.03 * sr)
        if len(lt) < k30 + w5:
            continue
        Lr = float(np.median(lt[w5:k30] - slope * tt[w5:k30]))
        d = Ls - Lr
        if d <= 0.2:
            continue
        rts.append(rt)
        drrs.append(10 * math.log10(10 ** (d / 10) - 1))
        used.append(dict(at_s=round((a + s0) / sr, 3), rt60_s=round(rt, 3), drop_db=round(d, 1), drr_db=round(drrs[-1], 1)))
    return _summary(rts, drrs, used)


def _summary(rts, drrs, used):
    if len(rts) < MIN_DECAYS:
        return Room(None, None, used)
    return Room(round(float(np.median(rts)), 3), round(float(np.median(drrs)), 1), used)


def measure_many(tracks, sr=SR):
    """one room from several tracks recorded in it: [(x, ends)] -> Room over all their decays"""
    rts, drrs, used = [], [], []
    for x, ends in tracks:
        m = measure(x, sr, ends)
        for d in m.decays:
            rts.append(d['rt60_s'])
            drrs.append(d['drr_db'])
            used.append(d)
    return _summary(rts, drrs, used)


def track_ends(items, ref_path):
    """[(note end, next sound)] on a track's clock from its phrase items (awvafit.phraseset): each
    phrase's last note into the breath before the next phrase"""
    spans = []
    for it in items:
        if it.ref_path != ref_path or not it.notes:
            continue
        on = it.ref_start + it.notes[0]['onset_s']
        off = it.notes[-1].get('offset_s')
        if off is not None:
            spans.append((on, it.ref_start + off))
    spans.sort()
    out = []
    for (a, b), (c, _) in zip(spans, spans[1:]):
        if c > b:
            out.append((b, c))
    return sorted(set(out))


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
