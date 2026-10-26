# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The recordings' liveliness: per note the sustained pitch's wobble (std, cents, f0 per 10 ms over
the middle of the note), the level's wobble (two-period RMS, std dB), the attack's pitch deviation
(40-200 ms after the first sound) and the dynamic range pp -> ff per note."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from work import ROOT, replay  # noqa: E402
SR = 48000
inst = sys.argv[1]
idx = [r for r in json.load(open(os.path.join(ROOT, 'ref', 'index.json'))) if r['inst'] == inst and not r['vib']]
pw, aw, ad, lv = [], [], [], {}
for r in idx:
    x = np.fromfile(os.path.join(ROOT, 'ref', r['name'] + '.f32'), np.float32).astype(np.float64)
    f0 = 440 * 2 ** ((r['note'] - 69) / 12); T0 = SR / f0
    a, b = int(r['onset'] * SR), int(r['off'] * SR)
    m0, m1 = a + int(0.3 * (b - a)), a + int(0.8 * (b - a))
    Nf = 4096; w = np.hanning(Nf); fr = np.fft.rfftfreq(Nf * 4, 1 / SR); cs = []
    for s0 in range(m0, m1 - Nf, 480):
        Sp = np.abs(np.fft.rfft(x[s0:s0 + Nf] * w, Nf * 4)); mm = (fr > f0 * 0.94) & (fr < f0 * 1.06)
        j = np.argmax(Sp * mm); al, be, ga = Sp[j - 1], Sp[j], Sp[j + 1]
        d = 0.5 * (al - ga) / (al - 2 * be + ga) if (al - 2 * be + ga) != 0 else 0
        cs.append(1200 * np.log2((fr[j] + d * (fr[1] - fr[0])) / f0))
    if len(cs) > 5: pw.append(np.std(cs))
    wl = int(max(240, 2 * T0)); seg = x[m0:m1]
    e = 10 * np.log10(np.array([np.mean(seg[i:i + wl] ** 2) for i in range(0, len(seg) - wl, 120)]) + 1e-20)
    aw.append(np.std(e))
    lv[(r['note'], r['dyn'])] = 10 * np.log10(np.mean(seg ** 2) + 1e-20)
    Na = int(max(1024, 4 * T0)) // 2 * 2; wa = np.hanning(Na); fra = np.fft.rfftfreq(Na * 8, 1 / SR); ac = []
    for s0 in range(a + 1920 - min(Na, 1920), a + 9600 - min(Na, 1920), 240):
        Sp = np.abs(np.fft.rfft(x[s0:s0 + Na] * wa, Na * 8)); mm = (fra > f0 * 0.85) & (fra < f0 * 1.15)
        j = np.argmax(Sp * mm); ac.append(1200 * np.log2(fra[j] / f0))
    ad.append(np.max(np.abs(np.array(ac) - np.median(cs))) if len(ac) > 3 and len(cs) else 0)
dr = [lv[(n, 'ff')] - lv[(n, 'pp')] for (n, d) in lv if d == 'pp' and (n, 'ff') in lv]
drm = [lv[(n, 'ff')] - lv[(n, 'mf')] for (n, d) in lv if d == 'mf' and (n, 'ff') in lv]
print(f"{inst}: {len(idx)} notes; sustained pitch wobble std median {np.median(pw):.1f} c (p10 {np.percentile(pw,10):.1f}, p90 {np.percentile(pw,90):.1f}); level wobble std median {np.median(aw):.2f} dB (p90 {np.percentile(aw,90):.2f}); attack pitch deviation re the sustain median {np.median(ad):.0f} c p90 {np.percentile(ad,90):.0f} c; dynamic range pp->ff median {np.median(dr):.1f} dB (n {len(dr)}), mf->ff {np.median(drm):.1f} dB")
