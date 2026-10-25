# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
import pickle, sys, numpy as np
R = pickle.load(open(sys.argv[1], 'rb'))
for inst, notes in R.items():
    print(inst)
    for F in sorted(notes, key=lambda F: (F['note'], F['dyn'])):
        h = F['harm'][:8]
        print(f"{F['note']:3d} {F['dyn']} lvl {F['lvl']:6.1f} c {F['cents']:+5.0f} rise {F['rise']:5.0f} rel {F['rel']:5.0f} cent {F['s_cent']:6.0f} acent {F['a_cent']:+.2f} nhr {' '.join(f'{v:5.0f}' for v in F['nhr'])} | h {' '.join(f'{v:5.1f}' for v in h)}")
