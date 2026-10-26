# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Regression proof: the clarinet's objective (the clarinet lane's evalp.py) from the shared core, the
shipped presets as they are, on the fit grid and the hold-out.

  AWVA_RENDER=<awva_render built on the clarinet engine's commit> AWVA_REFS=<cache with 'clarinet'> \
  AWVA_FIT_WORK=<dir> python tools/fit/regress/clarinet.py
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit import env  # noqa: E402
from awvafit.instruments import clarinet as C  # noqa: E402


def main():
    if '--lane' in sys.argv:  # the lane's renderer: no settling (its exact numbers)
        C.SETTLE = 0.0
    if not os.path.exists(C.work('refs.pkl')):
        C.refs(log=lambda *a: None)
    for p in (0, 1):
        for which in ('fit', 'hold'):
            keys, ref = C.grid(p, which)
            f, c = C.evaluate(env.renderer(), p, {}, keys, ref, 99)
            print(f'preset {p} {which:4s} {len(keys)} notes J {f!r} | {C.OBJECTIVE.fmt(c)} g={c["_g"]:+.1f}', flush=True)


if __name__ == '__main__':
    main()
