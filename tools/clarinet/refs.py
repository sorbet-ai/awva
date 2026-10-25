# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Reference features from the University of Iowa MIS clarinet scales (pp / mf / ff, note by note).

  python -I refs.py <samples root> <out.pkl>

Each scale file is segmented into its notes (envelope above -50 dB of the file), the notes are named
from the file's range (e.g. D3B3), and every note's features are computed (notefeats). A note whose
measured pitch is more than 80 cents from its name is dropped (a segmentation slip).
"""
import aifc
import os
import pickle
import re
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notefeats as nf  # noqa: E402

NAMES = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}


def midi(name):
    m = re.match(r'([A-G]b?)(\d)', name)
    return 12 * (int(m.group(2)) + 1) + NAMES[m.group(1)]


def read_aiff(p):
    with aifc.open(p, 'rb') as f:
        n, ch, sw, sr = f.getnframes(), f.getnchannels(), f.getsampwidth(), f.getframerate()
        raw = f.readframes(n)
    assert sw == 2
    x = np.frombuffer(raw, '>i2').astype(np.float64) / 32768.0
    if ch > 1:
        x = x.reshape(-1, ch).mean(1)
    return x, sr


def parse(fn):
    # BbClar.pp.D3B3.aiff, BassClarinet.mf.Db2Bb2.aiff, BbClar.mf.C7.aiff
    m = re.match(r'(\w+)\.(pp|mf|ff)\.([A-G]b?\d)([A-G]b?\d)?\.aiff', fn)
    if not m:
        return None
    lo = midi(m.group(3))
    hi = midi(m.group(4)) if m.group(4) else lo
    if hi < lo:
        hi += 12
    return m.group(2), lo, hi


def floor_feats(amb, sr, F, nfft):
    """The recording's noise floor in the note's terms: per harmonic lobe re the note's harmonic
    total (dB), and per band re the band's total (dB)."""
    S = nf._spec(amb, sr, nfft)
    tot = 10 ** (F['lvl'] / 10) * 2  # (approx. harmonic total ~ the sustain power x 2: rfft scaling)
    H = nf.harmonics(S, sr, nfft, F['f0'])
    return 10 * np.log10(H / (np.nansum(F['_H']) + 1e-30) + 1e-12), nf.noise_ratio(S, sr, nfft, F['f0'], F['_tot'])


def main():
    root, outp = sys.argv[1], sys.argv[2]
    amb, amb_sr = read_aiff(os.path.join(root, 'bb_clarinet', 'BbClar.ambient.silence.aiff'))
    res = {}
    for inst in ('bb_clarinet', 'bass_clarinet'):
        d = os.path.join(root, inst)
        notes = []
        for fn in sorted(os.listdir(d)):
            pr = parse(fn)
            if not pr:
                continue
            dyn, lo, hi = pr
            x, sr = read_aiff(os.path.join(d, fn))
            segs = nf.segments(x, sr)
            want = hi - lo + 1
            msg = f'{inst} {fn}: {len(segs)} segments for {want} notes'
            if len(segs) != want:
                # keep the longest `want` segments in time order
                if len(segs) > want:
                    keep = sorted(sorted(range(len(segs)), key=lambda i: segs[i][1] - segs[i][0])[-want:])
                    segs = [segs[i] for i in keep]
                    msg += ' (kept the longest)'
                else:
                    print(msg + ' SKIPPED')
                    continue
            ok = 0
            for k, (t0, t1) in enumerate(segs):
                nt = lo + k
                f0 = 440.0 * 2 ** ((nt - 69) / 12)
                F = nf.note_features(x, sr, f0, t0, t1)
                if abs(F['cents']) > 80:
                    continue
                F['hfloor'], F['nfloor'] = floor_feats(amb, sr, F, F['nfft'])
                F.update(inst=inst, dyn=dyn, note=nt, dur=t1 - t0, src=fn, sr=sr)
                notes.append(F)
                ok += 1
            print(msg + f', {ok} kept')
        res[inst] = notes
    with open(outp, 'wb') as f:
        pickle.dump(res, f)


if __name__ == '__main__':
    main()
