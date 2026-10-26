# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Regression proof: the sax presets' objective (the sax lane's sobj.py) from the shared core.

  AWVA_RENDER=<awva_render built on the sax engine's commit> AWVA_REFS=<cache with the 'sax' set> \
  AWVA_FIT_WORK=<dir> python tools/fit/regress/sax.py [preset ...]

Decodes and analyses the recordings, writes the key templates, and scores each shipped preset on its
fit set and (soprano, alto) the hold-out. The published values (tools/sax/README.md of the sax
branch, to three digits): soprano 13.0, alto 15.1, tenor 9.34, baritone 9.69.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit import env  # noqa: E402
from awvafit.instruments import sax  # noqa: E402

PUBLISHED = {0: '13.0', 1: '15.1', 2: '9.34', 3: '9.69'}


def main():
    presets = [int(a) for a in sys.argv[1:]] or [0, 1, 2, 3]
    sax.keys(log=lambda *a: None)
    if not os.path.exists(sax.work('ref', 'refs.pkl')):
        sax.refs(log=lambda *a: None)
    bad = 0
    for p in presets:
        r = sax.evaluate(env.renderer(), p, {}, sax.workdir('tmp', 'eval'), validate=True)
        t = r['fit']['total']
        ok = float(f'{t:.3g}') == float(PUBLISHED[p])
        bad += not ok
        v = f" | hold-out {r['valid']['total']:.6f}" if 'valid' in r else ''
        print(f"preset {p} {sax.NAMES[p]:9s} fit {t:.6f} (published {PUBLISHED[p]}){v} {'same' if ok else 'DIFFERENT'}", flush=True)
        print('   ', sax.OBJECTIVE.fmt(r['fit']), flush=True)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
