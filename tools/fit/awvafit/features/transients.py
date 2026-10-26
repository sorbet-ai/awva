# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Clicks and dropouts: a transient detector that tells a click from an attack or a buzzing waveform.

  clicks(x, sr)            [s] of the clicks: a sample-to-sample jump larger than 3 x every other jump
                           from 2 to 20 ms on both sides of it (in 1 ms blocks), and over 1 % of the
                           peak. A buzzing waveform's sharp edge recurs every period, within 20 ms, and an
                           attack out of silence is followed by its own large jumps, so neither counts;
                           an isolated pop, in a tone or in silence, does.
  split(times, onsets)     (clicks, onset transients): an event within 15 ms of a note onset may be the
                           attack's own first edge (a tongue), a question for the ear, not a failure
  hf_bursts(x, ref, sr)    [(start, end, dB)]: bright bursts against a reference: the energy above 5 kHz
                           in 25 ms frames more than 15 dB over the reference's for 2 frames or more,
                           away from the onsets (a glitch that is no click: a jet re-triggering)
  dropouts(x, sr, notes)   [(note index, s)]: inside a held note, once it has reached its level, the 10 ms
                           level falling 30 dB under the note's median and coming back (a gap in the sound)
"""
import numpy as np
from numpy.lib.stride_tricks import sliding_window_view


def clicks(x, sr=48000, ratio=3.0, near_ms=2, far_ms=20, floor=0.01):
    m = x.mean(1) if x.ndim > 1 else np.asarray(x)
    d = np.abs(np.diff(m))
    b = max(int(sr / 1000), 1)
    nb = len(d) // b
    if nb < 2 * far_ms + 2:
        return []
    bm = d[:nb * b].reshape(nb, b).max(1)
    pad = np.concatenate([np.zeros(far_ms), bm, np.zeros(far_ms)])
    win = sliding_window_view(pad, 2 * far_ms + 1)
    ref = np.maximum(win[:, :far_ms - near_ms + 1].max(1), win[:, far_ms + near_ms:].max(1))
    hit = (bm > ratio * np.maximum(ref, 1e-7)) & (bm > floor * max(np.abs(m).max(), 1e-9))
    ev = []
    for h in np.where(hit)[0]:
        if not ev or h - ev[-1] > 2:
            ev.append(int(h))
    return [h / 1000 for h in ev]


def hf_bursts(x, ref, sr=48000, onsets=(), hi=5000.0, frame=0.025, over_db=15.0, min_frames=2, near=0.06):
    """[(start s, end s, dB over the reference)]: runs of `min_frames` or more 25 ms frames whose energy
    above `hi` Hz is more than `over_db` over the reference's at the same time (the two aligned, each
    re its own overall level), away from an onset (within `near` s of one an attack is expected). A
    glitch inside a held note (a jet or reed re-triggering on a pressure swell) is bright and brief:
    it shows here and not as a click."""
    from scipy.signal import butter, sosfilt
    sos = butter(4, hi, 'high', fs=sr, output='sos')
    h = int(frame * sr)

    def bands(y):
        y = y.mean(1) if y.ndim > 1 else np.asarray(y, float)
        n = len(y) // h
        hp = sosfilt(sos, y)[:n * h].reshape(n, h)
        full = y[:n * h].reshape(n, h)
        return 10 * np.log10(np.mean(hp ** 2, 1) + 1e-20) - 10 * np.log10(np.mean(full ** 2) + 1e-20)

    a, b = bands(x), bands(ref)
    n = min(len(a), len(b))
    d = a[:n] - b[:n]
    hot = d > over_db
    for o in onsets:
        hot[max(int((o - near) / frame), 0):int((o + near) / frame) + 1] = False
    out = []
    k = 0
    while k < n:
        if hot[k]:
            j = k
            while j < n and hot[j]:
                j += 1
            if j - k >= min_frames:
                out.append((round(k * frame, 3), round(j * frame, 3), round(float(d[k:j].max()), 1)))
            k = j
        else:
            k += 1
    return out


def split(times, onsets, near=0.015):
    tr = [t for t in times if onsets and min(abs(t - o) for o in onsets) <= near]
    return [t for t in times if t not in tr], tr


def dropouts(x, sr, notes, depth_db=30.0):
    m = x.mean(1) if x.ndim > 1 else np.asarray(x)
    h = int(0.01 * sr)
    out = []
    for k, (on, off) in enumerate(notes):
        a, b = int((on + 0.08) * sr), int((off - 0.05) * sr)
        if b - a < 4 * h:
            continue
        seg = m[a:b]
        n = len(seg) // h
        L = 10 * np.log10(np.mean(seg[:n * h].reshape(n, h) ** 2, 1) + 1e-20)
        med = np.median(L)
        up = np.where(L > med - 6)[0]
        if not len(up):
            continue
        i0 = int(up[0])  # the note has reached its level: a slow attack before it is not a gap
        low = np.where(L[i0:] < med - depth_db)[0] + i0
        if len(low) and np.any(L[low[0]:] > med - 6):  # it comes back: a gap, not the note's own decay
            out.append((k, round(on + 0.08 + low[0] * 0.01, 3)))
    return out
