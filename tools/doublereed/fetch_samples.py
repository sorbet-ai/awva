# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Download the recordings the double reed's presets were fitted to, into <dir> (default
$DR_WORK/samples): nothing of them is in this repository.

  python -I fetch_samples.py [dir]

  iowa_mis/oboe, iowa_mis/bassoon  University of Iowa Musical Instrument Samples (pre-2012): pp/mf/ff
      chromatic scales (AIFF, 16-bit 44.1 kHz mono). Terms: "freely available ... may be downloaded and
      used for any projects, without restrictions" (theremin.music.uiowa.edu/MIS.html). The fit set.
  vsco2ce/oboe, vsco2ce/bassoon  VS Chamber Orchestra 2: Community Edition, sustained notes (WAV).
      Licence: CC0 1.0 (github.com/sgossner/VSCO-2-CE, LICENSE). Validation only.

The cor anglais has no recordings here: none with a permissive licence were found (the oboe's fitted
player plays it on its geometry). Then ref_segment.py cuts the Iowa scales into notes for ref_feats.py.
"""
import json
import os
import sys
import urllib.parse
import urllib.request

IOWA = "https://theremin.music.uiowa.edu/sound%20files/MIS/Woodwinds"
IOWA_FILES = {
    'oboe': [f"Oboe.{d}.{r}" for d in ('pp', 'mf', 'ff') for r in ('Bb3B3', 'C4B4', 'C5B5', 'C6Ab6')],
    'bassoon': [f"Bassoon.{d}.{r}" for d in ('pp', 'mf', 'ff') for r in ('Bb1B1', 'C2B2', 'C4B4', 'C5D5')]
    + ['Bassoon.ff.C3Bb3', 'Bassoon.mf.C3B3', 'Bassoon.pp.C3B3'],
}
VSCO_TREE = "https://api.github.com/repos/sgossner/VSCO-2-CE/git/trees/master?recursive=1"
VSCO_RAW = "https://raw.githubusercontent.com/sgossner/VSCO-2-CE/master/"
VSCO_DIRS = {'oboe': 'Woodwinds/Oboe/Sus/', 'bassoon': 'Woodwinds/Bassoon/sus/'}


def get(url, path):
    if os.path.exists(path):
        return
    with urllib.request.urlopen(url, timeout=120) as r, open(path + '.part', 'wb') as f:
        f.write(r.read())
    os.replace(path + '.part', path)
    print(path)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('DR_WORK', os.getcwd()), 'samples')
    for inst, names in IOWA_FILES.items():
        d = os.path.join(root, 'iowa_mis', inst)
        os.makedirs(d, exist_ok=True)
        for n in names:
            get(f"{IOWA}/{inst}/{n}.aiff", os.path.join(d, n + '.aiff'))
    with urllib.request.urlopen(VSCO_TREE, timeout=120) as r:
        tree = [t['path'] for t in json.load(r)['tree'] if t['type'] == 'blob']
    for inst, prefix in VSCO_DIRS.items():
        d = os.path.join(root, 'vsco2ce', inst)
        os.makedirs(d, exist_ok=True)
        for p in tree:
            if p.startswith(prefix) and p.lower().endswith('.wav'):
                get(VSCO_RAW + urllib.parse.quote(p), os.path.join(d, os.path.basename(p)))


if __name__ == '__main__':
    main()
