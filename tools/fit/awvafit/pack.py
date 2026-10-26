# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Listening packs: the same phrases (phrases.PACK_PHRASES) on every engine version of each
instrument, the references first (another engine's preset that a fit targets, and recordings),
the recordings loudness-matched (ITU-R BS.1770 integrated LUFS) to the renders; a folder per
instrument, index.md (what each file is, its loudness, the licences) and index.json (every file's
render manifest).

  python -m awvafit pack <config.json> <out dir> --renderer NAME=PATH ...

The config (JSON; no paths in it: the renderers are named, their builds given on the command line):
  {"title": "...", "scale": 0.25,
   "instruments": [
     {"name": "trombone", "centre": 62, "octave": -1,
      "reference": {"label": "the brass engine's trombone", "renderer": "v3", "engine": "brass", "preset": 2},
      "versions": [{"label": "brass family v2", "renderer": "v2", "engine": "trumpet", "preset": 2, "set": {},
                    "about": "what changed"}, ...],
      "recordings": [{"kind": "iowa", "source": "uiowa_mis", "dir": "Brass/tenortrombone", "dyns": ["pp", "mf", "ff"]}]}]}
Recording kinds: iowa (chromatic files per dynamic), mtg (the sfz mapping: "prefix": "ten", "dyns":
["p", "f"]), vsco (one sustain per file: "dir"). Phrases are centred on centre + 12 * octave; the
engine plays them through the key path (awva_render --rec, --transpose 12 * octave).
Renders are written at an embedded host's usual level (x scale, default 0.25 as awva_render --wav), 24-bit mono.
"""
import json
import math
import os
from concurrent.futures import ThreadPoolExecutor

import numpy as np

from . import labels, sources
from .audio import decode_ffmpeg, read, resample, write_wav
from .features.loudness import lufs
from .phrases import PACK_PHRASES, SCALE, pack_phrases
from .render import Renderer
from .segment import peak

SR = 48000
LICENCE_OF = {'iowa': 'uiowa_mis', 'mtg': 'mtg_solosax', 'vsco': 'vsco2ce'}


def slug(s):
    return s.replace(' ', '_').replace('/', '_')


def _load(p):
    if p.lower().endswith(('.aif', '.aiff', '.wav')):
        x, sr = read(p, np.float64)
        return resample(x, sr, SR)
    return decode_ffmpeg(p, SR).astype(np.float64)


class Recordings:
    def __init__(self):
        self.cache = {}

    def iowa(self, rel, dyn):
        key = ('iowa', rel, dyn)
        if key not in self.cache:
            d = sources.path('uiowa_mis', rel)
            notes = {}
            for f in sorted(os.listdir(d)):
                lab = labels.parse_iowa(f)
                if lab is None or lab['dyn'] != dyn or lab['vib']:
                    continue
                x = _load(os.path.join(d, f))
                for n, (a, b) in zip(lab['notes'], peak(x, SR, min_s=0.25, db=-24)):
                    notes.setdefault(n, (x, a, b))
            self.cache[key] = notes
        return self.cache[key]

    def mtg(self, prefix, dyn):
        key = ('mtg', prefix, dyn)
        if key not in self.cache:
            base = sources.path('mtg_solosax', 'MTG Solo Saxophones')
            fn = f'{prefix}_{dyn}_rr1.txt'
            p = os.path.join(base, 'Data', fn)
            regs = labels.mtg_regions(open(p).read(), fn) if os.path.exists(p) else []
            self.cache[key] = {k: os.path.join(base, 'Samples', smp + '.flac') for _, _, k, smp in regs}
        return self.cache[key]

    def take(self, src, note, dyn, length):
        """one recorded note, its first `length` s, faded out over 30 ms (None if there is none)"""
        if src['kind'] == 'iowa':
            nn = self.iowa(src['dir'], dyn)
            if note not in nn:
                return None
            x, a, b = nn[note]
            y = x[a:min(b, a + int(length * SR))].copy()
        elif src['kind'] == 'mtg':
            nn = self.mtg(src['prefix'], dyn)
            if note not in nn or not os.path.exists(nn[note]):
                return None
            y = _load(nn[note])[:int(length * SR)].copy()
        else:
            return None
        f = min(int(0.03 * SR), len(y) // 4)
        if f > 0:
            y[-f:] *= np.linspace(1, 0, f)
        return y


def _gap(s):
    return np.zeros(int(s * SR))


def _matched(y, target):
    """y at the target loudness, limited to a 0.99 peak; and a note when it was limited"""
    y = y * 10 ** ((target - lufs(y, SR)) / 20)
    pk = np.abs(y).max()
    if pk > 0.99:
        y *= 0.99 / pk
        return y, f' (peak limited: {20 * math.log10(0.99 / pk):+.1f} dB)'
    return y, ''


def build(config, out, renderers, workers=5, log=print):
    """render and write a pack; returns the index entries"""
    cfg = json.load(open(config)) if isinstance(config, str) else config
    scale = float(cfg.get('scale', 0.25))
    os.makedirs(out, exist_ok=True)
    work = os.path.join(out, '_work')
    os.makedirs(work, exist_ok=True)
    rec = Recordings()
    index = [f"# {cfg.get('title', 'Listening pack')}", '',
             'Every engine version plays the same phrases through the key path (awva_render --rec). Per instrument the references come '
             'first, then the versions in the order given; the file names sort that way (`<phrase>__<source>.wav`).', '', '**The phrases**', '']
    index += [f'- `{p}`: {d}' for p, d in PACK_PHRASES]
    index += ['', f'Renders are at the usual level of an embedded host (x{scale:g}). Each recording is matched in loudness (ITU-R BS.1770 integrated LUFS) to the '
              'mean of that instrument\'s renders of the same phrase; the level column is that loudness.', '']
    manifest = []
    jobs, plan = [], []
    for inst in cfg['instruments']:
        d = os.path.join(out, slug(inst['name']))
        os.makedirs(d, exist_ok=True)
        c, octave = int(inst['centre']), int(inst.get('octave', 0))
        csvs = {}
        for p, s in pack_phrases(c).items():
            csvs[p] = os.path.join(work, f"{slug(inst['name'])}_{p}.csv")
            s.write(csvs[p])
        items = []
        if inst.get('reference'):
            r = inst['reference']
            items.append(('0_reference_' + slug(r.get('tag', r['engine'] + '_engine')), f"the reference: {r['label']}", r))
        for i, v in enumerate(inst.get('versions', [])):
            items.append((f"{i + 1}_{slug(v['label'])}", f"{v['label']}" + (f": {v['about']}" if v.get('about') else ''), v))
        plan.append((inst, d, c, octave, items, csvs))
        for tag, _, v in items:
            for p in csvs:
                jobs.append((inst['name'], tag, v, 12 * octave, csvs[p], os.path.join(work, f"{slug(inst['name'])}_{tag}_{p}.f32"), p))

    def run(j):
        name, tag, v, T, csv, o, p = j
        if v['renderer'] not in renderers:
            raise KeyError(f"no --renderer {v['renderer']}=PATH")
        res = Renderer(renderers[v['renderer']]).render(v['engine'], v.get('preset'), rec=csv, transpose=T, sets=v.get('set'),
                                                        tail=1.2, out=o)
        return (name, tag, p), res

    with ThreadPoolExecutor(max_workers=workers) as ex:
        results = dict(ex.map(run, jobs))
    for inst, d, c, octave, items, csvs in plan:
        mid = c + 12 * octave
        index += [f"## {inst['name']}", '', f'Register: the phrases centre on MIDI {mid} (moved {octave:+d} octave).', '', 'What each file is:', '']
        index += [f'- `{tag}`: {desc}' for tag, desc, _ in items]
        rows, lev = [], {}
        for tag, desc, v in items:
            for p, _ in PACK_PHRASES:
                res = results[(inst['name'], tag, p)]
                x = res.x.astype(np.float64) * scale
                L = lufs(x, SR)
                lev.setdefault(p, []).append(L)
                fn = f'{p}__{tag}.wav'
                write_wav(os.path.join(d, fn), x, SR, bits=24)
                rows.append((fn, L))
                manifest.append(dict(instrument=inst['name'], file=f"{slug(inst['name'])}/{fn}", phrase=p, source=tag, lufs=L, render=res.manifest))
        used = set()
        for src in inst.get('recordings', []):
            kind = src['kind']
            if kind == 'vsco':
                parts = []
                dd = sources.path('vsco2ce', src['dir'])
                names = sorted(f for f in os.listdir(dd) if 'sustain' in f)
                for f in names:
                    parts += [_load(os.path.join(dd, f))[:int(2.0 * SR)], _gap(0.4)]
                if parts:
                    y, note = _matched(np.concatenate(parts), np.mean(lev['1_scale']))
                    fn = '0_reference_rec_vsco_sustains.wav'
                    write_wav(os.path.join(d, fn), y, SR, bits=24)
                    index.append(f'- `{fn}`: the recordings: every sustain of the set ({", ".join(names)}), 2 s each, at the mean loudness of the scale renders{note}')
                    rows.append((fn, lufs(y, SR)))
                    used.add(kind)
                continue
            dyns = src['dyns']
            middle = dyns[len(dyns) // 2] if len(dyns) == 3 else dyns[-1]
            notes = [mid + dd for dd in SCALE]
            sc = [rec.take(src, n, middle, 0.5) for n in notes]
            have = [n for n, y in zip(notes, sc) if y is not None]
            if have:
                y, note = _matched(np.concatenate([np.concatenate([yy, _gap(0.25)]) for yy in sc if yy is not None]), np.mean(lev['1_scale']))
                fn = f'1_scale__0_reference_rec_{kind}_{middle}.wav'
                write_wav(os.path.join(d, fn), y, SR, bits=24)
                index.append(f'- `{fn}`: the recordings ({kind}) at {middle}: the notes of the scale they have, MIDI {", ".join(map(str, have))}, 0.5 s each{note}')
                rows.append((fn, lufs(y, SR)))
                used.add(kind)
            dy = [(dn, rec.take(src, mid, dn, 2.0)) for dn in dyns]
            dy = [(dn, y) for dn, y in dy if y is not None]
            if dy:
                y, note = _matched(np.concatenate([np.concatenate([yy, _gap(0.4)]) for _, yy in dy]), np.mean(lev['2_swells']))
                fn = f'2_swells__0_reference_rec_{kind}_dynamics.wav'
                write_wav(os.path.join(d, fn), y, SR, bits=24)
                index.append(f'- `{fn}`: the recordings ({kind}): MIDI {mid} at {", ".join(dn for dn, _ in dy)}, 2 s each; one gain for the set, so the dynamics keep their recorded balance{note}')
                rows.append((fn, lufs(y, SR)))
                used.add(kind)
        if used:
            index += ['', 'Recordings: ' + '; '.join(sources.SOURCES[LICENCE_OF[k]]['citation'] for k in sorted(used)) + '.']
        index += ['', '| file | level (LUFS) |', '|---|---|'] + [f'| `{fn}` | {L:.1f} |' for fn, L in sorted(rows)] + ['']
    lic = sorted({LICENCE_OF[s['kind']] for i in cfg['instruments'] for s in i.get('recordings', [])})
    if lic:
        index += ['## Sources and licences', ''] + [f"- {sources.SOURCES[s]['title']}: {sources.SOURCES[s]['licence']}. {sources.SOURCES[s]['citation']}" for s in lic]
    with open(os.path.join(out, 'index.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(index) + '\n')
    with open(os.path.join(out, 'index.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=1)
    for fn in os.listdir(work):
        os.remove(os.path.join(work, fn))
    os.rmdir(work)
    log(f'pack: {out}, {len(jobs)} renders')
    return manifest
