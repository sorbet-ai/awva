# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe, feat, numpy as np
bt, preset, lo, hi = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
pres = [0.06, 0.3, 0.7, 1.2]
plays = [(n, P) for n in range(lo, hi + 1) for P in pres]
x, evs, err = probe.render(bt, preset, plays, {}, None, 0.5, 0.8, tag=f'sw{preset}')
bad = {}
for (n, P, t0, t1) in evs:
    f = feat.note_feats(x, n, t0, t1)
    b = f['per'] < 0.8 or f['sub'] > -15 or f['oct_up'] > 0 or abs(f['f0c']) > 30
    if b: bad.setdefault(int(n), []).append(f"{P}:{f['f0c']:+.0f}c/{f['per']:.2f}")
print(f"preset {preset} notes {lo}-{hi}: {len(bad)} notes with a bad pressure; {sum(len(v) for v in bad.values())}/{len(evs)} plays")
for n, v in sorted(bad.items()): print(' ', n, ' '.join(v))
