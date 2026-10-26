# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The recordings: one registry of where each comes from, under which licence, how to cite it, and
the SHA-256 of every file. No recording is in this repository; they are fetched into the cache
($AWVA_REFS/<source>/<path>), checked against their hash, and LICENSES.md is written next to them.

Only sources whose licence is permissive are fetched (SOURCES[...]['permissive']): a source listed
with permissive False (it is known, but its terms restrict use) is refused.

  python -m awvafit sources list                      the sources, their licences, file counts
  python -m awvafit sources fetch <source|set> ...    download what is missing, verify every hash
  python -m awvafit sources import <dir> [set ...]    take files already on disk (found by name, kept
                                                      only if the hash matches) instead of downloading
  python -m awvafit sources verify [set ...]          re-hash the cache
  path(source, rel) / files(set)                      where a file is in the cache; a set's files

Sets name what an instrument uses (e.g. 'sax', 'flute', 'clarinet', 'brass'); sources.json holds
every file: source, path, bytes, sha256, sets.
"""
import hashlib
import json
import os
import shutil
import sys
import time
import urllib.parse
import urllib.request

from . import env

HERE = os.path.dirname(os.path.abspath(__file__))
REGISTRY = os.path.join(HERE, 'sources.json')

SOURCES = {
    'uiowa_mis': dict(
        title='University of Iowa Musical Instrument Samples (MIS)',
        author='Lawrence Fritts, University of Iowa Electronic Music Studios',
        home='https://theremin.music.uiowa.edu/MIS.html',
        base='https://theremin.music.uiowa.edu/sound files/MIS/',
        licence='the site\'s terms: "freely available on this website and may be downloaded and used for any projects, without restrictions"',
        spdx=None, permissive=True,
        citation='University of Iowa Electronic Music Studios, Musical Instrument Samples, recorded by Lawrence Fritts. https://theremin.music.uiowa.edu/MIS.html'),
    'mtg_solosax': dict(
        title='MTG Solo Saxophones',
        author='kinwie (sfzinstruments), from the Music Technology Group (Universitat Pompeu Fabra) sample packs on Freesound (20251, 20239, 20247, 20253)',
        home='https://github.com/sfzinstruments/MTG.SoloSax',
        base='https://raw.githubusercontent.com/sfzinstruments/MTG.SoloSax/b494d256549b3d088fdec176ce82867f8a1f58b2/',
        licence='Creative Commons Attribution 4.0 International', spdx='CC-BY-4.0', permissive=True,
        citation='MTG Solo Saxophones by kinwie, from the MTG (Universitat Pompeu Fabra) Freesound packs, CC BY 4.0 '
                 '(https://creativecommons.org/licenses/by/4.0/). Credit it wherever the recordings or works made from them are shared.'),
    'vsco2ce': dict(
        title='VS Chamber Orchestra 2: Community Edition',
        author='Versilian Studios (Sam Gossner)',
        home='https://github.com/sgossner/VSCO-2-CE',
        base='https://raw.githubusercontent.com/sgossner/VSCO-2-CE/440300901dfe9275fd84e0b7763af1f8443ae62e/',
        licence='CC0 1.0 Universal (public domain dedication)', spdx='CC0-1.0', permissive=True,
        citation='VS Chamber Orchestra 2: Community Edition, Versilian Studios, CC0 1.0. https://github.com/sgossner/VSCO-2-CE'),
    'philharmonia': dict(
        title='Philharmonia Orchestra sound samples',
        author='Philharmonia Orchestra',
        home='https://philharmonia.co.uk/resources/sound-samples/',
        base=None,
        licence='free to use in music, but not to be sold or redistributed as samples: not an open licence', spdx=None, permissive=False,
        citation='Philharmonia Orchestra sound samples (not used: the licence is not permissive)'),
}


def registry():
    with open(REGISTRY, encoding='utf-8') as f:
        return json.load(f)['files']


def files(*sets):
    """the registry's entries in any of the sets (all with none)"""
    return [e for e in registry() if not sets or set(e['sets']) & set(sets)]


def path(source, rel):
    return os.path.join(env.refs_root(), source, *rel.split('/'))


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def _ok(e, p):
    return os.path.exists(p) and os.path.getsize(p) == e['bytes'] and sha256(p) == e['sha256']


def _refuse(source):
    s = SOURCES[source]
    if not s['permissive']:
        raise PermissionError(f"{source}: {s['licence']} - not fetched (permissive licences only)")


def _get(url, tries=4):
    for k in range(tries):
        try:
            req = urllib.request.Request(url, headers={'User-Agent': 'awvafit'})
            with urllib.request.urlopen(req, timeout=300) as r:
                return r.read()
        except Exception as ex:  # a network hiccup: try again
            if k == tries - 1:
                raise
            print(f'  retry {url}: {ex}', file=sys.stderr)
            time.sleep(2 + 3 * k)


def write_licences(root=None):
    root = root or env.refs_root()
    used = sorted({e['source'] for e in registry()})
    out = ['# The reference recordings', '', 'Fetched by awvafit (tools/fit/awvafit/sources.py). They are not part of awva.', '']
    for s in used:
        d = SOURCES[s]
        out += [f"## {d['title']} ({s})", '', f"- by: {d['author']}", f"- from: {d['home']}", f"- licence: {d['licence']}",
                f"- cite: {d['citation']}", '']
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, 'LICENSES.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


def fetch(sets_or_sources, log=print):
    """download every missing or mismatching file of the sets (or whole sources); verify each"""
    want = [e for e in registry() if e['source'] in sets_or_sources or set(e['sets']) & set(sets_or_sources)]
    for src in sorted({e['source'] for e in want}):
        _refuse(src)
    n = 0
    for e in want:
        p = path(e['source'], e['path'])
        if _ok(e, p):
            continue
        url = SOURCES[e['source']]['base'] + e['path']
        url = urllib.parse.quote(url, safe=':/')
        data = _get(url)
        if hashlib.sha256(data).hexdigest() != e['sha256']:
            raise ValueError(f'{url}: the hash differs from the registry (the source changed?)')
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p + '.part', 'wb') as f:
            f.write(data)
        os.replace(p + '.part', p)
        n += 1
        log('fetched', e['source'], e['path'])
    write_licences()
    return n


def import_local(src_dir, sets=(), log=print):
    """files already on disk: found by base name under src_dir, copied into the cache when their
    hash matches the registry's"""
    by_name = {}
    for dp, _, fns in os.walk(src_dir):
        for fn in fns:
            by_name.setdefault(fn, []).append(os.path.join(dp, fn))
    n = miss = 0
    for e in files(*sets):
        _refuse(e['source'])
        p = path(e['source'], e['path'])
        if _ok(e, p):
            continue
        cands = [c for c in by_name.get(os.path.basename(e['path']), []) if os.path.getsize(c) == e['bytes'] and sha256(c) == e['sha256']]
        if not cands:
            miss += 1
            continue
        os.makedirs(os.path.dirname(p), exist_ok=True)
        shutil.copyfile(cands[0], p)
        n += 1
    write_licences()
    log(f'imported {n}, {miss} not found')
    return n, miss


def verify(sets=()):
    bad = [e for e in files(*sets) if not _ok(e, path(e['source'], e['path']))]
    return bad


def pin(source, local_root, rel_of, sets_of, out=REGISTRY):
    """(maintenance) add or update a source's entries from local files: rel_of(local path) -> the
    path in the source (None: skip), sets_of(rel) -> its sets"""
    reg = json.load(open(out, encoding='utf-8')) if os.path.exists(out) else {'files': []}
    keep = [e for e in reg['files'] if e['source'] != source]
    new = []
    for dp, _, fns in os.walk(local_root):
        for fn in sorted(fns):
            lp = os.path.join(dp, fn)
            rel = rel_of(os.path.relpath(lp, local_root).replace(os.sep, '/'))
            if rel is None:
                continue
            new.append(dict(source=source, path=rel, bytes=os.path.getsize(lp), sha256=sha256(lp), sets=sets_of(rel)))
    new.sort(key=lambda e: e['path'])
    rows = [json.dumps(e, separators=(', ', ': ')) for e in keep + new]
    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('{"files": [\n' + ',\n'.join(rows) + '\n]}\n')
    return len(new)
