# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Regression proof: the flute's distances on its recordings (the flute lane's evalm.py) from the
shared core, the shipped presets as they are.

  AWVA_RENDER=<awva_render built on the flute engine's commit> AWVA_REFS=<cache with the 'flute' set> \
  AWVA_FIT_WORK=<dir> python tools/fit/regress/flute.py

Segments and analyses the recordings, then scores the flute (0), alto flute (1) and piccolo (2)
presets on every recorded note (--every 1). The piccolo's published distance (the flute v4 commit:
"9.20 -> 6.79") is under its fit's weights (PICCOLO: trise x3, no level or dynamics).
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit import env  # noqa: E402
from awvafit.instruments import flute  # noqa: E402


def main():
    if '--lane' in sys.argv:  # the lane's renderer: no settling (its exact numbers)
        flute.SETTLE = 0.0
    if not os.path.exists(flute.work('ref', 'feats.pkl')):
        flute.refs(log=lambda *a: None)
    for inst in ('flute', 'alto', 'piccolo'):
        comp, _, rows = flute.evaluate(env.renderer(), inst, {}, flute.workdir('tmp', 'eval'), 1)
        print(f"{inst:8s} {len(rows)} notes  total {comp['total']!r} | {flute.objective(inst).fmt(comp)}", flush=True)


if __name__ == '__main__':
    main()
