# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The tone's rise on the recorded takes (engine-level controls captured from the key recordings, as an embedded host plays them): per note
from silence (>= 100 ms) held >= 250 ms, from the key's onset until harmonics 1-3 are within 3 dB of
their level 150-400 ms in."""
import os, subprocess, numpy as np
import feat
from work import ROOT, replay  # noqa: E402
SR, BLK = 48000, 32
TAKES = ['play_2026-10-22_a.ctl', 'play_2026-10-22_legato.ctl', 'play_2026-10-22_moderate.ctl', 'play_2026-10-22_staccato.ctl', 'syn_strike.ctl']


def rises(bintag, preset, params, tag):
    rows = []
    d = os.path.join(ROOT, 'tmp', tag); os.makedirs(d, exist_ok=True)
    cmd = [replay(bintag), '--engine', 'flute', '--preset', str(preset)]
    for k, v in params.items(): cmd += ['--set', f'{k}={v}']
    jobs = []
    for fn in TAKES:
        cp = os.path.join(ROOT, 'takes', fn); op = os.path.join(d, fn + '.f32'); jobs += ['--job', cp, op]
    subprocess.run(cmd + jobs, check=True, capture_output=True)
    for fn in TAKES:
        x = np.fromfile(os.path.join(d, fn + '.f32'), np.float32)
        c = np.fromfile(os.path.join(ROOT, 'takes', fn), np.float32).reshape(-1, 58)[:, :50].reshape(-1, 10, 5)
        held = c[:, :, 1] > 0
        anyh = held.any(1)
        for b in range(1, len(anyh)):
            if anyh[b] and not anyh[max(b - 150, 0):b].any():
                v = int(np.argmax(held[b]))
                dur = int(np.argmax(~held[b:, v])) if (~held[b:, v]).any() else 300
                if dur < 375: continue
                strike = float(c[b:b + 60, v, 2].max())
                note = float(np.median(c[b + 10:b + 60, v, 0]))
                t, _ = feat.tonal_rise(x, note, b * BLK, int(min(dur * BLK, 0.4 * SR)))
                rows.append((strike, t))
    return np.array(rows)


def summary(r):
    hard = r[r[:, 0] >= 0.4, 1]; soft = r[r[:, 0] < 0.4, 1]
    return float(np.median(hard)) if len(hard) else 0.0, float(np.median(soft)) if len(soft) else 0.0, len(hard), len(soft)
