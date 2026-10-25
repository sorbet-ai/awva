# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
import numpy as np, sys, subprocess, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model  # noqa: E402
ROOT = model.ROOT
"""imp.py <bintag> <preset> <note> [name=value ...]: the bore's input impedance peaks at a note."""
bintag, pre, note = sys.argv[1], sys.argv[2], float(sys.argv[3])
extra = sys.argv[4:]
os.makedirs(os.path.join(ROOT, 'tmp'), exist_ok=True)
o = os.path.join(ROOT, 'tmp', 'imp.f32')
r = subprocess.run([os.path.join(ROOT, 'bin', bintag, 'imp' + model.EXE), pre, str(note), o] + extra, capture_output=True, text=True)
print(r.stdout.strip())
p = np.fromfile(o, np.float32).astype(np.float64)
Z = np.fft.rfft(p)
f = np.fft.rfftfreq(len(p), 1 / 48000)
f0 = 440 * 2 ** ((note - 69) / 12)
mag = np.abs(Z)
# peaks
pk = [i for i in range(1, len(mag) - 1) if mag[i] > mag[i - 1] and mag[i] > mag[i + 1] and f[i] < 4000 and mag[i] > 2]
for i in pk[:12]:
    # parabolic refine
    a, b, c = np.log(mag[i - 1:i + 2]); d = 0.5 * (a - c) / (a - 2 * b + c)
    fp = f[i] + d * (f[1] - f[0])
    print(f"peak {fp:8.2f} Hz  = {fp / f0:6.3f} f0  ({1200 * np.log2(fp / (f0 * round(fp / f0))) if round(fp/f0)>0 else 0:+6.1f} c re {round(fp / f0)} f0)  Zp {mag[i]:6.1f}")
