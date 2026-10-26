# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Regression proof: the brass family's published distances (finish --preset) from the shared core.

  AWVA_RENDER=<awva_render built on the engine's commit> AWVA_CAPTURES=<captured engine controls> \\
  AWVA_FIT_WORK=<dir> python tools/fit/regress/brassfamily.py v2|v3 [inst ...]

Builds the references (the brass engine on the captured controls), then scores the trumpet engine's
presets as they are, on the fit and the hold-out sets, and compares with the published table
(tools/brassfamily/README.md of the brass family's branch). Exit 1 if any differs in the 3rd decimal.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit.instruments import brassfamily as bf  # noqa: E402

PUBLISHED = {  # (fit, hold-out), from the captures (BF_CAPTURES)
    'v2': dict(flugelhorn=(16.698, 8.627), trombone=(24.931, 18.195), horn=(19.466, 10.300), euphonium=(18.646, 10.088), tuba=(20.674, 8.508)),
    'v3': dict(flugelhorn=(16.467, 8.592), trombone=(24.415, 17.506), horn=(19.229, 10.120), euphonium=(18.458, 9.031), tuba=(19.831, 8.630)),
}


def main():
    ver = sys.argv[1]
    insts = sys.argv[2:] or list(bf.INST)
    bf.refs(insts, log=lambda *a: None)
    bad = 0
    for inst in insts:
        r = bf.finish(inst)
        pf, ph = PUBLISHED[ver][inst]
        ok = f"{r['fit_total']:.3f}" == f'{pf:.3f}' and f"{r['hold_total']:.3f}" == f'{ph:.3f}'
        bad += not ok
        print(f"{ver} {inst:10s} fit {r['fit_total']:.6f} (published {pf:.3f}) hold-out {r['hold_total']:.6f} (published {ph:.3f}) "
              f"{'same' if ok else 'DIFFERENT'}", flush=True)
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
