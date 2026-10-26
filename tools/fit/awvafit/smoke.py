# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The musical smoke test: every musical test performance (tools/fit/performances) on its engine.

The checks measure the dry render (the engine's own output, before the master effects): a reverb
tail can hold a note's pitch past its end and fool a pitch tracker, so pitch, speaking and clicks are
judged dry; only the listening render keeps the effects.

  check(path, engine=None, preset=None) -> Report: per note and overall
    speaks      every note's level (its middle half) within 30 dB of the loudest note's and over -70 dBFS
    pitch       every note of 0.15 s or more: its median pitch within `pitch_tol` cents of the score
                (the note and its cents: the performance's own pitch, Per-Note Controller 3)
    clicks      none (features.transients.clicks); onset transients (within 15 ms of an onset) listed
    dropouts    none: no 30 dB gap inside a held note
    level       finite, the peak at most 8 (the engine scale), the whole over -60 dBFS
    stops       1 s after the last note ends, the level 50 dB under the loudest note
  listen(path, ...) -> the listening render's loudness and true peak (it must be within 1 LU of its
                target and at or under its ceiling; awva_render --listen refuses otherwise)

  python -m awvafit smoke [--engine E --preset P] [performance.ump ...]   (default: every performance)
"""
import math
import os
from dataclasses import dataclass, field

import numpy as np

from . import env
from .features.loudness import lufs
from .features.measures import pitch_track
from .features.transients import clicks, dropouts, split
from .perform import read_perf
from .render import Renderer

HERE = os.path.dirname(os.path.abspath(__file__))
PERFORMANCES = os.path.join(os.path.dirname(HERE), 'performances')
SR = 48000


@dataclass
class Report:
    path: str
    engine: str
    preset: str
    hash: str = ''
    problems: list = field(default_factory=list)
    onset_transients: list = field(default_factory=list)
    pitch_med_c: float = math.nan
    pitch_max_c: float = math.nan
    notes: int = 0

    @property
    def ok(self):
        return not self.problems


def performances(root=None):
    root = root or PERFORMANCES
    return [os.path.join(dp, f) for dp, _, fs in os.walk(root) for f in sorted(fs) if f.endswith('.ump')]


def note_pitch(x, on, off, midi):
    a, b = int(on * SR), int(off * SR)
    span = b - a
    seg = x[a + span // 5:b - span // 5]
    if len(seg) < int(0.08 * SR):
        return math.nan
    tr = pitch_track(seg, SR, midi, win_s=max(0.0427, 3.0 / (440 * 2 ** ((midi - 69) / 12))))
    ok = (tr[:, 2] > 0.5) & np.isfinite(tr[:, 1])
    return float(np.median(tr[ok, 1])) if ok.sum() >= 3 else math.nan


def check(path, engine=None, preset=None, exe=None, workdir=None, pitch_tol=25.0):
    meta, score, _ = read_perf(path)
    eng, pre = engine or meta['engine'], preset if preset is not None else meta['preset']
    d = workdir or env.workdir('smoke')
    os.makedirs(d, exist_ok=True)
    out = os.path.join(d, f"{os.path.splitext(os.path.basename(path))[0]}__{eng}_{pre}.f32".replace(' ', '_'))
    res = Renderer(exe).render(eng, pre, ump=path, out=out)
    x = res.x.astype(np.float64)
    rep = Report(path, eng, pre, res.manifest['hash'], notes=len(score))
    if not res.finite:
        rep.problems.append('non-finite samples')
        return rep
    if res.manifest['peak'] > 8:
        rep.problems.append(f"peak {res.manifest['peak']:.2f} over the engine scale's 8")
    lv = []
    for on, off, *_ in score:
        a, b = int(on * SR), int(off * SR)
        mid = x[a + (b - a) // 4:b - (b - a) // 4]
        lv.append(10 * math.log10(np.mean(mid ** 2) + 1e-20) if len(mid) else -200.0)
    top = max(lv)
    silent = [k for k, l in enumerate(lv) if l < top - 30 or l < -70]
    if silent:
        rep.problems.append(f'notes that do not speak: {silent}')
    if 10 * math.log10(np.mean(x ** 2) + 1e-20) < -60:
        rep.problems.append('the whole render is under -60 dBFS')
    devs = []
    for k, (on, off, midi, cents, _) in enumerate(score):
        if off - on < 0.15:
            continue
        p = note_pitch(x, on, off, midi)
        if not math.isfinite(p):
            continue
        devs.append((k, p - cents))
    if devs:
        dv = np.array([abs(v) for _, v in devs])
        rep.pitch_med_c, rep.pitch_max_c = float(np.median(dv)), float(dv.max())
        off_pitch = [(k, round(v, 1)) for k, v in devs if abs(v) > pitch_tol]
        if off_pitch:
            rep.problems.append(f'notes off the score by more than {pitch_tol:g} c: {off_pitch}')
    ck, tr = split(clicks(x, SR), [on for on, *_ in score])
    if ck:
        rep.problems.append(f'clicks at {ck} s')
    rep.onset_transients = tr
    dr = dropouts(x, SR, [(on, off) for on, off, *_ in score])
    if dr:
        rep.problems.append(f'dropouts (note, s): {dr}')
    end = int((score[-1][1] + 1.0) * SR)
    if len(x) > end + SR // 10:
        tail = 10 * math.log10(np.mean(x[end:] ** 2) + 1e-20) - top
        if tail > -50:
            rep.problems.append(f'still sounding 1 s after the last note ({tail:.0f} dB)')
    return rep


def listen(path, engine=None, preset=None, exe=None, workdir=None, target=-23.0, ceiling=-1.0):
    """the listening render (awva_render --listen): (manifest, the WAV's loudness measured here, its true peak)"""
    from scipy.signal import resample_poly

    from .audio import read_channels
    meta, _, _ = read_perf(path)
    eng, pre = engine or meta['engine'], preset if preset is not None else meta['preset']
    d = workdir or env.workdir('smoke')
    stem = os.path.join(d, f"{os.path.splitext(os.path.basename(path))[0]}__{eng}_{pre}_listen".replace(' ', '_'))
    res = Renderer(exe).render(eng, pre, ump=path, out=stem + '.f32', wav=stem + '.wav', listen=True, lufs=target, true_peak=ceiling)
    a, sr, bits = read_channels(stem + '.wav')
    y = a / float(2 ** (bits - 1))
    tp = 20 * math.log10(max(np.abs(resample_poly(y, 4, 1, axis=0)).max(), 1e-12))
    return res.manifest, lufs(y, sr), tp
