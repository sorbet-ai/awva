# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""(maintenance) Write tools/fit/performances/goldens.txt: every musical test performance's dry render
hash on main's engines, which ctest checks (sdk/CMakeLists.txt: awva_render.musical.*).

  AWVA_RENDER=build/sdk/awva_render python tools/fit/regress/goldens.py

Run it after an engine change that is meant to change the sound, and say so in the commit.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit.perform import read_perf  # noqa: E402
from awvafit.render import Renderer  # noqa: E402
from awvafit.smoke import PERFORMANCES, performances  # noqa: E402

HEAD = """# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
# The musical test performances' golden hashes on main's engines (sdk/CMakeLists.txt: awva_render.musical.*).
# <performance> <engine> <preset number> <hash of the dry render>. Update: tools/fit/regress/goldens.py
"""


def main():
    r = Renderer()
    presets = r.list()
    rows = []
    for p in performances():
        meta, _, _ = read_perf(p)
        n = presets[meta['engine']].index(meta['preset'])
        out = p[:-4] + '.tmp.f32'
        h = r.render(meta['engine'], n, ump=p, out=out).manifest['hash']
        for f in (out, out + '.json'):
            os.remove(f)
        rows.append(f"{os.path.relpath(p, PERFORMANCES).replace(os.sep, '/')} {meta['engine']} {n} {h}")
    with open(os.path.join(PERFORMANCES, 'goldens.txt'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(HEAD + '\n'.join(rows) + '\n')
    print('\n'.join(rows))


if __name__ == '__main__':
    main()
