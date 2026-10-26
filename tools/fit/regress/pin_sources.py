# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""(maintenance) Write awvafit/sources.json from a local copy of the recordings the lanes used:

  python tools/fit/regress/pin_sources.py <samples dir>

The local layout is the lanes' download folders: iowa_mis/<inst>, uiowa_mis/<inst>, mtg_solosax/repo,
vsco2ce/piccolo. Each file is entered under the path its source serves it at, with its size and
SHA-256, and the sets (instruments) that use it. The hashes were checked against the servers.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from awvafit import sources  # noqa: E402

IOWA = {  # local folder -> (the server's folder, set)
    'iowa_mis/alto_sax': ('Woodwinds/altosaxophone', 'sax'), 'iowa_mis/soprano_sax': ('Woodwinds/sopranosaxophone', 'sax'),
    'iowa_mis/flute': ('Woodwinds/flute', 'flute'), 'iowa_mis/altoflute': ('Woodwinds/altoflute', 'flute'),
    'iowa_mis/bassoon': ('Woodwinds/bassoon', 'doublereed'), 'iowa_mis/oboe': ('Woodwinds/oboe', 'doublereed'),
    'iowa_mis/tuba': ('Brass/tuba', 'brass'), 'iowa_mis/tenortrombone': ('Brass/tenortrombone', 'brass'),
    'iowa_mis/frenchhorn': ('Brass/frenchhorn', 'brass'), 'iowa_mis/bb_trumpet': ('Brass/Bbtrumpet', 'trumpet'),
    'uiowa_mis/bb_clarinet': ('Woodwinds/Bbclarinet', 'clarinet'), 'uiowa_mis/bass_clarinet': ('Woodwinds/bassclarinet', 'clarinet'),
}
MTG_LAYERS = ('sop_p', 'sop_f', 'alt_p', 'alt_f', 'ten_p', 'ten_f', 'bar_p', 'bar_m', 'bar_f')


def main():
    root = sys.argv[1]
    sources.pin('uiowa_mis', root, lambda rel: next((f'{srv}/{rel[len(loc) + 1:]}' for loc, (srv, _) in IOWA.items()
                                                      if rel.startswith(loc + '/') and rel.endswith('.aiff')), None),
                lambda rel: [s for srv, s in IOWA.values() if rel.startswith(srv + '/')])

    def mtg(rel):
        if not rel.startswith('mtg_solosax/repo/'):
            return None
        r = rel[len('mtg_solosax/repo/'):]
        b = os.path.basename(r)
        keep = r in ('LICENSE', 'README.md') or (r.startswith('MTG Solo Saxophones/Data/') and b.endswith('_rr1.txt')) or (
            r.startswith('MTG Solo Saxophones/Samples/') and b[:5] in MTG_LAYERS and b.endswith('.flac'))
        return r if keep else None
    sources.pin('mtg_solosax', root, mtg, lambda rel: ['sax'])

    def vsco(rel):
        if not rel.startswith('vsco2ce/piccolo/') or not rel.endswith('.wav'):
            return None
        b = os.path.basename(rel)
        return f"Woodwinds/Piccolo/{'Sus' if 'sustain' in b else 'Stac'}/{b}"
    sources.pin('vsco2ce', root, vsco, lambda rel: ['flute'])
    reg = sources.registry()
    for s in sorted({e['source'] for e in reg}):
        es = [e for e in reg if e['source'] == s]
        print(s, len(es), 'files', f"{sum(e['bytes'] for e in es) / 1e6:.0f} MB")


if __name__ == '__main__':
    main()
