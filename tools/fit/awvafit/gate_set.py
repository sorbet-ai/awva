# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The gate's held-out recordings, and the check that no fit uses them.

gate_set.json holds out a third of each instrument's performances, whole (gate.choose_set). A fit that
uses any phrase of a held-out track measures itself on what it learnt from, and its gate means nothing.

  held_out()                    {(set, track)}: every held-out recording, of every instrument
  fit_tracks_allowed(items)     the items a fit may use: those not from a held-out track (items:
                                phraseset Items, {'set', 'track'} or {'set', 'id'} dicts, (set, track))
  overlap(tracks)               the held-out ones among (set, track) pairs, sorted
  read_fit_set(path)            the (set, track) pairs of a fit set's manifest: a JSON list (or the
                                list under "items", "fit" or "phrases") of {'set', 'id'} phrases,
                                {'set', 'track'} recordings or bare phrase ids (ChoraleBricks first,
                                then URMP)
  check(path)                   (tracks, overlap): read_fit_set and overlap; the gate refuses a fit set
                                that overlaps

  python -m awvafit gate --fit-set <manifest> ...   (or --no-fit for builds that fitted no recordings)
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
GATE_SET = os.path.join(HERE, 'gate_set.json')
SETS = ('choralebricks', 'urmp')


def held_out(path=None):
    with open(path or GATE_SET, encoding='utf-8') as f:
        ins = json.load(f)['instruments']
    return {(t['set'], t['track']) for v in ins.values() for t in v['tracks']}


_PHRASES = {}


def _track_of_id(set_name, pid):
    from .phraseset import load
    if set_name not in _PHRASES:
        try:
            _PHRASES[set_name] = {i.id: i.ref_path for i in load(set_name)}
        except Exception:  # the set isn't on disk
            _PHRASES[set_name] = {}
    return _PHRASES[set_name].get(pid)


def _pair(x):
    """(set, track) of an item, or None where it can't be told"""
    if isinstance(x, tuple) and len(x) == 2:
        return x
    if isinstance(x, str):
        for s in SETS:
            t = _track_of_id(s, x)
            if t:
                return s, t
        return None
    if isinstance(x, dict):
        if x.get('track'):
            return x.get('set', 'choralebricks'), x['track']
        if x.get('id'):
            if x.get('set'):
                t = _track_of_id(x['set'], x['id'])
                return (x['set'], t) if t else None
            return _pair(x['id'])
        return None
    ref = getattr(x, 'ref_path', None)
    if ref:
        src = getattr(x, 'source', None) or 'choralebricks'
        return src, ref
    return None


def overlap(tracks, path=None):
    h = held_out(path)
    return sorted({p for p in tracks if p in h})


def fit_tracks_allowed(items, path=None):
    """the items a fit may use: every item not from a held-out recording (an item whose recording can't
    be told is left out too: a fit must be able to say what it used)"""
    h = held_out(path)
    out = []
    for it in items:
        p = _pair(it)
        if p is not None and p not in h:
            out.append(it)
    return out


def read_fit_set(path):
    with open(path, encoding='utf-8') as f:
        d = json.load(f)
    if isinstance(d, dict):
        d = next((d[k] for k in ('items', 'fit', 'phrases', 'tracks') if k in d), None)
        if d is None:
            raise ValueError(f'{path}: no list of fit items (a list, or one under "items", "fit", "phrases" or "tracks")')
    pairs, unknown = [], []
    for x in d:
        p = _pair(tuple(x) if isinstance(x, list) else x)
        (pairs if p else unknown).append(p or x)
    if unknown:
        raise ValueError(f'{path}: {len(unknown)} fit items whose recording can\'t be told, e.g. {unknown[:3]}')
    return sorted(set(pairs))


def check(path, gate_set=None):
    tracks = read_fit_set(path)
    return tracks, overlap(tracks, gate_set)
