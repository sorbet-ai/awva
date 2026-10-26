# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""A synthetic reference, generated, never recorded: notes of known pitch, harmonics, noise, attack
and dynamics, in the layouts the real sources use (a chromatic AIFF per dynamic, like the Iowa MIS).

  tone(note, seconds, sr, cents=0, rolloff=1, noise_db=-60, rise=0.03, glide_cents=0, glide_s=0)
  scale(notes, sr, gap=0.6, ...)            notes separated by silence, and their true onsets/offs
  write_aiff(path, x, sr)                   16-bit mono AIFF (big-endian), as Iowa's files are
"""
import math
import struct

import numpy as np


def tone(note, seconds, sr=48000, cents=0.0, rolloff=1.0, noise_db=-60.0, rise=0.03, amp=0.3, nh=20, glide_cents=0.0, glide_s=0.0,
         seed=1):
    """a harmonic tone: A_k = 1/k^rolloff, the pitch `cents` off the note (starting glide_cents
    further, settling exponentially over glide_s), a raised-cosine attack of `rise` s, white noise at
    noise_db re the tone, a 50 ms release"""
    n = int(seconds * sr)
    t = np.arange(n) / sr
    f0 = 440 * 2 ** ((note - 69) / 12) * 2 ** (cents / 1200)
    dev = glide_cents * np.exp(-t / glide_s) if glide_s > 0 else np.zeros(n)
    phase = 2 * np.pi * np.cumsum(f0 * 2 ** (dev / 1200)) / sr
    y = np.zeros(n)
    for k in range(1, nh + 1):
        if k * f0 > 0.45 * sr:
            break
        y += np.sin(k * phase) / k ** rolloff
    y /= np.sqrt(np.mean(y ** 2))
    rng = np.random.default_rng(seed)
    y += rng.standard_normal(n) * 10 ** (noise_db / 20)
    env = np.ones(n)
    a = max(int(rise * sr), 1)
    env[:a] = 0.5 * (1 - np.cos(np.pi * np.arange(a) / a))
    r = int(0.05 * sr)
    env[-r:] *= 0.5 * (1 + np.cos(np.pi * np.arange(r) / r))
    return amp * y * env


def scale(notes, sr=48000, seconds=1.2, gap=0.6, amps=None, **kw):
    """the notes one after another with silence between: (x, [(onset s, off s)])"""
    parts, times, t = [np.zeros(int(gap * sr))], [], gap
    for i, n in enumerate(notes):
        y = tone(n, seconds, sr, amp=amps[i] if amps else 0.3, seed=i + 1, **kw)
        parts += [y, np.zeros(int(gap * sr))]
        times.append((t, t + seconds))
        t += seconds + gap
    return np.concatenate(parts), times


def _ext80(v):
    """an IEEE 754 80-bit extended (the AIFF sample rate)"""
    if v == 0:
        return b'\x00' * 10
    e = int(math.floor(math.log2(v)))
    m = int(v / 2 ** e * 2 ** 63)
    return struct.pack('>HQ', e + 16383, m)


def write_aiff(path, x, sr):
    q = np.round(np.clip(x, -1, 1) * 32767).astype('>i2').tobytes()
    comm = struct.pack('>hIh', 1, len(x), 16) + _ext80(float(sr))
    ssnd = struct.pack('>II', 0, 0) + q
    body = b'AIFF' + b'COMM' + struct.pack('>I', len(comm)) + comm + b'SSND' + struct.pack('>I', len(ssnd)) + ssnd
    with open(path, 'wb') as f:
        f.write(b'FORM' + struct.pack('>I', len(body)) + body)
