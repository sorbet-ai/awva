# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""features of every reference note -> ref/feats.pkl; prints a summary"""
import json, os, pickle, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from work import ROOT  # noqa: E402
sys.path.insert(0,HERE)
import feat
idx=json.load(open(os.path.join(ROOT,'ref','index.json')))
out={}
for r in idx:
    x=np.fromfile(os.path.join(ROOT,'ref',r['name']+'.f32'),np.float32)
    f=feat.note_feats(x,r['note'],r['onset'],r['off'])
    out[r['name']]=dict(r, **f)
pickle.dump(out,open(os.path.join(ROOT,'ref','feats.pkl'),'wb'))
for inst in ():
  for dyn in ('pp','mf','ff'):
    rows=[v for v in out.values() if v['inst']==inst and v['dyn']==dyn and not v['vib']]
    rows.sort(key=lambda v:v['note'])
    print(f"== {inst} {dyn}")
    for v in rows[::4]:
        h=' '.join(f"{x:6.1f}" for x in v['harm'][:8]); nz=' '.join(f"{x:6.1f}" for x in v['noise'])
        print(f"n {v['note']} dur {v['dur']:.1f} lvl {v['lvl']:6.1f} f0c {v['f0c']:+5.1f} per {v['per']:.2f} rise {v['rise']*1000:4.0f}ms | H {h} | N {nz}")
