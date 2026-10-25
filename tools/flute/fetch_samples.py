# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Download the recordings the flute's presets were fitted to, into <dir> (default
$FLUTE_WORK/samples): nothing of them is in this repository.

  python -I fetch_samples.py [dir]

  iowa_mis/flute, iowa_mis/altoflute  University of Iowa Musical Instrument Samples (pre-2012):
      flute non-vibrato and alto flute, pp/mf/ff chromatic scales (AIFF, 16-bit 44.1 kHz mono).
      Terms: "freely available ... may be downloaded and used for any projects, without
      restrictions" (theremin.music.uiowa.edu/MIS.html).
  vsco2ce/piccolo  VS Chamber Orchestra 2: Community Edition, the piccolo's sustains and staccatos
      (WAV, 24-bit 44.1 kHz stereo). Licence: CC0 1.0 (github.com/sgossner/VSCO-2-CE, LICENSE).

Then: ref_segment.py (Iowa) and vsco_segment.py (VSCO) cut them into notes for ref_feats.py.
"""
import os
import sys
import urllib.parse
import urllib.request

IOWA = "https://theremin.music.uiowa.edu/sound%20files/MIS/Woodwinds"
IOWA_FILES = {
    'flute': ['Flute.nonvib.pp.B3B4', 'Flute.nonvib.pp.B4Bb5', 'Flute.nonvib.pp.C6Bb6', 'Flute.nonvib.mf.B3B4',
              'Flute.nonvib.mf.C5B5', 'Flute.nonvib.mf.C6B6', 'Flute.nonvib.mf.C7', 'Flute.nonvib.ff.B3B4',
              'Flute.nonvib.ff.C5B5', 'Flute.nonvib.ff.C6B6', 'Flute.nonvib.ff.C7Db7'],
    'altoflute': ['AltoFlute.pp.G3B3', 'AltoFlute.pp.C4B4', 'AltoFlute.pp.C5G5', 'AltoFlute.mf.G3B3',
                  'AltoFlute.mf.C4B4', 'AltoFlute.mf.C5B5', 'AltoFlute.mf.C6G6', 'AltoFlute.ff.C4B4',
                  'AltoFlute.ff.C5B5', 'AltoFlute.ff.C6G6'],
}
VSCO = "https://raw.githubusercontent.com/sgossner/VSCO-2-CE/master/Woodwinds/Piccolo"
VSCO_FILES = ['Sus/piccolo_G4_sustain1.wav', 'Sus/piccolo_C5_sustain1.wav', 'Sus/piccolo_G5_sustain1.wav',
              'Sus/piccolo_C6_sustain1.wav', 'Sus/piccolo_G6_sustain1.wav', 'Stac/piccolo_A#4_staccato1.wav',
              'Stac/piccolo_G5_staccato1.wav', 'Stac/piccolo_A#5_staccato1.wav', 'Stac/piccolo_G6_staccato1.wav',
              'Stac/piccolo_A#6_staccato1.wav']


def get(url, path):
    if os.path.exists(path):
        return
    with urllib.request.urlopen(url, timeout=120) as r, open(path + '.part', 'wb') as f:
        f.write(r.read())
    os.replace(path + '.part', path)
    print(path)


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('FLUTE_WORK', os.getcwd()), 'samples')
    for inst, names in IOWA_FILES.items():
        d = os.path.join(root, 'iowa_mis', inst)
        os.makedirs(d, exist_ok=True)
        for n in names:
            get(f"{IOWA}/{inst}/{n}.aiff", os.path.join(d, n + '.aiff'))
    d = os.path.join(root, 'vsco2ce', 'piccolo')
    os.makedirs(d, exist_ok=True)
    for f in VSCO_FILES:
        get(f"{VSCO}/{urllib.parse.quote(f)}", os.path.join(d, os.path.basename(f)))


if __name__ == '__main__':
    main()
