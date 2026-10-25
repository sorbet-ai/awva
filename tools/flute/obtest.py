# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Where overblowing engages: one held note per run, the key pressure ramped from 0.2 to 1.5 and
back over 6 s; per 50 ms the register (H1 vs the odd/even split): the pressure where the note leaves
its register going up, and where it comes back going down.
  python -I obtest.py <bintag> <preset> <notes> [name=value ...]"""
import sys, os, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ctl, subprocess
from work import ROOT, replay
bt, pre = sys.argv[1], int(sys.argv[2]); notes = [int(v) for v in sys.argv[3].split(',')]
params = dict(a.split('=') for a in sys.argv[4:])
SR = 48000
for n in notes:
    c = ctl.Ctl(7.0)
    shape = lambda t: np.interp(t, [0, 0.3, 3.3, 6.3], [1.0, 1.0, 7.5, 1.0])  # x0.2: 0.2 -> 1.5 -> 0.2
    c.note(0, 0.2, n, 0.2, 0.5, 6.2, shape=shape)
    cp = os.path.join(ROOT, 'tmp', 'ob.ctl'); op = os.path.join(ROOT, 'tmp', 'ob.f32'); c.write(cp)
    cmd = [replay(bt), '--engine', 'flute', '--preset', str(pre)]
    for k, v in params.items(): cmd += ['--set', f'{k}={v}']
    subprocess.run(cmd + ['--job', cp, op], check=True, capture_output=True)
    x = np.fromfile(op, np.float32)
    pr = c.c[:, 1]
    f0 = 440 * 2 ** ((n - 69) / 12); N = 4096; fr = np.fft.rfftfreq(N, 1 / SR)
    reg = []
    for i in range(int(0.5 * SR), int(6.3 * SR), 2400):
        S = np.abs(np.fft.rfft(x[i:i + N] * np.hanning(N))) ** 2
        b = lambda f: S[(fr > f * 0.97) & (fr < f * 1.03)].sum() + 1e-20
        h1, h2, h3 = b(f0), b(2 * f0), b(3 * f0)
        r = 2 if h1 < h2 / 30 else 1  # the fundamental 15 dB under the octave: overblown
        if r == 1 and h1 < h3 / 30 and h2 < h3 / 30: r = 3
        reg.append((pr[min((i + N // 2) // 32, len(pr) - 1)], r, (i + N // 2) / SR))
    up = [p for p, r, t in reg if r > 1 and t < 3.5]
    dn = [p for p, r, t in reg if r > 1 and t > 3.5]
    s = ''.join(str(r) for p, r, t in reg[::2])
    print(f"note {n}: overblows going up at pres {min(up) if up else '-'}" + (f" (to register x{max(r for p,r,t in reg)})" if up else '') + f", back below {min(dn) if dn else '-'} going down | {s}")
