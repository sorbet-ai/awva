# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Listener-set constraints: what is wanted that recordings can't show. The Iowa flute rises as
slowly as flute v1, which listening tagged slow-attack: a distance to the recordings would keep that
slowness. A constraint states the wish directly (a maximum rise time per strike, say) and adds a
penalty when a candidate breaks it; a listener sets the numbers, the fit never moves them.

  c = [MaxRise(strikes=(0.4, 1.0), ms=50), MaxRise(strikes=(0.07, 0.4), ms=120)]   the flute's wish
  rep = evaluate(c, 'flute', 0, params, notes=[62, 69, 76])
  rep.penalty (add it to the objective's total), rep.rows (every note: strike, measured, limit)

Each constraint renders its own test notes through the key path (stimuli.strike_set at its strikes,
pressure P) and measures every note from its key's onset (the controls the engine got):
  MaxRise / MinRise   ms from the key until the level is within 3 dB of the note's sustain (from 250 ms)
  MaxLatency          ms from the key to the first sound (40 dB under the sustain)
  Bound               any features.measures.note_measures value per note within [lo, hi]
The penalty: per note, how far past the limit in units of `tol`, averaged over the notes, times weight.
"""
import os
from dataclasses import dataclass

import numpy as np

from . import env
from .controls import note_events, read
from .features.measures import level_env, note_measures
from .render import Renderer
from .stimuli import strike_set

SR = 48000
BLOCK = 32


def key_rise(x, t_key, off, sr=SR, db=-3.0, sus_from=0.25):
    """ms from the key until the 5 ms level is within |db| of the sustain (from sus_from s on)"""
    L, h = level_env(x, sr)
    a, b = int(t_key * sr / h), int(off * sr / h)
    s0 = min(a + int(sus_from * sr / h), b - 1)
    sus = 10 * np.log10(np.mean(10 ** (L[s0:b] / 10)) + 1e-30)
    w = np.where(L[a:b] >= sus + db)[0]
    return float(w[0] * h / sr * 1000) if len(w) else float('inf')


@dataclass
class MaxRise:
    strikes: tuple = (0.07, 0.3)
    ms: float = 120.0
    tol: float = 10.0
    weight: float = 1.0
    P: float = 0.4
    levels: tuple = (0.07, 0.12, 0.2, 0.3)  # the strikes rendered (those inside `strikes` count)

    def measure(self, x, t_key, off):
        return key_rise(x, t_key, off)

    def excess(self, v):
        return max(0.0, v - self.ms) / self.tol


@dataclass
class MinRise(MaxRise):
    def excess(self, v):
        return max(0.0, self.ms - v) / self.tol


@dataclass
class MaxLatency(MaxRise):
    ms: float = 30.0

    def measure(self, x, t_key, off):
        return key_rise(x, t_key, off, db=-40.0)


@dataclass
class Bound:
    """a note_measures value (e.g. 'cents', 'tilt', 'bright') within [lo, hi]"""
    key: str = 'cents'
    lo: float = -10.0
    hi: float = 10.0
    tol: float = 1.0
    weight: float = 1.0
    strikes: tuple = (0.0, 1.0)
    P: float = 0.4
    levels: tuple = (0.2, 0.5)
    note_of: float = 0.0

    def measure(self, x, t_key, off):
        return note_measures(x, SR, self.note_of, t_key, off)[self.key]

    def excess(self, v):
        return (max(0.0, self.lo - v) + max(0.0, v - self.hi)) / self.tol


@dataclass
class Report:
    rows: list
    penalty: float

    def violations(self):
        return [r for r in self.rows if r['excess'] > 0]


def evaluate(constraints, engine, preset, params=None, notes=(60, 67, 74), exe=None, workdir=None, tag='constraints'):
    """render each constraint's notes, measure them, and the total penalty"""
    r = Renderer(exe)
    d = workdir or env.workdir('tmp', tag)
    os.makedirs(d, exist_ok=True)
    rows, pen = [], 0.0
    for i, c in enumerate(constraints):
        s, sched = strike_set(list(notes), c.levels, P=c.P)
        csv = os.path.join(d, f'c{i}.csv')
        s.write(csv, notes=False)
        out, ctl = os.path.join(d, f'c{i}.f32'), os.path.join(d, f'c{i}.ctl')
        x = r.render(engine, preset, rec=csv, sets=params, settle=1.0, tail=0.5, out=out, ctl_out=ctl).x.astype(np.float64)
        ev = note_events(read(ctl)[0])
        ex = []
        for e, sc in zip(ev, sched):
            if not (c.strikes[0] <= sc['v'] <= c.strikes[1]):
                continue
            t_key, off = e['b_on'] * BLOCK / SR, e['b_off'] * BLOCK / SR
            if isinstance(c, Bound):
                c.note_of = e['note']
            v = c.measure(x, t_key, off)
            k = c.excess(v)
            ex.append(k)
            rows.append(dict(constraint=type(c).__name__, note=e['note'], strike=sc['v'], value=v, excess=k))
        if ex:
            pen += c.weight * float(np.mean(ex))
    return Report(rows, pen)
