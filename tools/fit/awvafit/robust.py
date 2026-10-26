# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Robustness: every in-range note at three pressures must speak. One held note per play on voice 0
(engine controls: controls.Ctl), rendered once (awva_render --ctl), each play judged on its sustain:

  silent     the sustain level is more than `floor_db` below the loudest play's
  off pitch  the strongest spectral peak is not a harmonic of the note within `cents` cents
  no fundamental  the fundamental is more than 20 dB under the strongest peak
  noisy      under `harm_share` of the energy above 50 Hz is on the note's harmonics (+-1.5 %)

  check(engine, preset, notes, pressures=(0.12, 0.4, 0.9), sets=None) -> Report(bad, plays, fraction)

The flute lane's robust.py is this check with pressures (0.04, 0.4, 1.0), strike 0.6, held 0.7 s.
A fit uses fraction as a penalty (3 x fraction / 0.05 in the piccolo's) or as a gate.
"""
import os
from dataclasses import dataclass, field

import numpy as np

from . import env
from .controls import Ctl
from .render import Renderer

SR = 48000


@dataclass
class Report:
    plays: list
    bad: list = field(default_factory=list)  # (note, pressure, why)

    @property
    def fraction(self):
        return len(self.bad) / max(len(self.plays), 1)


def judge(x, sr, note, t0, cents=25.0, harm_share=0.85, n=8192):
    """(level dB, why or None) of one held note's sustain starting t0 + 0.35 s"""
    a = int((t0 + 0.35) * sr)
    seg = np.asarray(x[a:a + n], np.float64)
    if len(seg) < n:
        seg = np.concatenate([seg, np.zeros(n - len(seg))])
    lvl = 10 * np.log10(np.mean(seg ** 2) + 1e-20)
    S = np.abs(np.fft.rfft(seg * np.hanning(n))) ** 2
    f = np.arange(len(S)) * sr / n
    f0 = 440 * 2 ** ((note - 69) / 12)
    tot = S[f > 50].sum() + 1e-30
    harm = sum(S[(f > k * f0 * 0.985) & (f < k * f0 * 1.015)].sum() for k in range(1, int(min(20000, 0.45 * sr) / f0) + 1))
    i = int(np.argmax(S * (f > 50)))
    r = f[i] / f0
    m = max(1, int(round(r)))
    c = 1200 * np.log2(f[i] / (m * f0)) if r > 0.7 else 9999
    h1 = S[(f > f0 * 0.985) & (f < f0 * 1.015)].sum()
    if r < 0.7 or abs(c) > cents:
        return lvl, f'off pitch (peak at {f[i]:.0f} Hz)'
    if h1 < S[i] * 0.01:
        return lvl, 'no fundamental'
    if harm / tot < harm_share:
        return lvl, f'noisy ({harm / tot:.2f} harmonic)'
    return lvl, None


def check(engine, preset, notes, pressures=(0.12, 0.4, 0.9), sets=None, strike=0.6, hold=0.7, floor_db=40.0, exe=None, workdir=None, tag='robust'):
    plays = [(n, P) for n in notes for P in pressures]
    c = Ctl(len(plays) * (hold + 0.65) + 1.0)
    t = 0.2
    evs = []
    for n, P in plays:
        t_end = c.note(0, t, n, P, strike, hold)
        evs.append((n, P, t))
        t = t_end + 0.4
    d = workdir or env.workdir('tmp', tag)
    os.makedirs(d, exist_ok=True)
    cp, op = os.path.join(d, f'{engine}_{preset}.ctl'), os.path.join(d, f'{engine}_{preset}.f32')
    c.write(cp)
    x = Renderer(exe).render(engine, preset, ctl=cp, sets=sets, out=op).x
    rep = Report(plays=plays)
    lv = []
    for n, P, t0 in evs:
        lvl, why = judge(x, SR, n, t0)
        lv.append((n, P, lvl, why))
    top = max(l for _, _, l, _ in lv)
    for n, P, lvl, why in lv:
        if lvl < top - floor_db:
            why = f'silent ({lvl - top:.0f} dB)'
        if why:
            rep.bad.append((n, P, why))
    return rep
