# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Objective jank diagnosis: every note of a range at several pressures and strikes, held 1.5 s.
Per play: the tone's speak time from the key (harmonics 1-3 within 3 dB of their 0.4-1.4 s level),
failed speak, the register, pitch wobble (f0 per 10 ms over 0.4-1.4 s: std and peak-to-peak, cents),
amplitude wobble / limit cycles (5 ms RMS: std dB and the strongest modulation frequency), noise
bursts (2 ms frames 6 dB over the 50 ms median), the inharmonic share above 4 kHz (aliasing), and the
level, for steps across notes and dynamics.
  python -I diag.py <bintag> <preset> <lo> <hi> <step> [--json out] [name=value ...]"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe, feat
SR = 48000
a = sys.argv[1:]
bt, pre, lo, hi, step = a[0], int(a[1]), int(a[2]), int(a[3]), int(a[4])
out = None; params = {}
i = 5
while i < len(a):
    if a[i] == '--json': out = a[i + 1]; i += 2
    else: k, v = a[i].split('='); params[k] = float(v); i += 1
PRES = [0.08, 0.3, 0.6, 1.0]
STRIKES = [0.7, 0.15]
notes = list(range(lo, hi + 1, step))
rows = []
for sk in STRIKES:
    plays = [(n, P) for n in notes for P in PRES]
    x, evs, err = probe.render(bt, pre, plays, params, None, sk, 1.5, tag=f'dg{pre}')
    for (n, P, t0, t1) in evs:
        f0 = 440 * 2 ** ((n - 69) / 12)
        k0 = int(t0 * SR)
        trise, env = feat.tonal_rise(x, n, k0, int(1.4 * SR))
        a0, a1 = k0 + int(0.4 * SR), k0 + int(1.4 * SR)
        seg = x[a0:a1].astype(np.float64)
        lvl = 10 * np.log10(np.mean(seg ** 2) + 1e-20)
        # spectrum: register, harmonic share, inharmonic HF share
        N = 16384; S = np.abs(np.fft.rfft(seg[:N] * np.hanning(N))) ** 2; f = np.arange(len(S)) * SR / N
        hm = np.zeros(len(S), bool)
        for kk in range(1, int(23000 / f0) + 1): hm |= (f > kk * f0 * 0.985) & (f < kk * f0 * 1.015)
        tot = S[f > 50].sum() + 1e-30
        hshare = S[hm].sum() / tot
        hf = f > 4000
        inh_hf = S[hf & ~hm].sum() / (S[hf].sum() + 1e-30)
        ip = int(np.argmax(S * (f > 50))); r = f[ip] / f0; mult = max(1, int(round(r)))
        h1 = S[(f > f0 * 0.985) & (f < f0 * 1.015)].sum()
        reg_ok = r > 0.7 and abs(1200 * np.log2(f[ip] / (mult * f0))) < 25 and h1 > S[ip] * 0.01
        # pitch track: harmonic-weighted peak near f0 in 4096-sample frames every 10 ms
        cents = []
        Nf = 4096; w = np.hanning(Nf); fr = np.fft.rfftfreq(Nf * 4, 1 / SR)
        for s0 in range(a0, a1 - Nf, 480):
            Sp = np.abs(np.fft.rfft(x[s0:s0 + Nf] * w, Nf * 4))
            m = (fr > f0 * 0.94) & (fr < f0 * 1.06)
            if not m.any(): continue
            j = np.argmax(Sp * m)
            if Sp[j] < 1e-9: continue
            al, be, ga = Sp[j - 1], Sp[j], Sp[j + 1]
            d = 0.5 * (al - ga) / (al - 2 * be + ga) if (al - 2 * be + ga) != 0 else 0
            cents.append(1200 * np.log2((fr[j] + d * (fr[1] - fr[0])) / f0))
        cents = np.array(cents)
        # the attack: pitch over 40-200 ms after the key (frames of 1024), the level's overshoot
        ac = []
        T0 = SR / f0
        Na = int(max(1024, 4 * T0)) // 2 * 2; wa = np.hanning(Na); fra = np.fft.rfftfreq(Na * 8, 1 / SR)
        for s0 in range(k0 + 1920 - min(Na, 1920), k0 + 9600 - min(Na, 1920), 240):
            Sp = np.abs(np.fft.rfft(x[s0:s0 + Na] * wa, Na * 8)); m = (fra > f0 * 0.85) & (fra < f0 * 1.15)
            if Sp[m].max() < 1e-7: continue
            j = np.argmax(Sp * m); ac.append(1200 * np.log2(fra[j] / f0))
        att_pp = float(np.ptp(ac)) if len(ac) > 3 else 0.0
        att_dev = float(np.max(np.abs(ac))) if len(ac) > 3 else 0.0
        e10 = 10 * np.log10(np.array([np.mean(x[i:i + 480].astype(np.float64) ** 2) for i in range(k0, k0 + int(0.4 * SR), 480)]) + 1e-20)
        pw_std = float(np.std(cents)) if len(cents) > 5 else 99.0
        pw_pp = float(np.ptp(cents)) if len(cents) > 5 else 99.0
        # amplitude wobble: 5 ms RMS
        wl = int(max(240, 2 * T0))  # 5 ms or two periods
        e5 = 10 * np.log10(np.array([np.mean(seg[i:i + wl] ** 2) for i in range(0, len(seg) - wl, 120)]) + 1e-20)
        aw_std = float(np.std(e5))
        M = np.abs(np.fft.rfft(e5 - e5.mean())); mf = np.fft.rfftfreq(len(e5), 120 / SR)
        am_hz = float(mf[1 + np.argmax(M[1:])])
        # noise bursts: 2 ms frames vs the 50 ms median, whole note from 50 ms after the key
        wb = int(max(96, T0))  # 2 ms or one period, against the median over ~10 of them
        e2 = 10 * np.log10(np.array([np.mean(x[i:i + wb].astype(np.float64) ** 2) for i in range(k0 + 2400, a1, wb)]) + 1e-20)
        med = np.array([np.median(e2[max(0, i - 6):i + 7]) for i in range(len(e2))])
        bursts = int(np.sum(e2 > med + 6))
        rows.append(dict(note=n, pres=P, strike=sk, trise=float(trise), lvl=float(lvl), hshare=float(hshare), inh_hf=float(inh_hf),
                         reg_ok=bool(reg_ok), att_pp=att_pp, att_dev=att_dev, over=float(e10.max() - lvl), pw_std=pw_std, pw_pp=pw_pp, aw_std=aw_std, am_hz=am_hz, bursts=bursts))
R = rows
def med(k, f=lambda r: True): v = [r[k] for r in R if f(r)]; return float(np.nanmedian(v)) if v else float('nan')
print(f"bin {bt} preset {pre} notes {lo}-{hi}: {len(R)} plays")
print(f"  speak (tone within 3 dB, from the key): median {1000*med('trise', lambda r: r['strike']>0.5):.0f} ms struck, {1000*med('trise', lambda r: r['strike']<0.5):.0f} ms unstruck; >200 ms: {sum(r['trise']>0.2 for r in R)}")
print(f"  attack pitch (40-200 ms): max deviation median {med('att_dev'):.0f} c, p90 {np.percentile([r['att_dev'] for r in R],90):.0f} c; level overshoot over the sustain: median {med('over'):.1f} dB, p90 {np.percentile([r['over'] for r in R],90):.1f} dB")
print(f"  failed (harmonic share < 0.5): {sum(r['hshare']<0.5 for r in R)}; wrong register: {sum(not r['reg_ok'] for r in R)}")
print(f"  pitch wobble std: median {med('pw_std'):.1f} c, p90 {np.nanpercentile([r['pw_std'] for r in R],90):.1f} c; peak-to-peak median {med('pw_pp'):.1f} c")
print(f"  amplitude wobble std: median {med('aw_std'):.2f} dB, p90 {np.percentile([r['aw_std'] for r in R],90):.2f} dB; plays with > 1 dB: {sum(r['aw_std']>1 for r in R)}")
print(f"  noise bursts (2 ms / 1-period frames > their median + 6 dB): total {sum(r['bursts'] for r in R)}, plays with any: {sum(r['bursts']>0 for r in R)}")
print(f"  inharmonic share above 4 kHz: median {med('inh_hf'):.3f}, p90 {np.percentile([r['inh_hf'] for r in R],90):.3f}")
L = {(r['note'], r['pres']): r['lvl'] for r in R if r['strike'] > 0.5}
steps = [abs(L[(n2, P)] - L[(n1, P)]) for P in PRES for n1, n2 in zip(notes[:-1], notes[1:])]
dyn = [L[(n, 1.0)] - L[(n, 0.08)] for n in notes]
print(f"  level steps between adjacent notes (step {step}): median {np.median(steps):.1f} dB, max {np.max(steps):.1f} dB, > 4 dB: {sum(s>4 for s in steps)}/{len(steps)}")
print(f"  dynamic range 0.08 -> 1.0: median {np.median(dyn):.1f} dB (min {np.min(dyn):.1f}, max {np.max(dyn):.1f}); 0.6 -> 1.0 median {np.median([L[(n,1.0)]-L[(n,0.6)] for n in notes]):.1f} dB")
if out: json.dump(R, open(out, 'w'))
