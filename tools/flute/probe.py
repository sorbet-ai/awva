# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Render a few held notes on the model and print their features.
  python -I probe.py <bintag> <preset> "notes" "pressures" [name=value ...] [--sign s] [--strike v] [--wav out]
notes / pressures: comma lists; every note is played at every pressure, 1.0 s each, on voice 0."""
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT, replay  # noqa: E402
sys.path.insert(0, HERE)
import ctl  # noqa: E402
import feat  # noqa: E402


def render(bintag, preset, plays, params, sign=None, strike=0.5, hold=1.0, tag='probe'):
    total = len(plays) * (hold + 0.65) + 1.0
    c = ctl.Ctl(total)
    t = 0.2
    evs = []
    for (n, P) in plays:
        t_end = c.note(0, t, n, P, strike, hold)
        evs.append((n, P, t, t_end - 0.02))
        t = t_end + 0.4
    d = os.path.join(ROOT, 'tmp', tag)
    os.makedirs(d, exist_ok=True)
    cp, op = os.path.join(d, 'p.ctl'), os.path.join(d, 'p.f32')
    c.write(cp)
    cmd = [replay(bintag), '--engine', 'flute', '--preset', str(preset)]
    if sign is not None:
        cmd += ['--sign', str(sign)]
    for k, v in params.items():
        cmd += ['--set', f'{k}={v}']
    cmd += ['--job', cp, op]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        print(r.stderr, r.stdout)
    x = np.fromfile(op, np.float32)
    return x, evs, r.stderr


def main():
    a = sys.argv[1:]
    bintag, preset = a[0], int(a[1])
    notes = [float(v) for v in a[2].split(',')]
    pres = [float(v) for v in a[3].split(',')]
    params, sign, strike, wav = {}, None, 0.5, None
    i = 4
    while i < len(a):
        if a[i] == '--sign':
            sign = float(a[i + 1]); i += 2
        elif a[i] == '--strike':
            strike = float(a[i + 1]); i += 2
        elif a[i] == '--wav':
            wav = a[i + 1]; i += 2
        else:
            k, v = a[i].split('='); params[k] = float(v); i += 1
    plays = [(n, p) for n in notes for p in pres]
    x, evs, err = render(bintag, preset, plays, params, sign, strike)
    if err.strip():
        print(err.strip()[:500])
    print(f"peak {np.abs(x).max():.3f}")
    for (n, P, t0, t1) in evs:
        f = feat.note_feats(x, n, t0, t1)
        h = ' '.join(f"{v:6.1f}" for v in f['harm'][:8])
        nz = ' '.join(f"{v:6.1f}" for v in f['noise'])
        print(f"n {n:5.1f} P {P:4.2f} lvl {f['lvl']:6.1f} f0c {f['f0c']:+6.1f} per {f['per']:.2f} sub {f['sub']:+5.1f} up {f['oct_up']:+5.1f} rise {f['rise']*1000:4.0f}ms | H {h} | N {nz}")
    if wav:
        import wave
        y = np.clip(x / max(np.abs(x).max(), 1e-9) * 0.9, -1, 1)
        with wave.open(wav, 'wb') as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000)
            w.writeframes((y * 32767).astype('<i2').tobytes())


if __name__ == '__main__':
    main()
