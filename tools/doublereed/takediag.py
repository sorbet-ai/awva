# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The recorded Striso takes (engine controls): per note from its key onset, in frames where it is
the only held voice: the pitch's deviation from the note in the first 150 ms (the scoop) and after
(cents), off-register frames, and the level's jumps (> 4 dB per 10 ms while held)."""
import os
import sys, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from work import ROOT, replay  # noqa: E402
SR, BLK = 48000, 32
bt, pre = sys.argv[1], sys.argv[2]
eng = os.environ.get('REPLAY_ENGINE', 'doublereed')
TAKES = ['play_2026-10-22_a.ctl', 'play_2026-10-22_legato.ctl', 'play_2026-10-22_moderate.ctl', 'play_2026-10-22_staccato.ctl']
scoop, late, offreg, ljump, nfr, notes = [], [], 0, 0, 0, 0
for fn in TAKES:
    cp = os.path.join(ROOT, 'takes', fn); op = os.path.join(ROOT, 'tmp', 'td_' + fn + '.f32')
    subprocess.run([replay(bt), '--engine', eng, '--preset', pre, '--job', cp, op], check=True, capture_output=True)
    x = np.fromfile(op, np.float32)
    c = np.fromfile(cp, np.float32).reshape(-1, 58)[:, :50].reshape(-1, 10, 5)
    held = c[:, :, 1] > 0; nh = held.sum(1)
    for v in range(10):
        h = held[:, v].astype(int); d = np.diff(np.concatenate([[0], h, [0]]))
        for a, b in zip(np.where(d == 1)[0], np.where(d == -1)[0]):
            if b - a < 120: continue
            note = float(np.median(c[a:b, v, 0])); f0 = 440 * 2 ** ((note - 69) / 12); T0 = SR / f0
            N = int(max(1024, 4 * T0)) // 2 * 2; w = np.hanning(N); fr = np.fft.rfftfreq(N * 8, 1 / SR)
            notes += 1; prevl = None
            for s0 in range(a * BLK, b * BLK - N, 480):
                bb = (s0 + N // 2) // BLK
                if bb >= len(nh) or nh[bb] != 1: prevl = None; continue
                S = np.abs(np.fft.rfft(x[s0:s0 + N] * w, N * 8)) ** 2
                m = (fr > f0 * 0.9) & (fr < f0 * 1.1); j = np.argmax(S * m)
                cents = 1200 * np.log2(fr[j] / f0)
                ip = np.argmax(S * (fr > 40)); r = fr[ip] / f0
                ok = r > 0.7 and abs(r - round(r)) < 0.03 * round(r) and S[j] > S[ip] * 0.01
                lv = 10 * np.log10(S[(fr > f0 * 0.97) & (fr < f0 * 1.03)].sum() + 1e-20)
                t = (s0 + N // 2) / SR - a * BLK / SR
                nfr += 1
                if not ok: offreg += 1; continue
                if prevl is not None and abs(lv - prevl) > 4: ljump += 1
                prevl = lv
                (scoop if t < 0.15 else late).append(abs(cents))
print(f"{bt} preset {pre}: {notes} notes, {nfr} solo frames: attack (0-150 ms) |pitch error| median {np.median(scoop):.0f} c p90 {np.percentile(scoop,90):.0f} c; later median {np.median(late):.1f} c p90 {np.percentile(late,90):.1f} c, > 20 c: {100*np.mean(np.array(late)>20):.1f} %; off-register frames {offreg} ({100*offreg/max(nfr,1):.1f} %); level jumps > 4 dB/10 ms {ljump}")
