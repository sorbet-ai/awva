# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The reference sets' tests, on generated data only (no download): the registry's archives and
licence rules, a phrase manifest and its transition measures and distances, a Good-sounds-shaped
database for the good/bad back-test, and (with AWVA_RENDER) a phrase rendered and scored.

  python -m unittest discover -s tools/fit/tests -v
"""
import json
import math
import os
import sqlite3
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
from awvafit import audio, keys, sources  # noqa: E402

SR = 48000
TMP = tempfile.mkdtemp(prefix='awvafit_refs_')


def legato(notes, sr=SR, dur=0.6, glide=0.04, dip_db=8.0, lead=0.3):
    """notes played legato: the pitch glides between them over `glide` s, the level dips by dip_db at
    each change; (x, [(onset, offset)])"""
    n = int((lead + dur * len(notes) + 0.5) * sr)
    t = np.arange(n) / sr
    f = np.full(n, 440 * 2 ** ((notes[0] - 69) / 12))
    lvl = np.zeros(n)
    times = []
    for k, m in enumerate(notes):
        a = lead + k * dur
        times.append((a, a + dur - 0.01))
        if k:
            f0, f1 = 440 * 2 ** ((notes[k - 1] - 69) / 12), 440 * 2 ** ((m - 69) / 12)
            u = np.clip((t - a + glide / 2) / glide, 0, 1)
            f = np.where(t >= a - glide / 2, f0 * (f1 / f0) ** u, f)
            lvl -= dip_db * np.exp(-0.5 * ((t - a) / 0.012) ** 2)
    ph = 2 * np.pi * np.cumsum(f) / sr
    y = sum(np.sin(h * ph) / h for h in range(1, 8))
    env = 10 ** (lvl / 20) * ((t >= lead) & (t < lead + dur * len(notes)))
    return 0.2 * y * env, times


class Registry(unittest.TestCase):
    def test_archives_and_licences(self):
        a = sources.archives()
        self.assertEqual({e['source'] for e in a}, {'choralebricks', 'goodsounds'})
        for e in a:
            self.assertRegex(e['sha256'], r'^[0-9a-f]{64}$')
            self.assertTrue(e['url'].startswith('https://zenodo.org/'))
            self.assertTrue(sources.SOURCES[e['source']]['permissive'])
        self.assertEqual(sources.allowed('choralebricks'), (True, ''))
        ok, note = sources.allowed('urmp', 'read')
        self.assertTrue(ok)
        self.assertIn('fitting and evaluation only', note)
        self.assertFalse(sources.allowed('bach10', 'fetch')[0])
        self.assertFalse(sources.allowed('philharmonia', 'read')[0])

    def test_archive_check_and_members(self):
        import zipfile
        root = os.path.join(TMP, 'refs_a')
        os.environ['AWVA_REFS'] = root
        z = os.path.join(root, 'goodsounds', 'dl', 'g.zip')
        os.makedirs(os.path.dirname(z))
        with zipfile.ZipFile(z, 'w') as f:
            f.writestr('good-sounds/README.txt', 'hello')
        e = dict(source='goodsounds', path='dl/g.zip', bytes=os.path.getsize(z), sha256=sources.sha256(z), into='data', sets=[])
        self.assertEqual(sources.verify_archive(e), [])
        self.assertEqual(sources.read_member.__name__, 'read_member')
        sources.extract(e, log=lambda *a: None)
        p = sources.path('goodsounds', 'good-sounds/README.txt')
        self.assertEqual(open(p).read(), 'hello')
        open(p, 'w').write('HELLO')
        self.assertEqual(len(sources.verify_archive(e)), 1)  # the member differs from the archive's CRC


class Phrases(unittest.TestCase):
    def setUp(self):
        self.root = os.path.join(TMP, 'refs_p')
        os.environ['AWVA_REFS'] = self.root
        notes = [62, 65, 69]
        x, times = legato(notes, SR, glide=0.04, dip_db=8.0)
        wav = sources.path('choralebricks', '01_AudioAndAnnotations/Test/tracks/01_x.wav')
        os.makedirs(os.path.dirname(wav), exist_ok=True)
        audio.write_wav(wav, x, SR, bits=24)
        d = os.path.join(self.root, 'phrases', 'choralebricks')
        os.makedirs(os.path.join(d, 'brass'), exist_ok=True)
        s, t = keys.Stream(), 0.3
        for (a, b), m in zip(times, notes):
            s.press(a, 0.4, 0.5, hold=b - a - 0.02, note=m)
        s.write(os.path.join(d, 'brass', 'T.csv'))
        item = dict(id='T', kind='legato_line', family='brass', instrument='Trumpet', engine='va', preset='0', transpose=0,
                    stream='brass/T.csv', audio0_s=times[0][0], source='choralebricks',
                    reference=dict(source='choralebricks', path='01_AudioAndAnnotations/Test/tracks/01_x.wav', start_s=0.0, end_s=len(x) / SR),
                    notes=[dict(onset_s=a, offset_s=b, midi=m, cents=0, level_db=0.0) for (a, b), m in zip(times, notes)])
        json.dump(dict(format='awvafit-phrases/1', set='choralebricks', source='choralebricks', items=[item]),
                  open(os.path.join(d, 'manifest.json'), 'w'))

    def test_load_measure_distance(self):
        from awvafit import phraseset as P
        (it,) = P.load('choralebricks')
        self.assertEqual((it.engine, it.preset, it.transpose), ('va', '0', 0.0))
        y, sr = P.reference(it)
        T = P.transitions(y, sr, it, 0.0)
        self.assertEqual(len(T), 2)
        for r in T:
            self.assertTrue(r['slur'])
            self.assertAlmostEqual(r['dip_db'], 8.0, delta=3.0)
            self.assertLess(r['glide_ms'], 80)
            self.assertAlmostEqual(r['arrival_s'], r['at_s'], delta=0.06)
        d = P.distance(T, T)
        self.assertEqual(P.PHRASE_OBJECTIVE.total(d), 0.0)
        x2, _ = legato([62, 65, 69], SR, glide=0.15, dip_db=1.0)
        T2 = P.transitions(x2, SR, it, 0.0)
        d2 = P.distance(T, T2)
        self.assertGreater(d2['dip_db'], 3.0)
        self.assertGreater(d2['glide_shape'], 5.0)

    def test_first_manifest_form(self):
        """the builder's first, unversioned form: entry, oct, t_s/dur_s, from_, reference.audio"""
        from awvafit import phraseset as P
        d = os.path.join(self.root, 'phrases', 'choralebricks')
        m = json.load(open(os.path.join(d, 'manifest.json')))
        it = m['items'][0]
        old = dict(id='T', kind='legato_line', family='brass', instrument='Trumpet', entry='va:0', oct=-1, stream=it['stream'],
                   reference=dict(audio='choralebricks/data/' + it['reference']['path'], start_s=0.0, end_s=it['reference']['end_s']),
                   notes=[dict(t_s=n['onset_s'], dur_s=n['offset_s'] - n['onset_s'], midi=n['midi']) for n in it['notes']],
                   transitions=[dict(at_s=1.0, from_=62, to=65, slur=True, dip_db=1.0, dip_ms=0, level_step_db=0.0)])
        p = os.path.join(TMP, 'old_manifest.json')
        json.dump(dict(source='ChoraleBricks', items=[old]), open(p, 'w'))
        os.makedirs(os.path.join(TMP, 'brass'), exist_ok=True)
        import shutil
        shutil.copy(os.path.join(d, 'brass', 'T.csv'), os.path.join(TMP, 'brass', 'T.csv'))
        (o,) = P.load(path=p)
        self.assertEqual((o.engine, o.preset, o.transpose, o.source), ('va', '0', -12.0, 'choralebricks'))
        self.assertEqual(o.ref_path, it['reference']['path'])
        self.assertAlmostEqual(o.audio0_s, it['notes'][0]['onset_s'], places=3)  # (from the CSV's first row)
        self.assertEqual(o.transitions[0]['from'], 62)

    @unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'AWVA_RENDER not set')
    def test_render_and_score(self):
        from awvafit import phraseset as P
        (it,) = P.load('choralebricks')
        d, R, M = P.score(it, os.path.join(TMP, 'phr'), engine='va', preset='0')
        self.assertEqual(d['_n'], 2)
        self.assertEqual(len(M), 2)
        self.assertTrue(math.isfinite(d['total']))


class GoodSounds(unittest.TestCase):
    def test_backtest_separates_a_planted_fault(self):
        """good takes: fast clean attacks; bad-attack: slow, noisy attacks; the attack distance must
        separate them (AUC 1), a sustain-only distance need not"""
        from awvafit import goodsounds as G
        root = os.path.join(TMP, 'refs_g')
        os.environ['AWVA_REFS'] = root
        gs = sources.path('goodsounds', 'good-sounds')
        os.makedirs(os.path.join(gs, 'sound_files'), exist_ok=True)
        db = sqlite3.connect(os.path.join(gs, 'database.sqlite'))
        db.executescript('create table packs (id integer, name text, description text);'
                         'create table sounds (id integer, klass text, semitone integer, attack integer, pack_id integer);'
                         'create table takes (id integer, microphone text, filename text, sound_id integer);')
        db.executemany('insert into packs values (?, ?, ?)', [(1, 'trumpet_ramon_reference', ''), (2, 'trumpet_ramon_attack', '')])
        rng = np.random.default_rng(3)
        sid = 0
        for klass, pack, rise, noise in (('good-sound', 1, 0.01, 0.0), ('good-attack', 2, 0.012, 0.0), ('bad-attack-air', 2, 0.15, 0.08)):
            for k in range(4):
                sid += 1
                t = np.arange(int(1.8 * SR)) / SR
                x = 0.3 * sum(np.sin(2 * np.pi * h * 440 * t) / h for h in range(1, 6)) * np.clip(t / rise, 0, 1)
                x = x + noise * rng.standard_normal(len(t)) * np.exp(-t / 0.2)
                rel = f'sound_files/s{sid}.wav'
                audio.write_wav(os.path.join(gs, *rel.split('/')), np.concatenate([np.zeros(SR // 10), x]), SR, bits=16)
                db.execute('insert into sounds values (?, ?, ?, ?, ?)', (sid, klass, 69, None, pack))
                db.execute('insert into takes values (?, ?, ?, ?)', (sid, 'neumann', rel, sid))
        db.commit()
        db.close()
        res = G.run(['attack', 'mrstft'], log=lambda *a: None, sessions={'trumpet': 'ramon'})
        row = res['sessions']['trumpet']['bad-attack-air']
        self.assertTrue(row['clean'])
        self.assertEqual(row['pairs'], 16)
        self.assertEqual(row['auc']['attack'], 1.0)
        self.assertEqual(res['summary']['attack']['separates'], 1)


if __name__ == '__main__':
    unittest.main()
