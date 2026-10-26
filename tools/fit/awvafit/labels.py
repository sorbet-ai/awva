# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Note and dynamic labels: note names, and what each source's file names say.

  midi('Db3') -> 49; name(49) -> 'Db3' (flats, as Iowa names them); midi('C#4') also reads sharps
  DYNAMICS             the dynamic marks soft to loud; dyn_rank('mf')
  parse_iowa(fn)       University of Iowa MIS: <Instr>.[NoVib|nonvib|vib.]<dyn>.<lo>[<hi>].aiff ->
                       dict(stem, vib, dyn, lo, hi, notes) (a chromatic scale lo..hi), or None
  mtg_regions(data)    MTG Solo Saxophones' sfz mapping (Data/<inst>_<layer>_rr1.txt) ->
                       [(instrument, dynamic, key, sample)]
  parse_vsco(fn)       VSCO-2 CE: <instr>_<note><oct>_<articulation><n>.wav -> dict(written, art)
  parse_philharmonia(fn)  <instr>_<note><oct>_<dur>_<dynamic>_<art>.mp3 (listed for completeness: its
                       licence is not permissive, sources.py refuses it)
"""
import re

FLATS = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}
SHARPS = {'C#': 1, 'D#': 3, 'F#': 6, 'G#': 8, 'A#': 10}
FLAT_NAMES = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B']
DYNAMICS = ['ppp', 'pp', 'p', 'mp', 'm', 'mf', 'f', 'ff', 'fff']
PHILHARMONIA_DYN = {'pianissimo': 'pp', 'piano': 'p', 'mezzo-piano': 'mp', 'mezzo-forte': 'mf', 'forte': 'f', 'fortissimo': 'ff'}
MTG_INST = {'sop': 'soprano', 'alt': 'alto', 'ten': 'tenor', 'bar': 'baritone'}


def midi(s):
    m = re.fullmatch(r'([A-G](?:b|#)?)(-?\d)', s)
    if not m:
        raise ValueError(f'not a note name: {s!r}')
    pc = FLATS.get(m.group(1), SHARPS.get(m.group(1)))
    return 12 * (int(m.group(2)) + 1) + pc


def name(n):
    n = int(round(n))
    return f'{FLAT_NAMES[n % 12]}{n // 12 - 1}'


def dyn_rank(d):
    return DYNAMICS.index(d)


def parse_iowa(fn):
    """the Iowa MIS naming; None for a file that isn't a note sequence (e.g. ambient silence)"""
    if not fn.lower().endswith(('.aif', '.aiff')):
        return None
    parts = fn.rsplit('.', 1)[0].split('.')
    if len(parts) < 3:
        return None
    rng, dyn = parts[-1], parts[-2]
    if dyn not in DYNAMICS:
        return None
    m = re.findall(r'[A-G]b?\d', rng)
    if not m or ''.join(m) != rng:
        return None
    lo = midi(m[0])
    hi = midi(m[1]) if len(m) > 1 else lo
    if hi < lo:  # (a range written across the octave boundary)
        hi += 12
    flags = [p.lower() for p in parts[1:-2]]
    vib = 'vib' in flags
    return dict(stem=parts[0], vib=vib, nonvib=('novib' in flags or 'nonvib' in flags), dyn=dyn, lo=lo, hi=hi, notes=list(range(lo, hi + 1)))


def mtg_regions(text, layer_file):
    """[(instrument, dynamic, key, sample)] of one MTG mapping file (its name gives the layer)"""
    m = re.match(r'(sop|alt|ten|bar)_([pmf])_rr1\.txt', layer_file)
    if not m:
        return []
    return [(MTG_INST[m.group(1)], m.group(2), int(key), smp) for key, smp in re.findall(r'key=(\d+) sample=([a-z_0-9]+)', text)]


def parse_vsco(fn):
    m = re.fullmatch(r'([a-z]+)_([A-G]#?)(\d)_([a-z]+?)(\d+)\.wav', fn)
    if not m:
        return None
    return dict(instrument=m.group(1), written=midi(m.group(2) + m.group(3)), art=m.group(4), take=int(m.group(5)))


def parse_philharmonia(fn):
    m = re.fullmatch(r'([a-z-]+)_([A-G]s?)(\d)_([0-9a-z-]+)_([a-z-]+)_([a-z-]+)\.mp3', fn)
    if not m:
        return None
    pc = m.group(2).replace('s', '#')
    return dict(instrument=m.group(1), note=midi(pc + m.group(3)), dur=m.group(4), dyn=PHILHARMONIA_DYN.get(m.group(5), m.group(5)),
                art=m.group(6))
