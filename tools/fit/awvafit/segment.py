# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Recordings of note sequences (a chromatic scale with silence between the notes, or one note per
file) cut into notes: onset, sustain, release.

  notes(x, sr, method) -> [Note(start, onset, off, end)] in seconds, in time order
      start/end include the method's padding (the context kept around the note), onset/off are the
      sound's own beginning and end as the method finds them
  sustain(note, ...) -> (s0, s1): the steady part between onset and off

The methods (each instrument lane's, kept exactly; a config names one):
  'plateau'   (the sax) 5 ms frames; on above max(p5 + 12 dB, p99.5 - 45 dB); gaps under 0.12 s
              closed; notes of at least 0.9 s; padded 50 ms before, 200 ms + a frame after
  'runs'      (the flute) 10 ms frames; on above p5 + 20 dB; gaps under 60 ms closed; runs of at
              least 0.25 s; one stray run more than expected (a breath, a click) dropped
  'envelope'  (the clarinet) 10 ms RMS; on above -50 dB re p99; gaps under 60 ms merged; at least 0.25 s
  'peak'      (the brass sanity check) 10 ms RMS; on above -18 dB re the file's peak; at least 0.4 s
              (the listening packs: -24 dB, 0.25 s)
  'single'    one note per file: the first to the last 10 ms frame within 35 dB of the peak
"""
from dataclasses import dataclass

import numpy as np

from .features.notes import harmonic_sum, lobes


@dataclass
class Note:
    start: float
    onset: float
    off: float
    end: float

    def samples(self, sr):
        return int(round(self.start * sr)), int(round(self.end * sr))


def plateau(x, sr=48000):
    """the sax lane's segments (sample indices, padded), at 48 kHz"""
    if sr != harmonic_sum.SR:
        raise ValueError('plateau segmentation runs at 48 kHz (decode first)')
    return harmonic_sum.segments(x)


def runs(x, sr, expected=None):
    """the flute lane's runs: (a, b) in 10 ms frames (hop = sr // 100... int(0.01 sr)); with
    `expected` notes, one run too many drops the shortest. Returns (runs, hop, message)"""
    hop = int(0.01 * sr)
    nfr = len(x) // hop
    e = np.array([np.mean(x[i * hop:(i + 1) * hop] ** 2) for i in range(nfr)])
    L = 10 * np.log10(e + 1e-12)
    floor = np.percentile(L, 5)
    on = L > floor + 20
    out = []
    i = 0
    while i < nfr:
        if on[i]:
            j = i
            while j < nfr and (on[j] or (j + 6 < nfr and on[j:j + 6].any())):
                j += 1
            if j - i >= 25:
                out.append((i, j))
            i = j
        else:
            i += 1
    msg = f'{len(out)} runs' + (f' for {expected} notes' if expected is not None else '')
    if expected is not None and len(out) == expected + 1:
        k = int(np.argmin([b - a for a, b in out]))
        msg += f' (dropped a {out[k][1] - out[k][0]}0 ms run)'
        out.pop(k)
    return out, hop, msg


def envelope(x, sr):
    """the clarinet lane's note regions (seconds)"""
    return lobes.segments(x, sr)


def peak(x, sr, min_s=0.4, db=-18.0):
    """the brass sanity check's segments (sample indices): 10 ms RMS above db re the peak (the
    listening packs use -24 dB and 0.25 s)"""
    hop = int(0.01 * sr)
    e = np.sqrt(np.convolve(x * x, np.ones(hop) / hop, 'same')[::hop] + 1e-12)
    on = np.concatenate([e > e.max() * 10 ** (db / 20), [False]])
    segs, a = [], None
    for i, v in enumerate(on):
        if v and a is None:
            a = i
        if not v and a is not None:
            if (i - a) * hop >= min_s * sr:
                segs.append((a * hop, i * hop))
            a = None
    return segs


def single(x, sr):
    """one note per file: (a, b) 10 ms frames within 35 dB of the peak, the hop"""
    hop = sr // 100
    e = np.array([np.mean(x[i:i + hop] ** 2) for i in range(0, len(x) - hop, hop)])
    L = 10 * np.log10(e + 1e-14)
    on = np.where(L > L.max() - 35)[0]
    return int(on[0]), int(on[-1]) + 1, hop, len(L)


def notes(x, sr, method, expected=None):
    """the notes of a recording as Note records (seconds)"""
    if method == 'plateau':
        return [Note(a / sr, a / sr + 0.05, b / sr - 0.2, b / sr) for a, b in plateau(x, sr)]
    if method == 'runs':
        rr, hop, _ = runs(x, sr, expected)
        nfr = len(x) // hop
        return [Note(max(a - 5, 0) * hop / sr, a * hop / sr, b * hop / sr, min(b + 30, nfr) * hop / sr) for a, b in rr]
    if method == 'envelope':
        return [Note(a, a, b, b) for a, b in envelope(x, sr)]
    if method == 'peak':
        return [Note(a / sr, a / sr, b / sr, b / sr) for a, b in peak(x, sr)]
    if method == 'single':
        a, b, hop, n = single(x, sr)
        return [Note(max(a - 5, 0) * hop / sr, a * hop / sr, b * hop / sr, min(b + 30, n) * hop / sr)]
    raise ValueError(f'no segmentation method {method!r}')


def sustain(n, lead=0.3, trail=0.12, min_len=0.25):
    """the steady part of a note: lead s after the onset to trail s before the release (the middle
    half when the note is shorter)"""
    s0, s1 = n.onset + lead, n.off - trail
    if s1 - s0 < min_len:
        s0, s1 = n.onset + 0.5 * (n.off - n.onset), n.off - 0.05
    return s0, s1
