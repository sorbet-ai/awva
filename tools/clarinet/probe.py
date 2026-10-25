# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
import numpy as np, sys, subprocess, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model  # noqa: E402
ROOT = model.ROOT
"""probe.py <bintag> <preset> <note> <pres> [name=value ...]: harmonics of the mouthpiece pressure, flow, reed and output."""
bintag, pre, note, pres = sys.argv[1], sys.argv[2], float(sys.argv[3]), sys.argv[4]
os.makedirs(os.path.join(ROOT, 'tmp'), exist_ok=True)
o = os.path.join(ROOT, 'tmp', 'probe')
subprocess.run([os.path.join(ROOT, 'bin', bintag, 'probe' + model.EXE), pre, str(note), pres, o] + sys.argv[5:], check=True)
f0 = 440 * 2 ** ((note - 69) / 12); sr = 48000
for k in ('p', 'u', 'x', 'y'):
    s = np.fromfile(o + '.' + k, np.float32).astype(float)[24000:48000]
    s = s - s.mean()
    S = np.abs(np.fft.rfft(s * np.hanning(len(s)))) ** 2; df = sr / len(s)
    H = [S[int(round(h * f0 / df)) - 3:int(round(h * f0 / df)) + 4].sum() for h in range(1, 7)]
    print(k, 'rms %.3f' % np.sqrt(np.mean(s ** 2)), ' '.join(f'{10*np.log10(v/H[0]):6.1f}' for v in H))
