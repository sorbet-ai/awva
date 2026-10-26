# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The standard expression phrases, as key streams (keys.Stream) in the trumpet's register; an
instrument's config moves them to its own (awva_render --transpose). The same phrases go to every
engine version, so fits and listening packs compare like with like.

  strike    the strike sweep: 8 strikes x 2 pressures x 2 notes, held 0.6 s
  pressure  the pressure sweep: 8 sustain pressures x 2 strikes x 2 notes
  cresc     crescendo-decrescendo (soft strike) and messa di voce (medium), on 3 notes
  regs      the registers: 13 notes 41..83, soft and loud
  stacc     staccato: 4 notes x 8 short presses
  slurs     legato: the next key pressed softly before the last is released (30 ms overlap)
  tilts     pitch bends (sideways) and forward/back tilt on held notes
  grid      3 strikes x 3 pressures x 2 notes

  make(name) -> Stream;  write_all(dir) writes syn_<name>.csv (+ .notes.json) for every phrase
"""
import math
import os

from .keys import KEYS_OF, Stream

STRIKES = [0.08, 0.15, 0.25, 0.4, 0.55, 0.7, 0.85, 1.0]
PRESSES = [0.06, 0.12, 0.2, 0.3, 0.42, 0.55, 0.7, 0.9]


def strike_sweep():
    s = Stream()
    t = 0.0
    for P in (0.25, 0.6):
        for note in (60, 67):
            for v in STRIKES:
                t = s.press(t, v, P, hold=0.6, note=note) + 0.35
    return s


def pressure_sweep():
    s = Stream()
    t = 0.0
    for v in (0.2, 0.7):
        for note in (60, 67):
            for P in PRESSES:
                t = s.press(t, v, P, hold=0.6, note=note) + 0.35
    return s


def crescendo():
    s = Stream()
    t = 0.0
    for note in (55, 64, 72):
        sh = lambda u: (0.06 + 0.94 * min(u / 2.5, 1.0)) if u < 2.5 else max(1.0 - (u - 2.5) / 2.0 * 0.94, 0.06)  # noqa: E731
        t = s.press(t, 0.12, 0.9, hold=4.5, note=note, shape=sh) + 0.5
        # messa di voce at a medium strike
        sh2 = lambda u: 0.15 + 0.65 * math.sin(math.pi * min(u / 3.0, 1.0)) ** 2  # noqa: E731
        t = s.press(t, 0.5, 1.0, hold=3.0, note=note, shape=sh2) + 0.5
    return s


def registers():
    s = Stream()
    t = 0.0
    for note in (41, 45, 48, 52, 55, 58, 62, 65, 69, 72, 76, 79, 83):
        if note not in KEYS_OF:
            continue
        for v, P in ((0.3, 0.3), (0.85, 0.6)):
            t = s.press(t, v, P, hold=0.5, note=note) + 0.3
    return s


def staccato():
    s = Stream()
    t = 0.0
    for note, v, P in ((60, 0.9, 0.55), (67, 0.6, 0.4), (55, 0.35, 0.3), (64, 1.0, 0.7)):
        for k in range(8):
            t = s.press(t, v, P, hold=0.045, rel=0.015, note=note) + 0.075
        t += 0.4
    return s


def slurs():
    """Legato: each next key pressed softly before the last is released (overlap 30 ms)."""
    s = Stream()
    t = 0.0
    for phrase in ((60, 62, 64, 65, 67, 65, 64, 62, 60), (55, 60, 64, 67, 72, 67, 64, 60, 55)):
        tt = t
        for j, note in enumerate(phrase):
            v = 0.5 if j == 0 else 0.12
            end = s.press(tt, v, 0.45, hold=0.32 if j < len(phrase) - 1 else 0.6, note=note)
            tt = end - 0.03 - 0.02 if j < len(phrase) - 1 else end
        t = tt + 0.6
    return s


def tilts():
    s = Stream()
    t = 0.0
    # pitch bends (sideways) on a held note, then forward/back tilt
    t = s.press(t, 0.5, 0.45, hold=2.5, note=64, bend=lambda u: 0.6 * math.sin(2 * math.pi * 0.6 * max(u - 0.3, 0))) + 0.4
    t = s.press(t, 0.5, 0.45, hold=2.5, note=64, tilt=lambda u: 0.8 * math.sin(2 * math.pi * 0.5 * max(u - 0.3, 0))) + 0.4
    t = s.press(t, 0.6, 0.55, hold=2.0, note=57, bend=lambda u: -0.8 * min(max(u - 0.5, 0) / 1.0, 1.0)) + 0.4
    return s


def grid():
    s = Stream()
    t = 0.0
    for note in (57, 70):
        for v in (0.12, 0.45, 1.0):
            for P in (0.12, 0.4, 0.75):
                t = s.press(t, v, P, hold=0.5, note=note) + 0.35
    return s


PHRASES = dict(strike=strike_sweep, pressure=pressure_sweep, cresc=crescendo, regs=registers, stacc=staccato,
               slurs=slurs, tilts=tilts, grid=grid)


def make(name):
    return PHRASES[name]()


def write_all(out, names=None):
    """syn_<name>.csv and .notes.json for each phrase into out; returns {name: path}"""
    os.makedirs(out, exist_ok=True)
    paths = {}
    for name in names or PHRASES:
        s = make(name)
        p = os.path.join(out, f'syn_{name}.csv')
        s.write(p)
        paths[name] = p
    return paths


# --- the listening phrases (pack.py): five short musical phrases around a centre note c -------------
SCALE = [0, 2, 4, 5, 7, 9, 11, 12]
PACK_PHRASES = [
    ('1_scale', 'a slow scale at mf: eight tongued notes up an octave, 0.5 s each'),
    ('2_swells', 'p to ff swells (2.5 s) on a low, a middle and a high note'),
    ('3_staccato', 'fast tongued staccato: 16 notes, 50 ms each, about 7.7 notes per second'),
    ('4_legato', 'a legato line: each next key pressed before the last lets go'),
    ('5_bend', 'a held note with a slow bend (sideways tilt, up and down)'),
]


def pack_phrases(c):
    """{name: Stream} of the listening phrases centred on note c (in the key layout's range)"""
    out = {}
    s, t = Stream(), 0.0
    for d in SCALE:
        t = s.press(t, 0.35, 0.45, hold=0.5, note=c + d) + 0.25
    out['1_scale'] = s
    s, t = Stream(), 0.0
    for n in (c - 7, c, c + 9):
        t = s.press(t, 0.12, 1.0, hold=2.9, note=n, shape=lambda u: 0.08 + 0.92 * min(u / 2.5, 1.0)) + 0.6
    out['2_swells'] = s
    s, t = Stream(), 0.0
    for d in (0, 0, 0, 2, 4, 4, 2, 0, 7, 7, 5, 4, 2, 2, 0, 0):
        t = s.press(t, 0.75, 0.5, hold=0.05, rel=0.015, note=c + d) + 0.065
    out['3_staccato'] = s
    s, tt = Stream(), 0.0
    line = [0, 2, 4, 5, 7, 9, 11, 12, 7, 4, 0]
    for j, d in enumerate(line):
        end = s.press(tt, 0.5 if j == 0 else 0.12, 0.5, hold=0.38 if j < len(line) - 1 else 0.9, note=c + d)
        tt = end - 0.05 if j < len(line) - 1 else end
    out['4_legato'] = s
    s = Stream()
    s.press(0.0, 0.4, 0.5, hold=4.0, note=c, bend=lambda u: 0.8 * math.sin(2 * math.pi * 0.4 * max(u - 0.6, 0.0)))
    out['5_bend'] = s
    return out
