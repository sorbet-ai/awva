# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Frame features of a render (mono float32, 48 kHz) on the controls the engine got (.ctl voices,
[blocks, 10, 5]): for comparing two engines played identically (the brass family against the brass
engine), frame by frame.

Frames every 10 ms (HOP), two resolutions: short (512 samples, attacks) and long (2048, sustain).
  lvl     frame level, dB (long window RMS)
  mel_s   log-mel spectrum, short window (40 bands mel_lo .. 14 kHz), dB
  mel_l   the same, long window
  bright  high-band balance: energy above 1.5 kHz re the total, dB (long window)
  cent    spectral centroid (Hz, long window)
  f0c     pitch in cents re the note held (monophonic frames), normalised-ACF peak search
  per     periodicity: normalised ACF peak (0..1) at the pitch
  sub     ACF at twice the period minus at the period (period doubling > 0)
  mono    the frame has exactly one held voice and no release in the last 0.25 s
  note    the held note there
  events  the note events of the controls (controls.note_events), with f_on, f_off in frames

  A = FrameAnalysis(mel_lo=80.0); F = A.analyse(x, vc); A.tilt_bins(F, H)
"""
import numpy as np
import scipy.fft as sfft

from ..controls import note_events

SR = 48000
BLOCK = 32
HOP = 480
NS, NL = 512, 2048
NMEL = 40
MEL_HI = 14000.0


def mel_edges(lo, hi=MEL_HI, n=NMEL):
    m = lambda f: 2595 * np.log10(1 + f / 700)  # noqa: E731
    im = lambda m_: 700 * (10 ** (m_ / 2595) - 1)  # noqa: E731
    return im(np.linspace(m(lo), m(hi), n + 2))


def mel_fb(nfft, lo, n=NMEL, hi=MEL_HI):
    """triangular mel filters (rows sum to 1) on an nfft-point rfft at SR"""
    edges = mel_edges(lo, hi, n)
    f = np.arange(nfft // 2 + 1) * SR / nfft
    fb = np.zeros((n, len(f)), np.float32)
    for i in range(n):
        a, c, b = edges[i], edges[i + 1], edges[i + 2]
        fb[i] = np.clip(np.minimum((f - a) / (c - a), (b - f) / (b - c)), 0, None)
    fb /= np.maximum(fb.sum(1, keepdims=True), 1e-9)
    return fb


def frames(x, n, nfr):
    pad = np.concatenate([np.zeros(n // 2, np.float32), x, np.zeros(n, np.float32)])
    idx = np.arange(nfr)[:, None] * HOP + np.arange(n)[None, :]
    return pad[idx]


class FrameAnalysis:
    def __init__(self, mel_lo=80.0):
        self.mel_lo = float(mel_lo)
        self.fb_s, self.fb_l = mel_fb(NS, self.mel_lo), mel_fb(NL, self.mel_lo)
        self.freq_l = np.arange(NL // 2 + 1) * SR / NL
        self.win_s, self.win_l = np.hanning(NS).astype(np.float32), np.hanning(NL).astype(np.float32)
        self.fc = mel_edges(self.mel_lo)[1:-1]

    def analyse(self, x, vc):
        x = np.asarray(x, np.float32)
        nfr = len(x) // HOP
        out = {}
        WIN_L, WIN_S, FREQ_L = self.win_l, self.win_s, self.freq_l
        XL = frames(x, NL, nfr) * WIN_L
        FL = np.abs(sfft.rfft(XL, axis=1)) ** 2
        FS = np.abs(sfft.rfft(frames(x, NS, nfr) * WIN_S, axis=1)) ** 2
        pl = FL.sum(1) / (WIN_L ** 2).sum() * 2 / NL
        out['lvl'] = 10 * np.log10(pl + 1e-10)
        out['mel_l'] = 10 * np.log10(FL @ self.fb_l.T / (WIN_L ** 2).sum() + 1e-10)
        out['mel_s'] = 10 * np.log10(FS @ self.fb_s.T / (WIN_S ** 2).sum() + 1e-10)
        hi = FL[:, FREQ_L > 1500].sum(1)
        out['bright'] = 10 * np.log10((hi + 1e-12) / (FL.sum(1) + 1e-11))
        out['cent'] = (FL * FREQ_L).sum(1) / (FL.sum(1) + 1e-12)
        # controls per frame (the block at the frame centre)
        bidx = np.minimum((np.arange(nfr) * HOP) // BLOCK, len(vc) - 1)
        held = vc[:, :, 1] > 0
        nheld = held.sum(1)
        rel = np.zeros(len(vc), bool)  # a release in the last 0.25 s
        offs = np.where((held[:-1] & ~held[1:]).any(1))[0] + 1
        nb = int(0.25 * SR / BLOCK)
        for o in offs:
            rel[o:o + nb] = True
        mono_b = (nheld == 1) & ~rel
        mono = mono_b[bidx]
        vh = np.argmax(held, axis=1)
        note_b = vc[np.arange(len(vc)), vh, 0]
        out['mono'] = mono
        out['note'] = np.where(mono, note_b[bidx], 0.0)
        out['held'] = nheld[bidx] > 0
        # pitch and periodicity on monophonic frames: normalised ACF around the expected period
        f0c = np.full(nfr, np.nan)
        per = np.zeros(nfr)
        sub = np.zeros(nfr)
        idx = np.where(mono)[0]
        if len(idx):
            X = XL[idx]
            P = np.abs(sfft.rfft(X, 2 * NL, axis=1)) ** 2
            R = sfft.irfft(P, axis=1)[:, :NL]
            e0 = R[:, :1] + 1e-12
            # window-corrected normalisation (the Hann window's own ACF)
            wr = np.fft.irfft(np.abs(np.fft.rfft(WIN_L, 2 * NL)) ** 2)[:NL]
            Rn = R / e0 / (wr / wr[0] + 1e-9)[None, :]
            T = SR / (440.0 * 2 ** ((out['note'][idx] - 69) / 12))
            for j, (r, t) in enumerate(zip(Rn, T)):
                lo, hi_ = int(t / 1.07), int(t * 1.07) + 2
                if hi_ >= NL // 2 - 2 or lo < 8:
                    continue
                k = lo + int(np.argmax(r[lo:hi_]))
                a, b_, c = r[k - 1], r[k], r[k + 1]
                den = a - 2 * b_ + c
                d = min(max(0.5 * (a - c) / den, -1.0), 1.0) if den < 0 else 0.0
                tau = k + d
                f0c[idx[j]] = 1200 * np.log2(t / tau)
                per[idx[j]] = min(max(b_, 0.0), 1.0)
                k2 = int(round(2 * tau))
                if 2 <= k2 and k2 + 2 < NL // 2:
                    sub[idx[j]] = r[k2 - 2:k2 + 3].max() - b_
        out['f0c'] = f0c
        out['per'] = per
        out['sub'] = sub
        out['events'] = note_events(vc)
        for e in out['events']:
            e['f_on'] = int(e['b_on'] * BLOCK / HOP)
            e['f_off'] = int(e['b_off'] * BLOCK / HOP)
        out['peak'] = float(np.max(np.abs(x))) if len(x) else 0.0
        out['finite'] = bool(np.all(np.isfinite(x)))
        return out

    def tilt_bins(self, F, H, nbins=4):
        """The level-dependent spectral tilt: on H's held, monophonic, sustained frames, binned into
        quantiles of H's level, per bin (the slope of F's long-window mel spectrum re its frame level
        over log2 frequency, dB per octave, bands from 1.5 x the note's f0 to 8 kHz; the band energy
        0.5-1.5 kHz re 1.5-5 kHz, dB). None if there are too few frames."""
        FC = self.fc
        nfr = min(len(F['lvl']), len(H['lvl']))
        seg = np.ones(nfr, bool)
        for e in H['events']:
            seg[e['f_on']:min(e['f_on'] + 25, nfr)] = False
        m = H['mono'][:nfr] & seg & (H['lvl'][:nfr] > H['lvl'].max() - 45)
        idx = np.where(m)[0]
        if len(idx) < 4 * nbins:
            return None
        q = np.quantile(H['lvl'][idx], np.linspace(0, 1, nbins + 1))
        out = []
        for b in range(nbins):
            sel = idx[(H['lvl'][idx] >= q[b]) & (H['lvl'][idx] <= q[b + 1])]
            S = F['mel_l'][sel] - F['lvl'][sel, None]
            f0 = 440 * 2 ** ((H['note'][sel] - 69) / 12)
            ts, lm = [], []
            for s, f in zip(S, f0):
                k = (FC > 1.5 * f) & (FC < 8000)
                if k.sum() < 4:
                    continue
                x = np.log2(FC[k])
                ts.append(np.polyfit(x, s[k], 1)[0])
                lo = (FC > 500) & (FC < 1500)
                hi = (FC > 1500) & (FC < 5000)
                lm.append(10 * np.log10(np.mean(10 ** (s[lo] / 10)) / np.mean(10 ** (s[hi] / 10))))
            out.append((float(np.median(ts)) if ts else np.nan, float(np.median(lm)) if lm else np.nan))
        return out
