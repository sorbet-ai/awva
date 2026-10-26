# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Pressure ramps: per note the key pressure 0.03 -> 1.4 -> 0.03 over 8 s (no strike); every 20 ms
the tone's level and pitch: jumps in level (> 3 dB per 20 ms), in pitch (> 15 cents per 20 ms) and
register flips (the strongest component not the note's fundamental's harmonic series), dropouts
(tone gone while the pressure is up), and the hysteresis of the threshold.
  python -I ramp.py <bintag> <preset> <notes> [name=value ...]"""
import os
import sys, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctl
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from work import ROOT, replay  # noqa: E402
SR = 48000
bt, pre = sys.argv[1], int(sys.argv[2]); notes = [int(v) for v in sys.argv[3].split(',')]
params = dict(a.split('=') for a in sys.argv[4:])
eng = os.environ.get('REPLAY_ENGINE', 'doublereed')
tot = {'ljump': 0, 'pjump': 0, 'flip': 0, 'drop': 0, 'frames': 0}
for n in notes:
    c = ctl.Ctl(9.0)
    shape = lambda t: np.interp(t, [0, 4.0, 8.0], [1.0, 1.4 / 0.03, 1.0])
    c.note(0, 0.2, n, 0.03, 0.2, 8.0, shape=shape)
    cp = os.path.join(ROOT, 'tmp', 'ramp.ctl'); op = os.path.join(ROOT, 'tmp', 'ramp.f32'); c.write(cp)
    cmd = [replay(bt), '--engine', eng, '--preset', str(pre)]
    for k, v in params.items(): cmd += ['--set', f'{k}={v}']
    subprocess.run(cmd + ['--job', cp, op], check=True, capture_output=True)
    x = np.fromfile(op, np.float32)
    f0 = 440 * 2 ** ((n - 69) / 12); T0 = SR / f0
    N = int(max(2048, 6 * T0)) // 2 * 2; w = np.hanning(N); fr = np.fft.rfftfreq(N * 4, 1 / SR)
    lv, pc, ok, pr = [], [], [], []
    for s0 in range(int(0.4 * SR), int(8.2 * SR) - N, 960):
        S = np.abs(np.fft.rfft(x[s0:s0 + N] * w, N * 4)) ** 2
        b = lambda f: S[(fr > f * 0.97) & (fr < f * 1.03)].sum()
        h = [b(k * f0) for k in (1, 2, 3)]
        tone = sum(h); lv.append(10 * np.log10(tone + 1e-20))
        m = (fr > f0 * 0.95) & (fr < f0 * 1.05); j = np.argmax(S * m)
        pc.append(1200 * np.log2(fr[j] / f0))
        ip = np.argmax(S * (fr > 40)); r = fr[ip] / f0
        ok.append(r > 0.7 and abs(r - round(r)) < 0.03 * round(r) and h[0] > S[ip] * 0.01)
        pr.append(c.c[min((s0 + N // 2) // 32, len(c.c) - 1), 1])
    lv, pc, ok, pr = map(np.array, (lv, pc, ok, pr))
    sounding = lv > lv.max() - 40
    dl = np.abs(np.diff(lv)); dp = np.abs(np.diff(pc))
    lj = int(np.sum((dl > 3) & sounding[1:] & sounding[:-1]))
    pj = int(np.sum((dp > 15) & sounding[1:] & sounding[:-1] & ok[1:] & ok[:-1]))
    flip = int(np.sum(sounding & ~ok))
    drop = int(np.sum((pr > 0.3) & ~sounding))
    on = np.where(sounding)[0]
    up_on = pr[on[0]] if len(on) else None; dn_off = pr[on[-1]] if len(on) else None
    for k, v in (('ljump', lj), ('pjump', pj), ('flip', flip), ('drop', drop), ('frames', len(lv))): tot[k] += v
    rng = lv[sounding].max() - lv[sounding & (pr < 0.15)].min() if (sounding & (pr < 0.15)).any() else 0
    print(f"note {n}: speaks at pres {up_on:.2f} (up), stops at {dn_off:.2f} (down); level jumps {lj}, pitch jumps {pj}, register-off frames {flip}, dropouts {drop}; level range {rng:.0f} dB; pitch over the ramp {pc[sounding & ok].min():+.0f}..{pc[sounding & ok].max():+.0f} c")
print('TOTAL', tot)
