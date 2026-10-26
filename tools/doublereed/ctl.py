# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Engine-level control files (capture .ctl layout: per 32-sample block 10 voices x (note, pres,
vpres, x, y) + 8 globals) for scripted notes, the key built as the scanner reports it: the pressure
rises along a raised cosine to P in Tr = K pi P / (2 v), vpres = K dp/dt (K = 0.033 s), released
along a 20 ms raised cosine."""
import numpy as np

SR, BLOCK, K = 48000, 32, 0.033
GLOB = [1, 0, 0, -1, 0, 0, 0, 3 / 16]


class Ctl:
    def __init__(self, seconds):
        self.nb = int(seconds * SR / BLOCK)
        self.c = np.zeros((self.nb, 58), np.float32)
        for v in range(10):
            self.c[:, 5 * v] = 69
        self.c[:, 50:58] = GLOB
        self.t = np.arange(self.nb) * BLOCK / SR
        self.notes = []

    def note(self, voice, t0, note, P, v, hold, rel=0.02, shape=None, tilt=0.0):
        """shape(t): pressure multiplier over the note (t from the onset)."""
        v = max(v, 0.01)
        tr = max(K * np.pi * P / (2 * v), 0.004)
        t = self.t - t0
        t_end = tr + hold
        p = np.zeros(self.nb)
        m = (t >= 0) & (t < tr)
        p[m] = P * 0.5 * (1 - np.cos(np.pi * t[m] / tr))
        m = (t >= tr) & (t < t_end)
        p[m] = P
        if shape is not None:
            on = (t >= 0) & (t < t_end)
            p[on] *= shape(t[on])
        pe = p[np.searchsorted(self.t, t0 + t_end) - 1] if t0 + t_end < self.t[-1] else P
        m = (t >= t_end) & (t < t_end + rel)
        p[m] = pe * 0.5 * (1 + np.cos(np.pi * (t[m] - t_end) / rel))
        live = (t >= 0) & (t < t_end + rel)
        dp = np.gradient(p, self.t)
        vp = np.clip(K * dp, -1, 1)
        c = self.c
        c[live, 5 * voice] = note
        c[live, 5 * voice + 1] = np.maximum(p[live], 0)
        c[live, 5 * voice + 2] = vp[live]
        c[live, 5 * voice + 4] = tilt
        # the firmware keeps the last note and vpres after the release
        after = t >= t_end + rel
        c[after, 5 * voice] = note
        self.notes.append(dict(voice=voice, t_on=float(t0), t_off=float(t0 + t_end), note=float(note), P=float(P), v=float(v)))
        return t0 + t_end + rel

    def write(self, path):
        self.c.tofile(path)
