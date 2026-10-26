# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Engine-level controls (.ctl, what awva_render --ctl plays and --ctl-out writes): float32 per
32-sample block, 10 voices x (pitch, pressure, pressure_rate, timbre), then the instrument's 5
(sustain, accel_g, spin x 3).

  c = Ctl(seconds); c.note(voice, t0, note, P, v, hold); c.write('x.ctl')   synthesise scripted notes
  read(path) -> (voices [blocks, 10, 4], globals [blocks, 5])
  note_events(vc)    the notes in the controls: voice, onset and release block, note, strike, sustain
  held_runs(vc)      every press as (voice, b_on, b_off), in time order (no note-jump split)

Ctl builds a key the way the scanner reports it (keys.py): the pressure rises along a raised cosine
to P in Tr = K pi P / (2 v), vpres = K dp/dt (K = 0.033 s), released along a raised cosine.
"""
import numpy as np

from .render import BLOCK, CTL_VOICES, CTL_WIDTH, read_ctl

SR = 48000
K = 0.033
GLOB = [3 / 16, 1, 0, 0, 0]  # the instrument at rest (awva::instrument_at_rest)

read = read_ctl


class Ctl:
    def __init__(self, seconds, sr=SR):
        self.sr = sr
        self.nb = int(seconds * sr / BLOCK)
        self.c = np.zeros((self.nb, CTL_WIDTH), np.float32)
        for v in range(CTL_VOICES):
            self.c[:, 4 * v] = 69
        self.c[:, 4 * CTL_VOICES:] = GLOB
        self.t = np.arange(self.nb) * BLOCK / sr
        self.notes = []

    def note(self, voice, t0, note, P, v, hold, rel=0.02, shape=None, tilt=0.0):
        """a note on voice at t0 (s): sustain pressure P, strike v, held `hold` s after the rise;
        shape(t): a pressure multiplier over the note (t from the onset). Returns its end."""
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
        c[live, 4 * voice] = note
        c[live, 4 * voice + 1] = np.maximum(p[live], 0)
        c[live, 4 * voice + 2] = vp[live]
        c[live, 4 * voice + 3] = tilt
        # the key path keeps the last note and vpres after the release
        after = t >= t_end + rel
        c[after, 4 * voice] = note
        self.notes.append(dict(voice=voice, t_on=float(t0), t_off=float(t0 + t_end), note=float(note), P=float(P), v=float(v)))
        return t0 + t_end + rel

    def write(self, path):
        self.c.tofile(path)


def note_events(vc, sr=SR):
    """The notes in per-block voice controls: dict(v, b_on, b_off, note, strike (the highest vpres in
    the first 60 ms), P (the median pressure from 120 ms on), legato (a note jump while held: a new
    note too)), in onset order. Presses shorter than 3 blocks are dropped."""
    held = vc[:, :, 1] > 0
    ev = []
    for v in range(vc.shape[1]):
        h = held[:, v].astype(np.int8)
        d = np.diff(np.concatenate([[0], h, [0]]))
        ons, offs = np.where(d == 1)[0], np.where(d == -1)[0]
        for a, b in zip(ons, offs):
            nt = vc[a:b, v, 0]
            cuts = [a] + [a + j + 1 for j in np.where(np.abs(np.diff(nt)) >= 1.0)[0]] + [b]
            for a2, b2 in zip(cuts[:-1], cuts[1:]):
                if b2 - a2 < 3:
                    continue
                n60 = int(0.06 * sr / BLOCK)
                strike = float(vc[a2:min(a2 + n60, b2), v, 2].max())
                s0 = min(a2 + int(0.12 * sr / BLOCK), b2 - 1)
                ev.append(dict(v=v, b_on=int(a2), b_off=int(b2), note=float(np.median(vc[a2:b2, v, 0])), strike=strike,
                               P=float(np.median(vc[s0:b2, v, 1])), legato=bool(a2 != a)))
    ev.sort(key=lambda e: e['b_on'])
    return ev


def held_runs(vc):
    """(voice, b_on, b_off) of every press, in onset order"""
    held = vc[:, :, 1] > 0
    ev = []
    for v in range(vc.shape[1]):
        d = np.diff(np.concatenate([[0], held[:, v].astype(np.int8), [0]]))
        for a, b in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
            ev.append((v, int(a), int(b)))
    ev.sort(key=lambda e: e[1])
    return ev
