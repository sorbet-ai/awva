# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Strike calibration: the phrase streams' strikes set per engine, so that each note's attack overshoots
its sustain by as much as the recording's own note does.

Why: the phrase builder infers a note's strike from how fast the recording's level rises (strike =
K pi P / (2 rise), keys.K calibrated on finger presses). An acoustic attack rises in 20-30 ms, so most
tongued notes come out at 0.7-1.0, and an engine whose attack grows with the strike plays them far
harder than the player did (a ChoraleBricks trumpet line: the recording's attacks +0.7 dB over the
sustain, the renders +8 to +11 dB, up to +20).

  overshoot(x, on, off)            (peak dB, RMS dB): the attack (-10..+100 ms around the onset) over the
                                   sustain (onset + 0.25 s to offset - 0.05 s; a short note: 40-85 % of it):
                                   its peak over the sustain's 99th-percentile peak, and its loudest 5 ms
                                   RMS over the sustain's median 5 ms RMS
  sweep_stream(path, notes, Ps)    a key stream of isolated presses: every (note, pressure, strike) of
                                   STRIKES, 1.2 s held, 0.6 s apart; returns the presses' schedule
  curve(x, schedule)               {(note, P): [(strike, peak dB, RMS dB)]} from the sweep's render
  choose(curve, note, P, target, prior)
                                   the strike whose overshoot (interpolated over log strike, the nearest
                                   note, linear in P) is nearest the target, among strikes at which the
                                   note speaks (RMS overshoot >= -6 dB: sound inside the attack window;
                                   not asked of a slurred note that spoke in the phrase, its breath held);
                                   within 0.5 dB, the one nearest the stream's own strike (prior)
  restrike(csv_in, csv_out, strikes)
                                   the stream with the given notes' rises rewritten (a raised cosine to
                                   the same P in K pi P / (2 v)), the hold's pressure kept at the same clock
                                   time (so it stays aligned with the recording's level) and the note's
                                   end kept, the velocity recomputed as the scanner reports it; other rows
                                   as they were
  calibrate(item, render, ...)     the whole: the sweep, a choice per struck note against the recording's
                                   overshoot, then `refine` passes in the phrase itself (each note's
                                   in-phrase residual is added to its curve and the choice made again);
                                   the final stream takes each note's best pass

render(csv_path) -> the engine's dry output (mono float, 48 kHz, time 0 = the stream's first row):
Renderer-based for awva_render (render_with), or any other player of key streams (a virtual board).
Slurred notes (strike 0.1, pressed before the last key lets go) keep their soft press.

  python -m awvafit strike <phrase id> --engine E --preset P --out calibrated.csv [--set choralebricks]
"""
import json
import math
import os

import numpy as np

from . import keys

SR = 48000
STRIKES = (0.05, 0.08, 0.12, 0.17, 0.25, 0.35, 0.5, 0.7, 1.0)
SLUR = 0.1
SPEAKS_DB = -6.0
RAMP_MIN, RAMP_MAX = 0.004, 0.3


def _windows(on, off):
    s0, s1 = on + 0.25, off - 0.05
    if s1 - s0 < 0.1:
        s0, s1 = on + (off - on) * 0.4, off - (off - on) * 0.15
    return (on - 0.01, on + 0.10), (s0, s1)


def overshoot(x, on, off, sr=SR):
    (a0, a1), (s0, s1) = _windows(on, off)
    att, sus = x[max(int(a0 * sr), 0):int(a1 * sr)], x[int(s0 * sr):int(s1 * sr)]
    if len(sus) < 0.05 * sr or not len(att):
        return None
    h = int(0.005 * sr)

    def rms(y):
        n = len(y) // h * h
        return np.sqrt(np.mean(y[:n].reshape(-1, h) ** 2, 1) + 1e-20)

    pk = 20 * math.log10(np.abs(att).max() + 1e-12) - 20 * math.log10(np.percentile(np.abs(sus), 99) + 1e-12)
    rr = 20 * math.log10(rms(att).max()) - 20 * math.log10(np.median(rms(sus)))
    return round(pk, 2), round(rr, 2)


def sweep_stream(path, notes, Ps, strikes=STRIKES, hold=1.2, gap=0.6, ramp=None):
    s = keys.Stream()
    t = 0.3
    sched = []
    for n in notes:
        for P in Ps:
            for v in strikes:
                end = s.press(t, v, P, hold, note=n, ramp=ramp)
                sched.append(dict(note=n, P=P, v=v, t_on=t, t_off=end - 0.02))
                t = end + gap
    s.write(path, notes=False)
    t0 = sched[0]['t_on']
    for d in sched:
        d['t_on'] -= t0
        d['t_off'] -= t0
    return sched


def curve(x, sched, sr=SR):
    out = {}
    for d in sched:
        m = overshoot(x, d['t_on'], d['t_off'], sr)
        if m:
            out.setdefault((d['note'], d['P']), []).append((d['v'], m[0], m[1]))
    return out


def _at(curve_, note, P, grid):
    """the overshoot (peak, RMS) over the strike grid at a note and pressure: the nearest swept note,
    linear in P between the swept pressures, linear in log strike between the swept strikes"""
    nn = min({n for n, _ in curve_}, key=lambda n: abs(n - note))
    Ps = sorted(p for n, p in curve_ if n == nn)

    def one(p):
        c = sorted(curve_[(nn, p)])
        ls = np.log([v for v, _, _ in c])
        return np.interp(np.log(grid), ls, [k for _, k, _ in c]), np.interp(np.log(grid), ls, [r for _, _, r in c])

    if P <= Ps[0] or len(Ps) == 1:
        return one(Ps[0])
    if P >= Ps[-1]:
        return one(Ps[-1])
    j = int(np.searchsorted(Ps, P))
    w = (P - Ps[j - 1]) / (Ps[j] - Ps[j - 1])
    (a, ar), (b, br) = one(Ps[j - 1]), one(Ps[j])
    return (1 - w) * a + w * b, (1 - w) * ar + w * br


def choose(curve_, note, P, target, prior=None, offset=0.0, tol=0.5, must_speak=True):
    grid = np.geomspace(STRIKES[0], STRIKES[-1], 81)
    pk, rr = _at(curve_, note, P, grid)
    pk = pk + offset
    ok = rr >= SPEAKS_DB if must_speak else np.ones(len(grid), bool)
    if not ok.any():
        return float(grid[int(np.argmax(rr))])
    err = np.where(ok, np.abs(pk - target), np.inf)
    near = np.where(err <= err.min() + tol)[0]
    if prior is not None:
        return float(grid[near[np.argmin(np.abs(np.log(grid[near]) - math.log(max(prior, 1e-3))))]])
    return float(grid[near[0]])


def rise_time(x, on, off, sr=SR):
    """the onset's rise (s): the 5 ms RMS envelope from 10 to 90 % of the way from its lowest point near
    the onset (-50..+30 ms: silence, or a slur's dip) to the level the note reaches (the 90th percentile
    over its first 250 ms, or less of a short note); None where the note never rises 6 dB"""
    h = int(0.005 * sr)
    a = max(int((on - 0.05) * sr), 0)
    b = int(min(on + 0.25, max(off, on + 0.06)) * sr)
    seg = x[a:b]
    n = len(seg) // h
    if n < 6:
        return None
    env = np.sqrt(np.mean(seg[:n * h].reshape(n, h) ** 2, 1))
    i0 = int(np.argmin(env[:min(int(0.08 / 0.005), n)]))
    base = env[i0]
    top = float(np.percentile(env[i0:], 90))
    if top < 2 * base or top <= 0:
        return None
    up = env[i0:]
    k10 = int(np.argmax(up >= base + 0.1 * (top - base)))
    k90 = int(np.argmax(up >= base + 0.9 * (top - base)))
    return max(k90 - k10, 0) * 0.005


def read_csv(path):
    rows = []
    with open(path) as f:
        head = f.readline()
        for line in f:
            p = line.strip().split(',')
            if len(p) == 7:
                rows.append([float(p[0])] + [int(v) for v in p[1:]])
    return head, rows


def notes_of(csv_path):
    with open(csv_path[:-4] + '.notes.json') as f:
        return json.load(f)


def restrike(csv_in, csv_out, strikes, rel=0.02, ramps=None):
    """strikes: {note index (notes.json order): new strike}; ramps: {note index: rise time s} (the
    pressure's rise set apart from the strike, keys.Stream.press(ramp=)); writes csv_out and its
    .notes.json"""
    ramps = ramps or {}
    head, rows = read_csv(csv_in)
    notes = notes_of(csv_in)
    off = rows[0][0] - min(n['t_on'] for n in notes)  # the CSV clock re the schedule's
    drop = set()
    new_rows = []
    out_notes = [dict(n) for n in notes]
    for j in sorted(set(strikes) | set(ramps)):
        n = notes[j]
        v = strikes.get(j, n['v'])
        T = ramps.get(j)
        if abs(v - n['v']) < 1e-9 and T is None:
            continue
        ton, tend, kid, P, tr = n['t_on'] + off, n['t_off'] + off, n['kid'], n['P'], n['rise']
        idx = [i for i, r in enumerate(rows) if r[2] == kid and ton - 1e-6 <= r[0] <= tend + rel + 2 * keys.DT + 1e-6]
        if not idx:
            raise ValueError(f'note {j}: no rows for key {kid} at {ton:.3f} s')
        t = np.array([rows[i][0] for i in idx])
        p = np.array([rows[i][3] for i in idx]) / 8191.0
        xs = np.array([rows[i][5] for i in idx]) / 8191.0
        ys = np.array([rows[i][6] for i in idx]) / 8191.0
        hold0 = tend - ton - tr
        # the hold follows the original pressure at the same clock time (aligned with the recording's
        # level), from the original rise's end; before it (a faster new rise) at its first value. The
        # rise goes to the hold's value where it starts, so the pressure has no step (a step would read
        # as a second strike), its time K pi P / (2 v) at that value
        h0, h1 = ton + tr, ton + tr + max(hold0, 0.0)
        H = lambda c: float(np.interp(min(max(c, h0), h1), t, p))  # noqa: E731
        Pr = P
        for _ in range(4):
            tr_new = max(keys.K * math.pi * Pr / (2 * max(v, 0.01)), 0.004) if T is None else max(T, 0.004)
            Pr = max(H(ton + tr_new), 1e-4)
        tr_new = max(keys.K * math.pi * Pr / (2 * max(v, 0.01)), 0.004) if T is None else max(T, 0.004)
        hold = max(tend - ton - tr_new, 0.05)
        shape = lambda u: H(ton + tr_new + u) / Pr  # noqa: E731
        s = keys.Stream()
        s.press(ton, v, Pr, hold, rel=rel, kid=kid, shape=shape, ramp=T,
                bend=lambda u: float(np.interp(ton + u, t, xs)), tilt=lambda u: float(np.interp(ton + u, t, ys)))
        drop.update(idx)
        new_rows += [list(r) for r in s.rows]
        out_notes[j].update(v=v, P=Pr, rise=tr_new, t_off=n['t_on'] + tr_new + hold)
        if T is not None:
            out_notes[j]['ramp'] = True
    keep = [r for i, r in enumerate(rows) if i not in drop] + new_rows
    keep.sort(key=lambda r: r[0])
    with open(csv_out, 'w', newline='\n') as f:
        f.write(head)
        for r in keep:
            f.write(f'{r[0]:.4f},{r[1]},{r[2]},{r[3]},{r[4]},{r[5]},{r[6]}\n')
    with open(csv_out[:-4] + '.notes.json', 'w', newline='\n') as f:
        json.dump(out_notes, f, indent=0)
    return out_notes


def struck(notes):
    return [j for j, n in enumerate(notes) if abs(n['v'] - SLUR) > 1e-9]


def targets(item, ref=None, sr=SR):
    """the recording's overshoot per note (peak dB), None where it can't be measured: the manifest's
    notes[].attack_db where it has them (the same measure), else measured on the reference here"""
    if all(n.get('attack_db') is not None for n in item.notes):
        return [n['attack_db'] for n in item.notes]
    if ref is None:
        from .phraseset import reference
        ref, _ = reference(item, sr)
    out = []
    for n in item.notes:
        off = n.get('offset_s') or (n['onset_s'] + n.get('dur_s', 0.5))
        m = overshoot(ref, n['onset_s'], off, sr)
        out.append(m[0] if m else None)
    return out


def phrase_overshoot(x, notes, sr=SR):
    t0 = notes[0]['t_on']
    return [overshoot(x, n['t_on'] - t0, n['t_off'] - t0, sr) for n in notes]


def phrase_rise(x, notes, sr=SR):
    t0 = notes[0]['t_on']
    return [rise_time(x, n['t_on'] - t0, n['t_off'] - t0, sr) for n in notes]


def calibrate(item, render, workdir, refine=1, Ps=None, log=print, every=False, rises=None, cache=None, sweep_notes=None):
    """-> (calibrated stream path, report dict). render(csv) -> the dry engine output of that stream.
    every: calibrate the slurred notes' strikes too (a held breath that re-attacks as the recording does).
    rises: the recording's onset rise per note (s, None where unmeasured): each such note's pressure
    rises over its own ramp, set apart from the strike, refined in the phrase until the render's rise
    is the recording's; the strike then sizes only the attack's overshoot"""
    os.makedirs(workdir, exist_ok=True)
    notes = notes_of(item.stream)
    tg = targets(item)
    if len(tg) != len(notes):
        raise ValueError(f'{item.id}: {len(tg)} reference notes, {len(notes)} in the stream')
    js = [j for j in (range(len(notes)) if every else struck(notes)) if tg[j] is not None]
    pitches = sorted({notes[j]['note'] for j in js})
    sw_notes = sweep_notes or sorted({pitches[0], pitches[len(pitches) // 2], pitches[-1]})
    Pv = [notes[j]['P'] for j in js]
    q = 0.05 if cache else 0.01  # shared sweeps: the pressures to 0.05, the ramp to 5 ms
    Ps = Ps or sorted({round(round(float(np.percentile(Pv, 25)) / q) * q, 3), round(round(float(np.percentile(Pv, 75)) / q) * q, 3)})
    ramps = {}
    if rises:
        ramps = {j: float(np.clip(rises[j], RAMP_MIN, RAMP_MAX)) for j in js if rises[j] is not None}
    ramp = float(np.median(list(ramps.values()))) if ramps else None
    if ramp is not None and cache:
        ramp = round(max(round(ramp / 0.005) * 0.005, RAMP_MIN), 3)

    def strike_sweep():
        sw = os.path.join(workdir, 'sweep.csv')
        sched = sweep_stream(sw, sw_notes, Ps, ramp=ramp)
        return curve(render(sw), sched), len(sched)
    cv, n_sched = cache.get(('strike', tuple(sw_notes), tuple(Ps), ramp), strike_sweep) if cache else strike_sweep()
    log(f'sweep: notes {sw_notes}, P {Ps}, {n_sched} presses')
    offs = {j: 0.0 for j in js}
    spoke = {j: False for j in js}  # a note that spoke in the phrase at any strike (a held breath) needs no floor
    path = item.stream
    hist = []
    for it_ in range(refine + 1):
        st = {j: choose(cv, notes[j]['note'], notes[j]['P'], tg[j], prior=notes[j]['v'], offset=offs[j], must_speak=not spoke[j]) for j in js}
        path = os.path.join(workdir, f'calibrated_{it_}.csv')
        restrike(item.stream, path, st, ramps=dict(ramps))
        x = render(path)
        meas = phrase_overshoot(x, notes_of(path))
        rz = phrase_rise(x, notes_of(path))
        ramps_used = dict(ramps)
        for j in ramps:  # the ramp, scaled by how far the render's rise is from the recording's
            if rz[j] is not None and rz[j] > 0:
                ramps[j] = float(np.clip(ramps[j] * np.clip((rises[j] / rz[j]) ** 0.8, 0.5, 2.0), RAMP_MIN, RAMP_MAX))
        for j in js:
            if meas[j] is None:
                continue
            pred = float(_at(cv, notes[j]['note'], notes[j]['P'], np.array([st[j]]))[0][0]) + offs[j]
            spoke[j] = spoke[j] or (every and abs(notes[j]['v'] - SLUR) < 1e-9 and meas[j][1] >= SPEAKS_DB)
            offs[j] += meas[j][0] - pred
        res = [abs(meas[j][0] - tg[j]) for j in js if meas[j]]
        hist.append(dict(strikes={j: round(v, 3) for j, v in st.items()}, ramps={j: round(v, 4) for j, v in ramps_used.items()},
                         measured=[m[0] if m else None for m in meas], rise=rz,
                         median_abs_error_db=round(float(np.median(res)), 2)))
        log(f'pass {it_}: median |overshoot - recording| {np.median(res):.1f} dB over {len(res)} notes')
    # the final stream: per note, the pass whose strike came nearest the recording (the notes are
    # separate presses, so the choices hardly interact); rendered once more to report what it gives
    def err(k, j):  # the overshoot's error (dB) and the rise's (10 ms counted as 1 dB)
        m = hist[k]['measured'][j]
        e = abs(m - tg[j]) if m is not None else math.inf
        if rises and rises[j] is not None and j in hist[k]['ramps']:
            r = hist[k]['rise'][j]
            e += abs(r - rises[j]) * 100 if r is not None else 3.0
        return e
    best = {j: min(range(len(hist)), key=lambda k: err(k, j)) for j in js}
    st = {j: hist[best[j]]['strikes'][j] for j in js}
    rp = {j: hist[best[j]]['ramps'][j] for j in js if j in hist[best[j]]['ramps']}
    path = os.path.join(workdir, 'calibrated.csv')
    restrike(item.stream, path, st, ramps=rp)
    x = render(path)
    meas = phrase_overshoot(x, notes_of(path))
    rz = phrase_rise(x, notes_of(path))
    res = [abs(meas[j][0] - tg[j]) for j in js if meas[j]]
    final = dict(strikes=st, ramps=rp, measured=[m[0] if m else None for m in meas], rise=rz,
                 median_abs_error_db=round(float(np.median(res)), 2))
    if rises:
        rr = [abs(rz[j] - rises[j]) * 1000 for j in rp if rz[j] is not None]
        final['median_abs_rise_error_ms'] = round(float(np.median(rr)), 1) if rr else None
    log(f'final: median |overshoot - recording| {np.median(res):.1f} dB over {len(res)} notes')
    return path, dict(phrase=item.id, sweep=dict(notes=sw_notes, P=Ps, curve={f'{n}@{p}': c for (n, p), c in cv.items()}),
                      targets=tg, rise_targets=rises, original_strikes=[n['v'] for n in notes], passes=hist, final=final)


def render_with(renderer, engine, preset, transpose, workdir, mono=False):
    """a render(csv) for calibrate() through awva_render --rec (the key path). The output goes beside the
    stream, so renders of different streams (phrases, builds) running at once never share a file;
    workdir is not used for it (kept for the callers)"""
    def render(csv_path):
        out = csv_path[:-4] + '.render.f32'
        r = renderer.render(engine, preset, rec=csv_path, out=out, transpose=transpose, mono=mono)
        return r.x.astype(np.float64)
    return render
