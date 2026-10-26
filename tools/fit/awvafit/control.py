# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Calibrated phrase streams: a recording's phrase played on one engine with that engine's own breath
pressure, legato and strikes, so that the render follows the recording's level, slurs and attacks.

The phrase sets' default streams infer the control once for every engine (P = 0.4 * 10^((L - Lmed)/40),
a strike from the acoustic rise, connected notes only where the level dips < 6 dB): an engine whose
level grows 0.3-0.9 dB per dB of pressure plays the recording's dynamics compressed and may sit near
its oscillation threshold (breathy, weak pitch); slurs come out as a key let go and another struck 30-40
ms later (a near-silent gap); most tongued notes strike at 0.7-1.0. Here the stream is built per engine:

  sweep: isolated notes at the phrase's pitches over PRESSURES (strike 0.2): each one's sustain level
     (dB), brightness (spectral centroid over f0), periodicity (the autocorrelation peak at the period,
     median over 40 ms frames); the threshold = the lowest pressure from which the periodicity stays
     >= 0.9; the floor = 1.25 x the threshold, and at least where the engine's level comes within 30 dB of
     its loudest (every note sits clearly above both)
  anchor: the pressure whose brightness matches the recording's (the median note's centroid / f0, at
     its pitch), at least the floor
  pressure: each moment of a note maps the recording's 10 ms level (re the phrase's median note) to the
     pressure at which the engine's level is the anchor's plus that difference (its own level curve,
     inverted; clipped to the floor and 1): per-note dynamics and the motion within a note follow the
     recording's level through the engine's curve. Within a note the level followed goes at most 12 dB
     under the note's own, the level is smoothed over 70 ms, and the pressure moves at most 1.5 /s (the
     scanner's velocity <= 0.05): a breath's motion; a velocity that rises inside a held note is
     played as a second strike (clicks in the clarinets at 0.05-0.08)
  legato: a connected change (the next onset < CONNECT after this note's end, another pitch) keeps the
     breath: the next key goes down at its onset with a slurred strike (SLUR_STRIKE) on another key, the
     last one lets go `overlap` s later; repeated pitches and gaps re-tongue (released 20 ms before)
  strikes: every note's strike from strike.calibrate (the recording's own attack overshoot), the slurred
     ones too: a tongued legato re-attacks on the held breath as far as the recording does; a slur whose
     recording shows no attack keeps a soft press (the choice nearest SLUR_STRIKE within 0.5 dB)

  measure(x, onsets...)   per note: level (dB), periodicity, brightness; per connected change: the dip
                          (the two notes' sustain mean over the lowest 10 ms level between them)
  build(item, curves, path, overlap)   the stream (CSV + .notes.json, the keys format)
  calibrated(item, render, workdir)    the whole: sweep, stream, strikes; returns (path, report)
"""
import copy
import json
import math
import os

import numpy as np

from . import keys, strike
from .strike import rise_time  # noqa: F401  (re-exported)

SR = 48000
PRESSURES = tuple(float(p) for p in np.round(np.geomspace(0.04, 1.0, 12), 3))
CONNECT = 0.12
SLUR_STRIKE = 0.1
TONGUE_STRIKE = 0.2
PERIODIC = 0.9
FLOOR_X = 1.25
FLOOR_DB = 30.0   # the floor is also where the engine's level is within this of its loudest
HOLD_DIP = 12.0   # dB a held note's pressure may follow the recording's level under the note's own
SLEW = 1.5        # /s: the fastest a held note's pressure moves (the scanner's velocity <= K * SLEW = 0.05)
ENV_SMOOTH = 0.07  # s: the recording's 10 ms level, smoothed over this before the pressure follows it


def env_db(x, sr=SR, hop=0.01):
    h = int(hop * sr)
    n = len(x) // h
    return 10 * np.log10(np.mean(x[:n * h].reshape(n, h) ** 2, 1) + 1e-20)


def periodicity(seg, f0, sr=SR, frame=0.04):
    """the median over 40 ms frames of the normalised autocorrelation's peak within 3 % of the period"""
    T = sr / f0
    n = int(frame * sr)
    lo, hi = max(int(T * 0.97), 1), int(math.ceil(T * 1.03))
    if len(seg) < n + hi + 1:
        return math.nan
    out = []
    for a in range(0, len(seg) - n - hi, n // 2):
        u = seg[a:a + n]
        eu = np.dot(u, u)
        best = 0.0
        for L in range(lo, hi + 1):
            v = seg[a + L:a + L + n]
            d = math.sqrt(eu * np.dot(v, v)) + 1e-20
            best = max(best, float(np.dot(u, v)) / d)
        out.append(best)
    return float(np.median(out)) if out else math.nan


def brightness(seg, f0, sr=SR, nfft=4096):
    if len(seg) < nfft:
        return math.nan
    w = np.hanning(nfft)
    S = np.zeros(nfft // 2 + 1)
    for a in range(0, len(seg) - nfft + 1, nfft // 2):
        S += np.abs(np.fft.rfft(seg[a:a + nfft] * w)) ** 2
    f = np.fft.rfftfreq(nfft, 1 / sr)
    m = (f > 0.5 * f0) & (f < 16000)
    return float(np.sum(f[m] * S[m]) / (np.sum(S[m]) + 1e-20) / f0)


def _sus(on, off):
    s0, s1 = on + 0.25, off - 0.05
    if s1 - s0 < 0.12:
        s0, s1 = on + (off - on) * 0.4, off - (off - on) * 0.15
    return s0, s1


def note_measures(x, on, off, midi, sr=SR):
    s0, s1 = _sus(on, off)
    seg = x[int(s0 * sr):int(s1 * sr)]
    if len(seg) < 0.06 * sr:
        return dict(level=math.nan, periodicity=math.nan, brightness=math.nan)
    f0 = 440 * 2 ** ((midi - 69) / 12)
    return dict(level=float(np.median(env_db(seg, sr))), periodicity=periodicity(seg, f0, sr), brightness=brightness(seg, f0, sr))


def measure(x, notes, sr=SR):
    """notes: [(onset, offset, midi)] on x's clock -> (per note measures, per connected change dips)"""
    per = [note_measures(x, a, b, m, sr) for a, b, m in notes]
    e = env_db(x, sr)
    dips = []
    for j in range(len(notes) - 1):
        (a0, b0, m0), (a1, b1, m1) = notes[j], notes[j + 1]
        if a1 - b0 >= CONNECT or m1 == m0:
            continue
        lo = e[max(int((b0 - 0.08) / 0.01), 0):int((a1 + 0.08) / 0.01) + 1]
        ref = np.nanmean([per[j]['level'], per[j + 1]['level']])
        if len(lo) and math.isfinite(ref):
            dips.append(dict(at=round(a1, 3), dip_db=round(float(ref - lo.min()), 1)))
    return per, dips


# --- the sweep -------------------------------------------------------------------------------------

def sweep_stream(path, layout_notes, pressures=PRESSURES, hold=1.0, gap=0.6):
    s = keys.Stream()
    t = 0.3
    sched = []
    for n in layout_notes:
        for P in pressures:
            end = s.press(t, TONGUE_STRIKE, P, hold, note=n)
            sched.append(dict(note=n, P=P, t_on=t, t_off=end - 0.02))
            t = end + gap
    s.write(path, notes=False)
    t0 = sched[0]['t_on']
    for d in sched:
        d['t_on'] -= t0
        d['t_off'] -= t0
    return sched


def curves(x, sched, octave):
    """{layout note: dict(P, level, periodicity, brightness, threshold, floor)}"""
    out = {}
    for d in sched:
        m = note_measures(x, d['t_on'], d['t_off'], d['note'] + 12 * octave)
        c = out.setdefault(d['note'], dict(P=[], level=[], periodicity=[], brightness=[]))
        c['P'].append(d['P'])
        for k in ('level', 'periodicity', 'brightness'):
            c[k].append(m[k])
    for n, c in out.items():
        r = np.nan_to_num(np.array(c['periodicity']), nan=0.0)
        ok = r >= PERIODIC
        thr = None
        for i in range(len(r)):
            if ok[i:].all():
                thr = c['P'][i]
                break
        c['threshold'] = thr
        lv = np.nan_to_num(np.array(c['level']), nan=-200.0)
        audible = c['P'][int(np.argmax(lv >= lv.max() - FLOOR_DB))]
        c['floor'] = min(max(FLOOR_X * thr, audible), 1.0) if thr is not None else 1.0
    return out


def _curve_for(cv, layout_note):
    return cv[min(cv, key=lambda n: abs(n - layout_note))]


def level_at(c, P):
    lv = np.maximum.accumulate(np.nan_to_num(np.array(c['level']), nan=-200.0))
    return float(np.interp(math.log(P), np.log(c['P']), lv))


def pressure_for(c, level):
    """the pressure at which the engine's (monotone) level reaches `level`, within [floor, 1]"""
    lv = np.maximum.accumulate(np.nan_to_num(np.array(c['level']), nan=-200.0))
    lp = np.log(c['P'])
    keep = np.concatenate([[True], np.diff(lv) > 1e-6])
    P = float(np.exp(np.interp(level, lv[keep], lp[keep])))
    return min(max(P, c['floor']), 1.0)


def anchor(c, target_brightness):
    """the pressure whose brightness is nearest the target (log-interpolated), at least the floor"""
    grid = np.geomspace(c['P'][0], c['P'][-1], 200)
    b = np.interp(np.log(grid), np.log(c['P']), np.nan_to_num(np.array(c['brightness']), nan=0.0))
    ok = grid >= c['floor']
    return float(grid[ok][np.argmin(np.abs(b[ok] - target_brightness))])


# --- the stream ------------------------------------------------------------------------------------

def _octave(item):
    return int(round(item.transpose / 12))


def rec_notes(item):
    out = []
    for n in item.notes:
        off = n.get('offset_s') or (n['onset_s'] + n.get('dur_s', 0.5))
        out.append((n['onset_s'], off, n['midi'] + (n.get('cents') or 0) / 100.0))
    return out


def build(item, rec, cv, path, overlap=0.03, strikes=None, bright_offset=0.0):
    """the calibrated stream for one engine (its sweep curves cv); rec: the recording (48 kHz mono)"""
    o = _octave(item)
    notes = rec_notes(item)
    meas = [note_measures(rec, a, b, m) for a, b, m in notes]
    lv = np.array([m['level'] for m in meas])
    Lmed = float(np.nanmedian(lv))
    jm = int(np.nanargmin(np.abs(lv - Lmed)))
    e = env_db(rec)
    # the breath follows the level's motion, not its ripple (beats, a room's flutter): ENV_SMOOTH s
    w = max(int(round(ENV_SMOOTH / 0.01)), 1)
    e = np.convolve(np.pad(e, (w // 2, w - 1 - w // 2), mode='edge'), np.ones(w) / w, mode='valid')
    te = np.arange(len(e))
    mids = [int(round(m)) for _, _, m in notes]
    lay = [m - 12 * o for m in mids]
    c_med = _curve_for(cv, lay[jm])
    bright = float(np.nanmedian([m['brightness'] for m in meas]))
    P_anchor = anchor(c_med, bright - bright_offset)
    L_anchor = level_at(c_med, P_anchor)
    s = keys.Stream()
    t_first = notes[0][0]
    last_kid = None
    info = []
    for j, (a, b, _) in enumerate(notes):
        nxt = notes[j + 1] if j + 1 < len(notes) else None
        conn_in = j > 0 and a - notes[j - 1][1] < CONNECT and mids[j] != mids[j - 1]
        conn_out = nxt is not None and nxt[0] - b < CONNECT and mids[j + 1] != mids[j]
        c = _curve_for(cv, lay[j])
        end = nxt[0] + overlap if conn_out else (min(b, nxt[0] - 0.02) if nxt is not None and nxt[0] - b < CONNECT else b)

        lvl_j = lv[j] if math.isfinite(lv[j]) else Lmed
        low = (lvl_j - Lmed) - HOLD_DIP  # a held note's pressure stays within HOLD_DIP dB of its own level

        def target(t, c=c, low=low):
            return pressure_for(c, L_anchor + max(float(np.interp(t / 0.01, te, e)) - Lmed, low))

        P0 = pressure_for(c, L_anchor + (lvl_j - Lmed))
        v = SLUR_STRIKE if conn_in else (strikes[j] if strikes and j in strikes else TONGUE_STRIKE)
        tr = max(keys.K * math.pi * P0 / (2 * v), 0.004)
        hold = max(end - a - tr, 0.05)
        # the hold's pressure follows the recording's level from the rise's end, slew-limited (the
        # scanner's velocity stays under K * SLEW: a breath's motion, never a second strike)
        ug = np.arange(0.0, hold + keys.DT, keys.DT)
        y = np.empty(len(ug))
        y[0] = P0
        for k in range(1, len(ug)):
            y[k] = y[k - 1] + float(np.clip(target(a + tr + ug[k]) - y[k - 1], -SLEW * keys.DT, SLEW * keys.DT))
        avoid = (last_kid,) if conn_in and last_kid is not None else ()
        s.press(a - t_first + 0.3, v, P0, hold, note=lay[j], avoid=avoid, shape=lambda u, ug=ug, y=y, P0=P0: float(np.interp(u, ug, y)) / P0)
        last_kid = s.notes[-1]['kid']
        info.append(dict(P0=round(P0, 3), floor=round(c['floor'], 3), slur_in=conn_in))
    s.write(path)
    return dict(bright_offset=round(bright_offset, 2), anchor_P=round(P_anchor, 3), anchor_level_db=round(L_anchor, 1), recording_brightness=round(bright, 2), notes=info)


def calibrated(item, render, workdir, overlap=0.03, refine=1, log=print):
    """-> (stream path, report). render(csv) -> the engine's dry output (time 0 = the first row)."""
    from .phraseset import reference
    os.makedirs(workdir, exist_ok=True)
    rec, _ = reference(item)
    o = _octave(item)
    lay = sorted({int(round(n['midi'])) - 12 * o for n in item.notes})
    sw_notes = sorted({lay[0], lay[len(lay) // 2], lay[-1]})
    sw = os.path.join(workdir, 'pressure_sweep.csv')
    sched = sweep_stream(sw, sw_notes)
    cv = curves(render(sw), sched, o)
    for n in sw_notes:
        c = cv[n]
        log(f"pressure sweep note {n + 12 * o}: threshold {c['threshold']}, floor {c['floor']:.3f}, level "
            f"{np.nanmin(c['level']):.1f}..{np.nanmax(c['level']):.1f} dB")
    p1 = os.path.join(workdir, 'pressure.csv')
    rep = build(item, rec, cv, p1, overlap)
    # the anchor in the phrase itself: an isolated note's brightness is not the phrase's (legato, the
    # level's motion), so the phrase's median brightness is measured and the anchor moved by the
    # difference (refine passes)
    off = 0.0
    for _ in range(refine):
        got = compare(rec, render(p1), item, p1)
        rb = float(np.nanmedian(got['R']['brightness']))
        xb = float(np.nanmedian(got['render']['brightness']))
        off += xb - rb
        log(f'anchor pass: phrase brightness {xb:.2f} against the recording {rb:.2f}; offset {off:+.2f}')
        rep = build(item, rec, cv, p1, overlap, bright_offset=off)
    # every note's strike, calibrated in this stream (the slurred notes start from SLUR_STRIKE)
    shim = copy.copy(item)
    shim.stream = p1
    rises = [strike.rise_time(rec, a, b) for a, b, _ in rec_notes(item)]
    path, srep = strike.calibrate(shim, render, os.path.join(workdir, 'strike'), refine=refine + 1, log=log, every=True, rises=rises)
    rep.update(pressure_curves={str(n + 12 * o): {k: (v if not isinstance(v, list) else [round(float(z), 3) for z in v]) for k, v in c.items()}
                                for n, c in cv.items()}, strike=srep['final'], targets=srep['targets'], rise_targets=rises)
    return path, rep


def compare(rec, x, item, stream):
    """R against a render of `stream` (its notes.json clock): per note level re the median, periodicity,
    brightness; the dips at connected changes"""
    rn = rec_notes(item)
    sn = strike.notes_of(stream)
    t0 = sn[0]['t_on']
    xn = [(n['t_on'] - t0, n['t_off'] - t0, m) for n, (_, _, m) in zip(sn, rn)]
    out = {}
    for name, sig, nn in (('R', rec, rn), ('render', x, xn)):
        per, dips = measure(sig, nn)
        lv = np.array([p['level'] for p in per])
        out[name] = dict(level_re_median=[round(float(v - np.nanmedian(lv)), 1) for v in lv],
                         periodicity=[round(p['periodicity'], 3) for p in per], brightness=[round(p['brightness'], 2) for p in per], dips=dips)
    return out


def summary(cmp_):
    def s(d):
        lv = np.array(d['level_re_median'], float)
        dp = [x['dip_db'] for x in d['dips']]
        return dict(level_range_db=round(float(np.nanmax(lv) - np.nanmin(lv)), 1), periodicity=round(float(np.nanmedian(d['periodicity'])), 3),
                    brightness=round(float(np.nanmedian(d['brightness'])), 2), slur_dip_db=round(float(np.median(dp)), 1) if dp else None, slurs=len(dp))
    out = {k: s(v) for k, v in cmp_.items()}
    r, x = np.array(cmp_['R']['level_re_median'], float), np.array(cmp_['render']['level_re_median'], float)
    ok = np.isfinite(r) & np.isfinite(x)
    out['level_corr'] = round(float(np.corrcoef(r[ok], x[ok])[0, 1]), 2) if ok.sum() > 2 else None
    out['level_rms_err_db'] = round(float(np.sqrt(np.mean((r[ok] - x[ok]) ** 2))), 1) if ok.any() else None
    return out


def write_report(path, rep):
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(rep, f, indent=1, default=float)
