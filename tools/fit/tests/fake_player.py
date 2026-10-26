# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""A stand-in build for the gate's tests: plays a key stream's notes (its .notes.json) as harmonic tones.
  python fake_player.py <csv> <out.f32> <transpose> <brightness>"""
import json
import sys

import numpy as np

SR = 48000
csv, out, transpose, bright = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4])
notes = json.load(open(csv[:-4] + '.notes.json'))
t0 = notes[0]['t_on']
x = np.zeros(int((notes[-1]['t_off'] - t0 + 1.0) * SR))
for n in notes:
    a, b = int((n['t_on'] - t0) * SR), int((n['t_off'] - t0) * SR)
    t = np.arange(b - a) / SR
    f = 440 * 2 ** ((n['note'] + transpose - 69) / 12)
    env = np.minimum(1, t / 0.03) * np.minimum(1, (t[-1] - t) / 0.02 + 1e-9)
    x[a:b] += 0.2 * n['P'] * env * sum(np.sin(2 * np.pi * f * k * t) * bright ** (k - 1) for k in range(1, 9))
x.astype(np.float32).tofile(out)
