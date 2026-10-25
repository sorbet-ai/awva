# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""In-range robustness: notes lo..hi (step) at pressures; per play bad if the strongest peak isn't the
note's harmonic within 25 cents, the fundamental is 20 dB under it, or under 85 % of the energy is
harmonic (the same test as sweep2.py, a little more lenient on breath)."""
import numpy as np
import probe
SR = 48000


def bad_fraction(bt, preset, params, notes, pres=(0.04, 0.4, 1.0), tag='rb'):
    plays = [(n, P) for n in notes for P in pres]
    x, evs, err = probe.render(bt, preset, plays, params, None, 0.6, 0.7, tag=tag)
    N = 8192; nb = 0; bad = []
    for (n, P, t0, t1) in evs:
        a = int((t0 + 0.35) * SR); seg = x[a:a + N] * np.hanning(N)
        S = np.abs(np.fft.rfft(seg)) ** 2; f = np.arange(len(S)) * SR / N
        f0 = 440 * 2 ** ((n - 69) / 12); tot = S[f > 50].sum() + 1e-30
        harm = sum(S[(f > k * f0 * 0.985) & (f < k * f0 * 1.015)].sum() for k in range(1, int(20000 / f0) + 1))
        i = int(np.argmax(S * (f > 50))); r = f[i] / f0; m = max(1, int(round(r)))
        c = 1200 * np.log2(f[i] / (m * f0)) if r > 0.7 else 9999
        h1 = S[(f > f0 * 0.985) & (f < f0 * 1.015)].sum()
        b = r < 0.7 or abs(c) > 25 or h1 < S[i] * 0.01 or harm / tot < 0.85
        nb += b
        if b: bad.append((n, P))
    return nb / len(evs), bad
