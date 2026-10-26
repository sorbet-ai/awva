# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""In-range robustness: notes at pressures; per play bad if the strongest component isn't a harmonic of
the note (25 cents), a subharmonic series is present (energy at the half-integer harmonics over 10 %
of the harmonic energy: period doubling, the lower register), the odd harmonics vanish (the octave
up), or under 85 % of the energy is harmonic. Also the level per play (the dynamics)."""
import numpy as np
import probe
SR = 48000


def check(bt, preset, params, notes, pres=(0.06, 0.4, 1.0), tag='rb', hold=1.3):
    plays = [(n, P) for n in notes for P in pres]
    x, evs, err = probe.render(bt, preset, plays, params, None, 0.6, hold, tag=tag)
    N = 16384
    res = []
    for (n, P, t0, t1) in evs:
        a = int((t0 + 0.9) * SR)  # after the attack's accent: the sustained level
        seg = x[a:a + N]
        if len(seg) < N:
            seg = np.concatenate([seg, np.zeros(N - len(seg), np.float32)])
        S = np.abs(np.fft.rfft(seg * np.hanning(N))) ** 2
        f = np.arange(len(S)) * SR / N
        f0 = 440 * 2 ** ((n - 69) / 12)
        tot = S[f > 30].sum() + 1e-30

        def band(fk):
            return S[(f > fk * 0.985) & (f < fk * 1.015)].sum()
        K = int(20000 / f0)
        H = np.array([band(k * f0) for k in range(1, K + 1)])
        Hs = np.array([band((k + 0.5) * f0) for k in range(0, min(K, 12))])
        harm = H.sum()
        ip = int(np.argmax(S * (f > 30)))
        r = f[ip] / f0
        m = max(1, int(round(r)))
        c = 1200 * np.log2(f[ip] / (m * f0)) if r > 0.7 else 9999
        odd, even = H[0::2][:6].sum(), H[1::2][:6].sum()
        lvl = 10 * np.log10(np.mean(seg.astype(np.float64) ** 2) + 1e-20)
        b = r < 0.7 or abs(c) > 25 or Hs.sum() > 0.1 * harm or odd < 0.01 * even or harm / tot < 0.85
        res.append((n, P, bool(b), lvl))
    return res


def bad_fraction(bt, preset, params, notes, pres=(0.06, 0.4, 1.0), tag='rb'):
    res = check(bt, preset, params, notes, pres, tag)
    return sum(r[2] for r in res) / len(res), [(r[0], r[1]) for r in res if r[2]]
