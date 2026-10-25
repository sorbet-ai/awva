# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Robustness sweep: every note at several pressures; per play the strongest spectral peak's pitch
re the note (cents, the octave folded out: 'oct' says which multiple), and the share of the energy
in the note's harmonics (+-1.5 %). Bad: the peak isn't a harmonic of the note, the note's own
fundamental is more than 20 dB below the peak, or less than 90 % of the energy is harmonic.
  python -I sweep2.py <bintag> <preset> <lo> <hi> [pressures] [name=value ...]"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe
SR = 48000
bt, preset, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
pres = [float(v) for v in sys.argv[5].split(',')] if len(sys.argv) > 5 and '=' not in sys.argv[5] else [0.04, 0.12, 0.4, 0.8, 1.2]
params = dict(a.split('=') for a in sys.argv[5:] if '=' in a)
plays = [(n, P) for n in range(lo, hi + 1) for P in pres]
x, evs, err = probe.render(bt, preset, plays, params, None, 0.6, 0.9, tag=f'sw2_{preset}')
res = {}
N = 16384
for (n, P, t0, t1) in evs:
    a = int((t0 + 0.4) * SR); seg = x[a:a + N] * np.hanning(N)
    S = np.abs(np.fft.rfft(seg)) ** 2; f = np.arange(len(S)) * SR / N
    f0 = 440 * 2 ** ((n - 69) / 12)
    tot = S[f > 50].sum() + 1e-30
    k = np.arange(1, int(20000 / f0) + 1)
    harm = sum(S[(f > kk * f0 * 0.985) & (f < kk * f0 * 1.015)].sum() for kk in k)
    i = int(np.argmax(S * (f > 50))); fp = f[i]
    ratio = fp / f0; m = max(1, int(round(ratio)))
    c = 1200 * np.log2(fp / (m * f0)) if ratio > 0.7 else -9999
    h1 = S[(f > f0 * 0.985) & (f < f0 * 1.015)].sum()
    lvl = 10 * np.log10(np.mean(x[a:a + N] ** 2) + 1e-20)
    bad = (ratio < 0.7) or abs(c) > 25 or h1 < S[i] * 0.01 or harm / tot < 0.9
    res.setdefault(n, []).append((P, bad, c, harm / tot, m, lvl))
nb = sum(1 for v in res.values() for r in v if r[1])
print(f"preset {preset} {lo}-{hi}: {nb}/{len(evs)} plays bad")
for n, v in sorted(res.items()):
    if any(r[1] for r in v):
        print(f"  {n}: " + '  '.join(f"{P}:{'BAD' if b else 'ok'} {c:+.0f}c h{hr:.2f} x{m} {l:.0f}dB" for P, b, c, hr, m, l in v))
