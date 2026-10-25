# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Segment the Iowa MIS chromatic-scale AIFFs into notes, check each note's pitch, and save
per-note mono float32 at 48 kHz (resampled) plus an index (ref/index.json).

  python -I ref_segment.py <samples dir> <instrument tag> <out dir>

File names: <Instr>.[nonvib.]<dyn>.<lo><hi>.aiff, a chromatic scale from lo to hi (one file may be a
single note, e.g. C7). Notes are found as runs above the file's floor + 20 dB (at least 0.25 s),
then matched in order to the expected pitches; a note whose measured f0 is more than 60 cents off
is dropped (reported).
"""
import aifc
import json
import os
import re
import sys

import numpy as np
from scipy.signal import resample_poly

NAMES = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}


def midi(s):
    m = re.fullmatch(r'([A-G]b?)(\d)', s)
    return NAMES[m.group(1)] + 12 * (int(m.group(2)) + 1)


def read_aiff(p):
    with aifc.open(p, 'rb') as f:
        n, ch, sw, sr = f.getnframes(), f.getnchannels(), f.getsampwidth(), f.getframerate()
        raw = f.readframes(n)
    assert sw == 2
    x = np.frombuffer(raw, '>i2').astype(np.float32) / 32768.0
    x = x.reshape(-1, ch).mean(1)
    return x, sr


def f0_acf(x, sr, f_exp):
    n = min(len(x), 8192)
    seg = x[len(x) // 2 - n // 2: len(x) // 2 + n // 2] * np.hanning(n)
    r = np.fft.irfft(np.abs(np.fft.rfft(seg, 2 * n)) ** 2)[:n]
    T = sr / f_exp
    lo, hi = int(T / 1.12), int(T * 1.12) + 2
    k = lo + int(np.argmax(r[lo:hi]))
    a, b, c = r[k - 1], r[k], r[k + 1]
    d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) < 0 else 0
    return sr / (k + d)


def main():
    src, tag, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    idx = []
    for fn in sorted(os.listdir(src)):
        if not fn.endswith('.aiff'):
            continue
        parts = fn[:-5].split('.')
        dyn = parts[-2]
        rng = parts[-1]
        vib = 'vib' in parts and 'nonvib' not in parts
        m = re.findall(r'[A-G]b?\d', rng)
        lo = midi(m[0])
        hi = midi(m[1]) if len(m) > 1 else lo
        notes = list(range(lo, hi + 1))
        x, sr = read_aiff(os.path.join(src, fn))
        hop = int(0.01 * sr)
        nfr = len(x) // hop
        e = np.array([np.mean(x[i * hop:(i + 1) * hop] ** 2) for i in range(nfr)])
        L = 10 * np.log10(e + 1e-12)
        floor = np.percentile(L, 5)
        on = L > floor + 20
        # close short gaps (< 60 ms), find runs >= 0.25 s
        runs = []
        i = 0
        while i < nfr:
            if on[i]:
                j = i
                while j < nfr and (on[j] or (j + 6 < nfr and on[j:j + 6].any())):
                    j += 1
                if j - i >= 25:
                    runs.append((i, j))
                i = j
            else:
                i += 1
        msg = f"{fn}: {len(runs)} runs for {len(notes)} notes"
        if len(runs) == len(notes) + 1:  # one stray run (a breath, a click): drop the shortest
            k = int(np.argmin([b - a for a, b in runs]))
            msg += f" (dropped a {runs[k][1] - runs[k][0]}0 ms run)"
            runs.pop(k)
        if len(runs) != len(notes):
            print(msg, '-- MISMATCH, skipped')
            continue
        print(msg)
        for (a, b), nt in zip(runs, notes):
            a0 = max(a - 5, 0) * hop  # 50 ms before the onset
            b0 = min(b + 30, nfr) * hop
            seg = x[a0:b0]
            fexp = 440 * 2 ** ((nt - 69) / 12)
            mid = x[a * hop + (b - a) * hop // 4: a * hop + 3 * (b - a) * hop // 4]
            f0 = f0_acf(mid, sr, fexp)
            cents = 1200 * np.log2(f0 / fexp)
            if abs(cents) > 60:
                print(f"  note {nt}: f0 {f0:.1f} Hz is {cents:+.0f} c off, dropped")
                continue
            y = resample_poly(seg, 160, 147).astype(np.float32)  # 44.1 -> 48 kHz
            name = f"{tag}_{'vib_' if vib else ''}{dyn}_{nt}"
            y.tofile(os.path.join(out, name + '.f32'))
            idx.append(dict(name=name, inst=tag, dyn=dyn, vib=vib, note=nt, cents=float(cents), dur=float((b - a) * 0.01),
                            onset=float((a * hop - a0) / sr), off=float((b * hop - a0) / sr), file=fn))
    ip = os.path.join(out, 'index.json')
    old = []
    if os.path.exists(ip):
        old = [r for r in json.load(open(ip)) if r['inst'] != tag]
    json.dump(old + idx, open(ip, 'w'), indent=0)
    print(len(idx), 'notes')


if __name__ == '__main__':
    main()
