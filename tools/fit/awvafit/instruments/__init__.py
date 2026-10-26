# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The instruments' configurations on the shared core: what is played, what it is compared with,
the objective's terms and weights, the parameter space, and where the result is baked.

  brassfamily  the trumpet engine's presets 1-5 against the brass engine's presets (framewise)
  sax          the sax engine's presets against the Iowa MIS and MTG recordings (notewise)
  flute        the flute engine against the Iowa MIS (flute, alto flute) and VSCO-2 (piccolo)
  clarinet     the clarinet engine against the Iowa MIS (Bb, bass)

Each is a module with the same shape: SETS / inputs, OBJECTIVE (awvafit.objective.Objective), SPACE
(awvafit.fit.Space bounds), evaluate(...), and its bake target. A new instrument copies the nearest.
"""
import importlib

NAMES = ('brassfamily', 'sax', 'flute', 'clarinet')


def load(name):
    if name not in NAMES:
        raise KeyError(f'no instrument config {name!r} (there are: {", ".join(NAMES)})')
    return importlib.import_module(f'{__name__}.{name}')
