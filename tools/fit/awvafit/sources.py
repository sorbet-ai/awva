# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The recordings: one registry of where each comes from, under which licence, how to cite it, and
the SHA-256 of every file. No recording is in this repository; they are fetched into the cache
($AWVA_REFS/<source>/<path>), checked against their hash, and LICENSES.md is written next to them.

Only sources whose licence is permissive are fetched (SOURCES[...]['permissive']): a source listed
with permissive False (it is known, but its terms restrict use) is refused. One exception, recorded
per source: use "fit-eval" (no stated licence; the owner has allowed fitting and evaluation only,
with no redistribution, while permission is asked of the authors): never fetched, but files already
on disk may be imported and read, and every read says so (allowed()). A source with "manual" (e.g.
URMP: its host serves it only to a browser) is not fetched either: it is downloaded by hand and
checked by its hash (sha256, else the md5 its host publishes).

Two kinds of entry:
  sources.json   one file each: source, path, bytes, sha256, sets (served file by file)
  archives.json  a whole archive (the Zenodo datasets): source, path (in the cache), url, bytes,
                 sha256 (and the md5 Zenodo publishes), the folder it is extracted into; every member
                 is checked against the CRC-32 the archive records for it (verify_archive)
A source's files live at $AWVA_REFS/<source>/[<root>/]<path> (root: SOURCES[...]['root'], the
extracted archive's folder).

  python -m awvafit sources list                      the sources, their licences, file counts
  python -m awvafit sources fetch <source|set> ...    download what is missing, verify every hash
  python -m awvafit sources import <dir> [set ...]    take files already on disk (found by name, kept
                                                      only if the hash matches) instead of downloading
  python -m awvafit sources verify [set ...]          re-hash the cache (and the archives' members)
  python -m awvafit sources fetch choralebricks       an archive: download, check, extract
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
ARCHIVES = os.path.join(HERE, 'archives.json')

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
    'choralebricks': dict(
        title='ChoraleBricks 1.1.0: a modular multitrack dataset for wind music research',
        author='Stefan Balke, Axel Berndt, Meinard Mueller (Transactions of the ISMIR, 2025, doi:10.5334/tismir.252)',
        home='https://doi.org/10.5281/zenodo.15081740',
        base=None, root='data',
        licence='Creative Commons Attribution 4.0 International (the sheet music excluded)', spdx='CC-BY-4.0', permissive=True,
        citation='ChoraleBricks 1.1.0, S. Balke, A. Berndt, M. Mueller, Transactions of the ISMIR 2025, doi:10.5281/zenodo.15081740, CC BY 4.0. '
                 'Credit it wherever the recordings, or works made from them, are shared.'),
    'goodsounds': dict(
        title='Good-sounds dataset',
        author='O. Romani Picas, H. Parra Rodriguez, D. Dabiri, H. Tokuda, W. Hariya, K. Oishi, X. Serra (MTG, Universitat Pompeu Fabra; AES 138th Convention, 2015)',
        home='https://zenodo.org/records/820937',
        base=None, root='data',
        licence='Creative Commons Attribution 4.0 International', spdx='CC-BY-4.0', permissive=True,
        citation='Good-sounds, O. Romani Picas et al., "A real-time system for measuring sound goodness in instrumental sounds", AES 2015; '
                 'Zenodo 820937, CC BY 4.0. Credit it wherever the recordings, or works made from them, are shared.'),
    'urmp': dict(
        title='URMP: University of Rochester Multi-Modal Music Performance dataset',
        author='B. Li, X. Liu, K. Dinesh, Z. Duan, G. Sharma (IEEE Transactions on Multimedia 21(2), 2019)',
        home='https://doi.org/10.5061/dryad.ng3r749',
        base=None, root='data', manual='Dryad serves it only through a browser check: download it by hand into $AWVA_REFS/urmp/dl/',
        licence='CC0 1.0 (Dryad: "available to the public under a CC0 instrument"; scholarly citation asked)', spdx='CC0-1.0', permissive=True,
        citation='URMP, B. Li, X. Liu, K. Dinesh, Z. Duan, G. Sharma, "Creating a Multitrack Classical Music Performance Dataset for Multimodal Music Analysis", '
                 'IEEE Transactions on Multimedia 21(2), 2019; Dryad doi:10.5061/dryad.ng3r749 (version 1, 2019-09-21), CC0 1.0.'),
    'bach10': dict(
        title='Bach10 dataset',
        author='Z. Duan, B. Pardo, C. Zhang (AIR Lab, University of Rochester, 2010)',
        home='https://labsites.rochester.edu/air/resource.html',
        base=None, root='data',
        licence='no licence stated; permission asked of the authors. The owner allows fitting and evaluation only, no redistribution',
        spdx=None, permissive=False, use='fit-eval',
        citation='Bach10, Duan, Pardo, Zhang 2010. Fitting and evaluation only; not redistributed.'),
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


def archives(*sources_or_sets):
    """the archive entries (archives.json) of the sources or sets (all with none)"""
    with open(ARCHIVES, encoding='utf-8') as f:
        a = json.load(f)['archives']
    return [e for e in a if not sources_or_sets or e['source'] in sources_or_sets or set(e['sets']) & set(sources_or_sets)]


def path(source, rel):
    """a file of a source in the cache: $AWVA_REFS/<source>/[<root>/]<rel>"""
    root = SOURCES[source].get('root')
    return os.path.join(env.refs_root(), source, *([root] if root else []), *rel.split('/'))


def archive_path(e):
    return os.path.join(env.refs_root(), e['source'], *e['path'].split('/'))


def allowed(source, purpose='read'):
    """(ok, note): may a source's files be used for purpose ('fetch', 'import', 'read')?"""
    s = SOURCES[source]
    if s['permissive']:
        return True, ''
    if s.get('use') == 'fit-eval' and purpose in ('import', 'read'):
        return True, f"{source}: {s['licence']}"
    return False, f"{source}: {s['licence']} - not used (permissive licences only)"


def sha256(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def _ok(e, p):
    return os.path.exists(p) and os.path.getsize(p) == e['bytes'] and sha256(p) == e['sha256']


def _refuse(source, purpose='fetch'):
    ok, note = allowed(source, purpose)
    if not ok:
        raise PermissionError(note)
    return note


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
    used = sorted({e['source'] for e in registry()} | {e['source'] for e in archives()})
    out = ['# The reference recordings', '', 'Fetched by awvafit (tools/fit/awvafit/sources.py). They are not part of awva.', '']
    for s in used:
        d = SOURCES[s]
        out += [f"## {d['title']} ({s})", '', f"- by: {d['author']}", f"- from: {d['home']}", f"- licence: {d['licence']}",
                f"- cite: {d['citation']}", '']
    os.makedirs(root, exist_ok=True)
    with open(os.path.join(root, 'LICENSES.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(out))


def _download(url, dst, sha, tries=4):
    """a large file to dst (streamed), its SHA-256 checked"""
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    for k in range(tries):
        try:
            h = hashlib.sha256()
            req = urllib.request.Request(url, headers={'User-Agent': 'awvafit'})
            with urllib.request.urlopen(req, timeout=600) as r, open(dst + '.part', 'wb') as f:
                for b in iter(lambda: r.read(1 << 20), b''):
                    h.update(b)
                    f.write(b)
            if h.hexdigest() != sha:
                raise ValueError(f'{url}: the hash differs from the registry (the source changed?)')
            os.replace(dst + '.part', dst)
            return
        except ValueError:
            raise
        except Exception as ex:  # a network hiccup: try again
            if k == tries - 1:
                raise
            print(f'  retry {url}: {ex}', file=sys.stderr)
            time.sleep(2 + 3 * k)


def extract(e, log=print):
    """an archive's members into the source's root (each checked by its CRC-32 as it is read)"""
    import zipfile
    dst = os.path.join(env.refs_root(), e['source'], e['into'])
    with zipfile.ZipFile(archive_path(e)) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            out = os.path.normpath(os.path.join(dst, info.filename))
            if not out.startswith(os.path.normpath(dst) + os.sep):  # no member leaves the folder
                continue
            if os.path.exists(out) and os.path.getsize(out) == info.file_size:
                continue
            os.makedirs(os.path.dirname(out), exist_ok=True)
            with z.open(info) as src, open(out + '.part', 'wb') as f:
                shutil.copyfileobj(src, f, 1 << 20)  # (zipfile raises on a CRC mismatch)
            os.replace(out + '.part', out)
    log('extracted', e['source'], e['path'])


def _archive_ok(e, p):
    if os.path.getsize(p) != e['bytes']:
        return False
    if e.get('sha256'):
        return sha256(p) == e['sha256']
    h = hashlib.md5()
    with open(p, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest() == e['md5']


def verify_archive(e, members=True):
    """[problems] of an archive entry: the archive's size and SHA-256; with members, every extracted
    file's CRC-32 against the archive's record (a member not extracted is not a problem: it is read
    from the archive when needed)"""
    import zipfile
    import zlib
    p = archive_path(e)
    if not os.path.exists(p):
        return [f"{e['path']}: missing (python -m awvafit sources fetch {e['source']})"]
    bad = []
    if not _archive_ok(e, p):
        bad.append(f"{e['path']}: size or hash differs from the registry")
    if members and not bad and e['path'].endswith('.zip'):
        dst = os.path.join(env.refs_root(), e['source'], e['into'])
        with zipfile.ZipFile(p) as z:
            for info in z.infolist():
                out = os.path.join(dst, *info.filename.split('/'))
                if info.is_dir() or not os.path.exists(out):
                    continue
                c = 0
                with open(out, 'rb') as f:
                    for b in iter(lambda: f.read(1 << 20), b''):
                        c = zlib.crc32(b, c)
                if c != info.CRC or os.path.getsize(out) != info.file_size:
                    bad.append(f"{info.filename}: differs from the archive")
    return bad


def fetch(sets_or_sources, log=print):
    """download every missing or mismatching file of the sets (or whole sources); verify each;
    archives are downloaded, checked and extracted"""
    for e in archives(*sets_or_sources):
        _refuse(e['source'])
        if SOURCES[e['source']].get('manual') or not e.get('url'):
            if os.path.exists(archive_path(e)) and not verify_archive(e, members=False):
                log(e['source'], 'is on disk and checks out; extract it by hand (', SOURCES[e['source']].get('manual', ''), ')')
                continue
            raise PermissionError(f"{e['source']}: not fetched automatically - {SOURCES[e['source']].get('manual', 'no download URL')}")
        if not (os.path.exists(archive_path(e)) and not verify_archive(e, members=False)):
            log('fetching', e['url'], f"({e['bytes'] / 1e9:.1f} GB)")
            _download(e['url'], archive_path(e), e['sha256'])
        extract(e, log)
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
        _refuse(e['source'], 'import')
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


def read_member(source, rel):
    """a source file's bytes: the extracted copy if there is one, else straight from its archive"""
    p = path(source, rel)
    if os.path.exists(p):
        with open(p, 'rb') as f:
            return f.read()
    import zipfile
    for e in archives(source):
        if os.path.exists(archive_path(e)):
            with zipfile.ZipFile(archive_path(e)) as z:
                try:
                    return z.read(rel)
                except KeyError:
                    continue
    raise FileNotFoundError(f'{source}: {rel} (python -m awvafit sources fetch {source})')


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
