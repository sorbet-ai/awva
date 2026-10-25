# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""h2.py <bintag> <preset> [k=v ...]: the chalumeau's even harmonics, model vs recordings (radiated h2, h4 re h1, dB)."""
import os, sys, pickle
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import model
a = sys.argv[1:]; bt, pr = a[0], int(a[1]); params = dict(kv.split('=') for kv in a[2:])
R = pickle.load(open(os.path.join(model.ROOT, 'refs.pkl'), 'rb'))['bb_clarinet' if pr == 0 else 'bass_clarinet']
ref = {(F['note'], F['dyn']): F for F in R}
low = 50 if pr == 0 else 37
notes = [low + k for k in (0, 3, 6, 9, 12, 15)]
X = model.render(bt, pr, params, [(f'n{n}{d}', n, d) for n in notes for d in ('mf', 'ff')], os.path.join(model.ROOT, 'tmp', 'h2'))
out = []
for d in ('mf', 'ff'):
    m2, r2, m4, r4 = [], [], [], []
    for n in notes:
        M = model.features(X[f'n{n}{d}'], n, d); Rf = ref.get((n, d))
        m2.append(M['harm'][1] - M['harm'][0]); m4.append(M['harm'][3] - M['harm'][0])
        if Rf: r2.append(Rf['harm'][1] - Rf['harm'][0]); r4.append(Rf['harm'][3] - Rf['harm'][0])
    out.append(f"{d}: h2-h1 model {np.mean(m2):6.1f} ref {np.mean(r2):6.1f} | h4-h1 model {np.mean(m4):6.1f} ref {np.mean(r4):6.1f}")
print(' || '.join(out))
