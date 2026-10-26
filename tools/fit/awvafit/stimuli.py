# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Test notes at the strikes and pressures real players use, measured from the datasets: the phrase
sets' key streams (phraseset; ChoraleBricks now, URMP and Bach10 when they are on disk), whose control
is inferred from the recordings (pressure from the level, the strike from how fast each attack rises).
Proper recorded takes on a pressure-sensing controller will come later; the takes in tools/harness/rec
are not used as test material.

A metric can only see what the stimuli exercise. The trumpet's stage-2 regression (tagged slow-attack)
shows only at soft strikes (0.07-0.3): at 0.3 and above the builds are the same, so a set of medium and
hard strikes misses it. Draw the test notes from real playing, with the soft strikes in.

  dist = playing()                      every press of the phrase set (family=..., kind=... to narrow it):
                                        strike, sustain pressure, length
  dist.quantiles(); dist.held()         their quantiles; the notes held 0.25 s or more
  s, sched = note_set(notes, dist)      a key stream: every note at the strike quantiles (and always
                                        0.07) x the pressure quantiles; the schedule of what was played
  s, sched = strike_set(notes, (0.07, 0.12, 0.2, 0.3), P=0.4)   the research lane's soft-strike set

From the raw key rows (a key's rows from pres > 0 to pres = 0; the highest velo in the first 60 ms /
8191, the time-weighted median pres from 120 ms on), or, with a renderer, through the key path (what the
engines get: the highest vpres in a note's first 60 ms, the median pressure from 120 ms on).
"""
import os
from dataclasses import dataclass

import numpy as np

from .keys import Stream

FULL = 8191.0
SOFT = (0.07, 0.3)


@dataclass
class Playing:
    strikes: np.ndarray
    pressures: np.ndarray
    lengths: np.ndarray

    def quantiles(self, q=(0.1, 0.25, 0.5, 0.75, 0.9)):
        return dict(strike=dict(zip(q, np.quantile(self.strikes, q))), pressure=dict(zip(q, np.quantile(self.pressures, q))),
                    length=dict(zip(q, np.quantile(self.lengths, q))))

    def held(self, min_s=0.25):
        """the notes held at least min_s (what a sustained test note stands for)"""
        m = self.lengths >= min_s
        return Playing(self.strikes[m], self.pressures[m], self.lengths[m])

    def soft_fraction(self):
        return float(np.mean((self.strikes >= SOFT[0]) & (self.strikes <= SOFT[1])))


def presses(path):
    """[(strike, pressure, length s)] of every press in one key stream"""
    a = np.genfromtxt(path, delimiter=',', skip_header=1)
    out = []
    for kid in np.unique(a[:, 2]):
        r = a[a[:, 2] == kid]
        t, p, v = r[:, 0], r[:, 3], r[:, 4]
        i = 0
        while i < len(r):
            if p[i] <= 0:
                i += 1
                continue
            j = i
            while j < len(r) and p[j] > 0:
                j += 1
            t0, t1 = t[i], t[j] if j < len(r) else t[j - 1]
            w = (t[i:j] >= t0) & (t[i:j] <= t0 + 0.06)
            strike = float(max(v[i:j][w].max(), 0.0) / FULL)
            # pressure: time-weighted over the rows from 120 ms on (each row holds until the next)
            tt = np.append(t[i:j], t1)
            dur = np.diff(tt)
            m = tt[:-1] >= t0 + 0.12
            if m.any() and dur[m].sum() > 0:
                pv, dw = p[i:j][m], dur[m]
                o = np.argsort(pv)
                c = np.cumsum(dw[o])
                P = float(pv[o][np.searchsorted(c, 0.5 * c[-1])] / FULL)
            else:
                P = float(p[i:j].max() / FULL)
            out.append((strike, P, float(t1 - t0)))
            i = j
    return out


def presses_through_key_path(path, renderer, workdir):
    """[(strike, pressure, length s)] of the notes the engines get from one take: the take through the
    key path (awva_render --rec --ctl-out, the sine engine), the strike the highest vpres in a note's
    first 60 ms, the pressure its median from 120 ms on (controls.note_events)"""
    from .controls import note_events, read
    base = os.path.join(workdir, os.path.splitext(os.path.basename(path))[0])
    renderer.render('sine', None, rec=path, out=base + '.f32', ctl_out=base + '.ctl')
    return [(e['strike'], e['P'], (e['b_off'] - e['b_on']) * 32 / 48000) for e in note_events(read(base + '.ctl')[0])]


def playing(streams=None, renderer=None, set_name='choralebricks', family=None, kind=None):
    """the presses of key streams (default: every stream of a phrase set, narrowed by family and kind):
    from the raw rows, or through the key path with a renderer"""
    if streams is None:
        from . import phraseset
        streams = sorted({i.stream for i in phraseset.load(set_name, family, kind)})
    rows = []
    for f in streams:
        if renderer is not None:
            import tempfile
            with tempfile.TemporaryDirectory() as d:
                rows += presses_through_key_path(f, renderer, d)
        else:
            rows += presses(f)
    a = np.array(rows)
    return Playing(a[:, 0], a[:, 1], a[:, 2])


def strike_set(notes, strikes=(0.07, 0.12, 0.2, 0.3), P=0.4, hold=1.6, gap=0.9):
    """every note at every strike, at pressure P: (Stream, schedule [dict(note, dyn='s<strike>', v, P, t_on, t_off)])
    (t_on, t_off: times in the written CSV, which starts 0.1 s in)"""
    s, t, sched = Stream(), 0.3, []
    for n in notes:
        for v in strikes:
            end = s.press(t, v, P, hold=hold, note=n)
            sched.append(dict(note=n, dyn=f's{v:g}', v=v, P=P, t_on=t + 0.1, t_off=end + 0.1))
            t = end + gap
    return s, sched


def note_set(notes, dist=None, strike_q=(0.1, 0.25, 0.5), pressure_q=(0.25, 0.5, 0.75), hold=1.6, gap=0.9):
    """every note at the strike quantiles of the held notes (and always the softest, 0.07) x their
    pressure quantiles"""
    dist = (dist or playing()).held()
    strikes = sorted({float(np.clip(np.quantile(dist.strikes, q), SOFT[0], 1.0)) for q in strike_q} | {SOFT[0]})
    pressures = [float(np.quantile(dist.pressures, q)) for q in pressure_q]
    s, t, sched = Stream(), 0.3, []
    for n in notes:
        for P in pressures:
            for v in strikes:
                end = s.press(t, v, P, hold=hold, note=n)
                sched.append(dict(note=n, dyn=f's{v:.2f}p{P:.2f}', v=v, P=P, t_on=t + 0.1, t_off=end + 0.1))
                t = end + gap
    return s, sched
