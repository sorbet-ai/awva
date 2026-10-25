# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""VSCO-2 Community Edition (CC0) piccolo samples -> reference notes (ref/<name>.f32, ref/index.json).
One note per file (piccolo_<written><oct>_<sustain|staccato>N.wav, 24-bit stereo 44.1 kHz); the
piccolo sounds an octave above the written note: the sounding pitch is checked (ACF) against both.
  python -I vsco_segment.py <samples dir> <out dir>"""
import json, os, re, sys, wave
import numpy as np
from scipy.signal import resample_poly
NAMES = {'C': 0, 'C#': 1, 'D': 2, 'D#': 3, 'E': 4, 'F': 5, 'F#': 6, 'G': 7, 'G#': 8, 'A': 9, 'A#': 10, 'B': 11}


def read24(p):
    with wave.open(p, 'rb') as w:
        ch, sw, sr, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    assert sw == 3
    b = np.frombuffer(raw, np.uint8).reshape(-1, 3).astype(np.int32)
    v = (b[:, 0] | (b[:, 1] << 8) | (b[:, 2] << 16))
    v = np.where(v >= 1 << 23, v - (1 << 24), v).astype(np.float32) / (1 << 23)
    return v.reshape(-1, ch).mean(1), sr


def acf_cents(x, sr, f):
    n = min(len(x), 8192) // 2 * 2; c = len(x) // 2; s = x[c - n // 2:c + n // 2] * np.hanning(n)
    r = np.fft.irfft(np.abs(np.fft.rfft(s, 2 * n)) ** 2)[:n]
    T = sr / f; lo, hi = int(T / 1.06), int(T * 1.06) + 2
    k = lo + int(np.argmax(r[lo:hi])); return r[k] / r[0], 1200 * np.log2(T / k)


def main():
    src, out = sys.argv[1:3]
    os.makedirs(out, exist_ok=True)
    ip = os.path.join(out, 'index.json')
    idx = [r for r in json.load(open(ip)) if r['inst'] != 'piccolo'] if os.path.exists(ip) else []
    for fn in sorted(os.listdir(src)):
        m = re.fullmatch(r'piccolo_([A-G]#?)(\d)_(sustain|staccato)\d\.wav', fn)
        if not m: continue
        written = NAMES[m.group(1)] + 12 * (int(m.group(2)) + 1)
        x, sr = read24(os.path.join(src, fn))
        hop = sr // 100
        e = np.array([np.mean(x[i:i + hop] ** 2) for i in range(0, len(x) - hop, hop)])
        L = 10 * np.log10(e + 1e-14)
        on = np.where(L > L.max() - 35)[0]
        a, b = on[0], on[-1] + 1
        mid = x[(a + (b - a) // 4) * hop:(a + 3 * (b - a) // 4) * hop]
        cand = [(acf_cents(mid, sr, 440 * 2 ** ((nt - 69) / 12)), nt) for nt in (written, written + 12)]
        (per, cents), note = max(cand, key=lambda c: c[0][0] - (0.02 if c[1] == written else 0.0))
        kind = 'mf' if m.group(3) == 'sustain' else 'st'
        a0 = max(a - 5, 0) * hop; b0 = min(b + 30, len(L)) * hop
        y = resample_poly(x[a0:b0], 160, 147).astype(np.float32)
        name = f"piccolo_{kind}_{note}"
        y.tofile(os.path.join(out, name + '.f32'))
        idx.append(dict(name=name, inst='piccolo', dyn=kind, vib=False, note=note, cents=float(cents), dur=float((b - a) * 0.01),
                        onset=float((a * hop - a0) / sr), off=float((b * hop - a0) / sr), file=fn))
        print(f"{fn}: written {written}, sounding {note} ({cents:+.0f} c, ACF {per:.2f}), {(b - a) * 0.01:.2f} s")
    json.dump(idx, open(ip, 'w'), indent=0)


if __name__ == '__main__':
    main()
