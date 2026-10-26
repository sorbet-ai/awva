# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The back-test on Good-sounds' expert labels: which distances separate the good takes from the
deliberately bad ones (bad attack, pitch, timbre, dynamics, richness, air). A distance that can't tell
a bad attack from a good one shouldn't gate attacks.

  python -m awvafit backtest --goodsounds [--metrics attack,mrstft,...] [--json out.json]

The design (the research lane's, kept): per instrument session (trumpet/ramon, clarinet/pablo,
flute/almudena; the Neumann microphone), the references are the session's good-sound takes at each
semitone (every recorded dynamic). A take's score is its distance to the closest reference at its
semitone (so the dynamic doesn't decide). The good comparison takes are the session's good-attack
(and good-timbre, good-air) takes, which are not references. Per bad class, AUC = P(score(bad) >
score(good)) over bad/good pairs at the same semitone (ties 0.5): 1 = the distance always puts the bad
take farther, 0.5 = chance. At most 40 takes per class (a seeded shuffle).

Caveat: a bad class compares like with like only where it was recorded in the same session as good
comparison takes (the trumpet's and the clarinet's attack sessions; the flute's attack, air and timbre
sessions). The others (stability, dynamics, richness, the trumpet's and the clarinet's air) were
recorded on other days, so a distance can separate them by the day rather than the fault: their rows
are marked "confounded".

The recordings: sources 'goodsounds' (data/good-sounds/database.sqlite and sound_files/...; a file not
extracted is taken from the archive), read at 48 kHz.
"""
import os
import random
import sqlite3
from collections import defaultdict

import numpy as np

from . import sources
from .audio import decode_ffmpeg, read, resample
from .features import ranking as R

SESSIONS = {'trumpet': 'ramon', 'clarinet': 'pablo', 'flute': 'almudena'}
CAP = 40
SR = 48000


def is_good(k):
    return k.startswith('good-attack') or k.startswith('good-timbre') or k.startswith('good-air')


def _file(rel):
    """a Good-sounds file on disk (extracted from the archive on first use)"""
    p = sources.path('goodsounds', 'good-sounds/' + rel)
    if not os.path.exists(p):
        data = sources.read_member('goodsounds', 'good-sounds/' + rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p + '.part', 'wb') as f:
            f.write(data)
        os.replace(p + '.part', p)
    return p


def _note(row):
    sid, rel, midi, onset = row
    try:
        p = _file(rel)
        if p.lower().endswith('.wav'):
            x, sr = read(p, np.float64)
            x = resample(x, sr, SR)
        else:
            x = decode_ffmpeg(p, SR).astype(np.float64)
        on = int(onset) if onset else R.onset(x, 0, min(len(x), SR))
        n = R.note(x, max(on, 0), midi, '')
        if n is not None:  # the per-note features now: a take they fail on is left out (as the research lane did)
            R.cached(n, 'psn', R.noise_per_period)
            R.cached(n, 'lanes', R.lanes_feats)
            R.cached(n, 'ac', R.attack_curve)
        return n
    except Exception:  # an unreadable or too short take, or one the features fail on, is left out
        return None


def run(metrics=None, log=print, sessions=None):
    metrics = metrics or [m for m in R.METRICS if m != 'jtfs' or R.jtfs_available()]
    db = sqlite3.connect(_file('database.sqlite'))
    q = """select s.id, s.klass, s.semitone, s.attack, p.name, t.filename from sounds s
           join packs p on p.id = s.pack_id join takes t on t.sound_id = s.id
           where t.microphone = 'neumann' and p.name like ?"""
    rng = random.Random(0)
    res = {}
    for inst, who in (sessions or SESSIONS).items():
        by, packs_of = defaultdict(list), defaultdict(set)
        for sid, klass, semi, att, pack, fn in db.execute(q, (f'{inst}_{who}_%',)).fetchall():
            if klass and 'evaluation' not in pack:
                by[klass].append((sid, fn, int(semi), att))
                packs_of[klass].add(pack)
        # a bad class is clean where it was recorded in a session that also holds good comparison takes
        good_packs = {p for k, ps in packs_of.items() if is_good(k) for p in ps}
        sel = {}
        for k, v in by.items():
            rng.shuffle(v)
            sel[k] = v if (k == 'good-sound' or is_good(k)) else v[:CAP]
        notes = {}
        for v in sel.values():
            for r in v:
                if r[0] not in notes:
                    notes[r[0]] = _note(r)
        F = {k: [(r, notes[r[0]]) for r in v if notes.get(r[0]) is not None] for k, v in sel.items()}
        refs, goods = defaultdict(list), defaultdict(list)
        for r, n in F.get('good-sound', []):
            refs[r[2]].append(n)
        for k, v in F.items():
            if is_good(k):
                for r, n in v:
                    goods[r[2]].append(n)

        def score(x, semi):
            return {m: min(R.METRICS[m](x, ref) for ref in refs[semi]) for m in metrics}
        gscore = {semi: [score(g, semi) for g in gl] for semi, gl in goods.items() if refs.get(semi)}
        out = {}
        for k, v in F.items():
            if k == 'good-sound' or is_good(k) or not v:
                continue
            w = {m: [] for m in metrics}
            for r, b in v:
                if not refs.get(r[2]) or not gscore.get(r[2]):
                    continue
                sb = score(b, r[2])
                for sg in gscore[r[2]]:
                    for m in metrics:
                        w[m].append(1.0 if sb[m] > sg[m] else 0.0 if sb[m] < sg[m] else 0.5)
            if w[metrics[0]]:
                clean = bool(packs_of[k] & good_packs)
                out[k] = dict(auc={m: float(np.mean(x)) for m, x in w.items()}, pairs=len(w[metrics[0]]), clean=clean)
                log(f"{inst:9s} {k:36s} {'' if clean else '(confounded) '}" + ' '.join(f"{m} {out[k]['auc'][m]:.2f}" for m in metrics) +
                    f"  ({out[k]['pairs']} pairs)")
        res[inst] = out
    summary = {}
    for m in metrics:
        clean = [c['auc'][m] for o in res.values() for c in o.values() if c['clean']]
        allc = [c['auc'][m] for o in res.values() for c in o.values()]
        summary[m] = dict(clean_mean=float(np.mean(clean)) if clean else float('nan'), clean_n=len(clean),
                          all_mean=float(np.mean(allc)) if allc else float('nan'), separates=sum(a >= 0.75 for a in clean))
        log(f"{m:9s} the attack session's classes: mean AUC {summary[m]['clean_mean']:.2f}, {summary[m]['separates']} of "
            f"{summary[m]['clean_n']} at 0.75 or more | every class (confounded included): {summary[m]['all_mean']:.2f}")
    return dict(sessions=res, summary=summary)
