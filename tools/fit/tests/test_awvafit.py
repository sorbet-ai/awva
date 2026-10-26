# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The toolkit's tests on a synthetic reference (synth.py: generated, never recorded), so they run on
every OS without a download:  python -m unittest discover -s tools/fit/tests -v

With AWVA_RENDER (awva_render) set, the renderer's tests run too: inputs, overrides, transposition,
the note list, the controls round trip, the manifest, a frame-wise comparison, a small CMA-ES fit
that finds an engine parameter again, a listening pack.
"""
import json
import math
import os
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import synth  # noqa: E402
from awvafit import audio, bake, controls, keys, labels, objective, phrases, segment, sources  # noqa: E402
from awvafit.features import loudness, measures  # noqa: E402
from awvafit.features.frames import FrameAnalysis  # noqa: E402
from awvafit.fit import Fit, Space  # noqa: E402
from awvafit.terms import framewise  # noqa: E402

SR = 48000
TMP = tempfile.mkdtemp(prefix='awvafit_test_')


def quad(params, wid):
    """a toy objective for the fit driver (module level: picklable)"""
    return (params['a'] - 0.3) ** 2 + (math.log(params['b']) - math.log(20.0)) ** 2, {'a': params['a']}


class Audio(unittest.TestCase):
    def test_aiff_wav_roundtrip(self):
        x = synth.tone(60, 0.5, 44100)
        p = os.path.join(TMP, 'a.aiff')
        synth.write_aiff(p, x, 44100)
        y, sr = audio.read(p)
        self.assertEqual(sr, 44100)
        self.assertLess(np.max(np.abs(y - x)), 1.5 / 32767)  # (written x 32767, read / 32768)
        q = os.path.join(TMP, 'a.wav')
        for bits in (16, 24, 32):
            audio.write_wav(q, x, 44100, bits=bits)
            z, sr = audio.read(q)
            self.assertLess(np.max(np.abs(z - x)), 2.0 / 2 ** (bits - 1) if bits < 32 else 1e-6)

    def test_resample(self):
        y = audio.resample(synth.tone(69, 0.5, 44100), 44100, 48000)
        self.assertAlmostEqual(len(y), 0.5 * 48000, delta=2)


class Labels(unittest.TestCase):
    def test_names(self):
        self.assertEqual(labels.midi('A4'), 69)
        self.assertEqual(labels.midi('Db3'), 49)
        self.assertEqual(labels.midi('C#4'), 61)
        self.assertEqual(labels.name(49), 'Db3')

    def test_iowa(self):
        r = labels.parse_iowa('AltoSax.NoVib.ff.C4B4.aiff')
        self.assertEqual((r['dyn'], r['lo'], r['hi'], r['vib']), ('ff', 60, 71, False))
        self.assertTrue(labels.parse_iowa('Flute.vib.mf.B3B4.aiff')['vib'])
        self.assertEqual(labels.parse_iowa('Flute.nonvib.mf.C7.aiff')['notes'], [96])
        self.assertEqual(labels.parse_iowa('BassClarinet.pp.Db2C2.aiff')['hi'], 48)  # (across the octave)
        self.assertIsNone(labels.parse_iowa('BbClar.ambient.silence.aiff'))

    def test_mtg_vsco(self):
        r = labels.mtg_regions('<region> key=49 sample=alt_p_01.$EXT\n<region> key=50 sample=alt_p_02.$EXT', 'alt_p_rr1.txt')
        self.assertEqual(r, [('alto', 'p', 49, 'alt_p_01'), ('alto', 'p', 50, 'alt_p_02')])
        self.assertEqual(labels.parse_vsco('piccolo_A#4_staccato1.wav')['written'], 70)


class Segment(unittest.TestCase):
    def test_methods_find_the_notes(self):
        notes = [60, 61, 62]
        x, times = synth.scale(notes, SR, seconds=1.2, gap=0.6)
        for method in ('plateau', 'runs', 'envelope', 'peak'):
            found = segment.notes(x, SR, method, expected=len(notes))
            self.assertEqual(len(found), 3, method)
            for n, (on, off) in zip(found, times):
                self.assertAlmostEqual(n.onset, on, delta=0.08, msg=method)
                self.assertAlmostEqual(n.off, off, delta=0.25, msg=method)

    def test_single(self):
        x = np.concatenate([np.zeros(SR // 2), synth.tone(72, 1.0), np.zeros(SR // 2)])
        (n,) = segment.notes(x, SR, 'single')
        self.assertAlmostEqual(n.onset, 0.5, delta=0.03)


class Measures(unittest.TestCase):
    def test_pitch_settle_harmonics_hnr(self):
        x = synth.tone(57, 1.5, SR, cents=12.0, rolloff=1.0, noise_db=-50, glide_cents=-60, glide_s=0.06, nh=60)
        m = measures.note_measures(x, SR, 57, 0.0, 1.45, nh=40)
        self.assertAlmostEqual(m['cents'], 12.0, delta=1.0)
        self.assertGreater(m['settle'], 0.05)
        self.assertLess(m['settle'], 0.4)
        # A_k = 1/k: harmonic 2 is 6 dB under 1, harmonic 4 12 dB
        self.assertAlmostEqual(m['harm'][0] - m['harm'][1], 20 * math.log10(2), delta=0.5)
        self.assertAlmostEqual(m['harm'][0] - m['harm'][3], 20 * math.log10(4), delta=0.5)
        self.assertAlmostEqual(m['tilt'], -6.0, delta=0.6)  # 1/k: -6 dB per octave
        noisy = measures.note_measures(synth.tone(57, 1.5, SR, noise_db=-15), SR, 57, 0.0, 1.45)
        self.assertGreater(m['hnr'], noisy['hnr'] + 10)

    def test_attack(self):
        x = np.concatenate([np.zeros(SR // 10), synth.tone(64, 1.0, SR, rise=0.08)])
        a = measures.attack(x, SR, onset=0.1, off=1.05, t_key=0.05)
        self.assertAlmostEqual(a['latency'], 0.05, delta=0.03)
        self.assertGreater(a['rise'], 0.03)
        self.assertLess(a['rise'], 0.1)

    def test_brightness_and_dynamics(self):
        dark = synth.tone(60, 1.0, SR, rolloff=2.0)
        bright = synth.tone(60, 1.0, SR, rolloff=0.7)
        self.assertGreater(measures.brightness(bright, SR), measures.brightness(dark, SR) + 5)
        per, mean = measures.dynamics_curve({('pp', 60): -30.0, ('mf', 60): -20.0, ('ff', 60): -10.0})
        self.assertEqual(mean, {'pp': -20.0, 'mf': -10.0, 'ff': 0.0})


class Loudness(unittest.TestCase):
    def test_bs1770(self):
        (b1, a1), (b2, a2) = loudness.k_weighting(48000)
        # the standard's coefficients at 48 kHz
        np.testing.assert_allclose(b1, [1.53512485958697, -2.69169618940638, 1.19839281085285], atol=1e-8)
        np.testing.assert_allclose(a1, [1.0, -1.69065929318241, 0.73248077421585], atol=1e-8)
        np.testing.assert_allclose(a2, [1.0, -1.99004745483398, 0.99007225036621], atol=1e-8)
        t = np.arange(10 * 48000) / 48000
        self.assertAlmostEqual(loudness.lufs(np.sin(2 * np.pi * 997 * t), 48000), -3.01, delta=0.02)
        x = 0.1 * np.sin(2 * np.pi * 440 * t)
        g = loudness.match_gain(x, 2 * x, 48000)
        self.assertAlmostEqual(g, 2.0, delta=1e-6)
        self.assertEqual(loudness.lufs(np.zeros(48000), 48000), -math.inf)


class Objective(unittest.TestCase):
    def test_total_and_combine(self):
        o = objective.Objective([('a', 2.0, 1.0), ('b', 4.0, 3.0)])
        self.assertEqual(o.total({'a': 2.0, 'b': 4.0, 'x': 99}), 4.0)
        c = o.combine({'f1': {'a': 1.0, 'b': 0.0}, 'f2': {'a': 3.0, 'b': 8.0}})
        self.assertEqual((c['a'], c['b'], c['total']), (2.0, 4.0, 4.0))
        self.assertEqual(o.failed(), {'a': 20.0, 'b': 40.0})
        self.assertEqual(o.with_terms([('c', 1.0, 1.0)]).names, ['a', 'b', 'c'])


class FitDriver(unittest.TestCase):
    def test_space(self):
        s = Space({'g': (1.0, 100.0, True), 'x': (-1.0, 1.0, False)})
        self.assertAlmostEqual(s.to_unit('g', 10.0), 0.5)
        self.assertAlmostEqual(s.from_unit('x', 0.75), 0.5)

    def test_cmaes_converges_and_resumes(self):
        space = Space({'a': (0.0, 1.0, False), 'b': (1.0, 100.0, True)})
        d = os.path.join(TMP, 'fit')
        best = Fit(d, space, quad, {'a': 0.9, 'b': 2.0}, sigma=0.3, pop=8, seed=3, workers=2).run(12)
        self.assertLess(best[0], 0.01)
        self.assertTrue(os.path.exists(os.path.join(d, 'checkpoint.pkl')))
        again = Fit(d, space, quad, {'a': 0.9, 'b': 2.0}, sigma=0.3, pop=8, seed=3, workers=2).run(14)  # resumes at 12
        self.assertLessEqual(again[0], best[0])
        with open(os.path.join(d, 'log.txt'), encoding='utf-8') as f:
            self.assertIn('resume at generation 12', f.read())


class Bake(unittest.TestCase):
    HEADER = '''struct Params { float gain, tone; };
static const P table[] = {
    {{"gain", "level", 0.0f, 10.0f, C_}, &Params::gain},
    {{"tone_hz", "tone", 100.0f, 9000.0f, C_}, &Params::tone},
};
static inline Params base_params() {
	Params p;
	p.gain = 1.0f;
	p.tone = 500.0f;
	return p;
}
[[nodiscard]] static inline Params preset_params(int i) {
	Params p = base_params();
	switch (i) {
	case 1: // a
		p.gain = 2.0f;
		break;
	}
	return p;
}
'''

    def test_case_assignments_switch(self):
        h = os.path.join(TMP, 'h.h')
        open(h, 'w').write(self.HEADER)
        self.assertEqual(bake.param_fields(self.HEADER), {'gain': 'gain', 'tone_hz': 'tone'})
        self.assertEqual(bake.bake_case(h, 1, {'gain': 1.0, 'tone_hz': 1234.5}, h, 'base_params', 'fitted'), 1)
        s = open(h).read()
        self.assertIn('\tcase 1: // fitted\n\t\tp.tone = 1234.5f;\n\t\tbreak;', s)  # gain equals the base: not written
        rep, add = bake.bake_assignments(h, 'base_params', {'gain': 3.0, 'tone_hz': 700})
        self.assertEqual((rep, add), (2, 0))
        bake.write_switch(h, '[[nodiscard]] static inline Params preset_params(int i) {', {0: {'gain': 4.0}, 1: {'gain': 5.0}}, {0: 'x', 1: 'y'}, default=1)
        s = open(h).read()
        self.assertIn('\tcase 0: // x\n\t\tp.gain = 4.0f;\n\t\tbreak;\n\tdefault:\n\tcase 1: // y', s)
        self.assertEqual(bake.literal(3.0), '3.0f')
        self.assertEqual(bake.literal(0.1234567, 'g6'), '0.123457f')


class KeysControls(unittest.TestCase):
    def test_press(self):
        s = keys.Stream()
        end = s.press(0.0, 0.5, 0.6, hold=0.3, note=62)
        self.assertAlmostEqual(s.notes[0]['rise'], keys.K * math.pi * 0.6 / (2 * 0.5))
        self.assertAlmostEqual(end, s.notes[0]['rise'] + 0.3 + 0.02)
        self.assertEqual(keys.note_of(s.notes[0]['kid']), 62)
        p = os.path.join(TMP, 'k.csv')
        s.write(p)
        rows = open(p).read().splitlines()
        self.assertEqual(rows[0], 't,src,id,pres,velo,x,y')
        self.assertEqual(max(int(r.split(',')[3]) for r in rows[1:]), round(0.6 * 8191))
        self.assertEqual(len(phrases.make('grid').notes), 18)

    def test_ctl_events(self):
        c = controls.Ctl(2.0)
        c.note(0, 0.1, 60, 0.5, 0.5, 0.5)
        c.note(1, 1.0, 64, 0.3, 0.2, 0.4)
        p = os.path.join(TMP, 'c.ctl')
        c.write(p)
        vc, g = controls.read(p)
        ev = controls.note_events(vc)
        self.assertEqual([(e['v'], round(e['note'])) for e in ev], [(0, 60), (1, 64)])
        self.assertAlmostEqual(ev[0]['P'], 0.5, places=5)
        self.assertEqual(g.shape[1], 8)


class Registry(unittest.TestCase):
    def test_every_file_is_pinned_and_permissive(self):
        reg = sources.registry()
        self.assertGreater(len(reg), 100)
        for e in reg:
            self.assertIn(e['source'], sources.SOURCES)
            self.assertTrue(sources.SOURCES[e['source']]['permissive'])
            self.assertRegex(e['sha256'], r'^[0-9a-f]{64}$')
            self.assertGreater(e['bytes'], 0)
            self.assertTrue(e['sets'])
        with self.assertRaises(PermissionError):
            sources._refuse('philharmonia')


class Stimuli(unittest.TestCase):
    def test_the_takes(self):
        from awvafit import stimuli
        d = stimuli.playing(renderer=None) if not os.environ.get('AWVA_RENDER') else stimuli.playing()
        self.assertGreater(len(d.strikes), 100)
        h = d.held()
        self.assertTrue(0.1 < np.median(h.strikes) < 0.6)
        s, sched = stimuli.note_set([62, 67], d)
        self.assertIn(0.07, {x['v'] for x in sched})  # the softest strike is always in
        s, sched = stimuli.strike_set([62], (0.07, 0.3))
        self.assertEqual([x['dyn'] for x in sched], ['s0.07', 's0.3'])


class Ranking(unittest.TestCase):
    def _note(self, x, midi=60):
        from awvafit.features import ranking as R
        on = R.onset(x, 0, int(0.5 * SR))
        return R.note(x, on, midi, 'mf')

    def test_attack_jtfs_free_metrics(self):
        from awvafit.features import ranking as R
        pad = np.zeros(int(0.1 * SR))
        fast = self._note(np.concatenate([pad, synth.tone(60, 1.6, SR, rise=0.01)]))
        slow = self._note(np.concatenate([pad, synth.tone(60, 1.6, SR, rise=0.09)]))
        self.assertEqual(R.attack(fast, fast), 0.0)
        self.assertGreater(R.attack(fast, slow), 2.0)
        self.assertLess(R.rise_ms(fast), R.rise_ms(slow))
        self.assertLess(R.early_level(slow), R.early_level(fast))
        self.assertEqual(R.mrstft(fast, fast), 0.0)
        self.assertGreater(R.mrstft(fast, slow), 0.0)

    def test_noise_per_period(self):
        from awvafit.features import ranking as R
        t = np.arange(int(1.8 * SR)) / SR
        f0 = 440 * 2 ** ((57 - 69) / 12)
        tone = 0.3 * np.sin(2 * np.pi * f0 * t)
        rng = np.random.default_rng(0)
        hiss = rng.standard_normal(len(t)) * 0.01
        pulse = (0.5 + 0.5 * np.cos(2 * np.pi * f0 * t)) ** 8  # noise in a burst once per period
        steady = self._note(np.concatenate([np.zeros(SR // 10), tone + hiss]), 57)
        pulsed = self._note(np.concatenate([np.zeros(SR // 10), tone + hiss * pulse * 3]), 57)
        a, b = R.noise_per_period(steady), R.noise_per_period(pulsed)
        self.assertLess(a[1], 0.1)
        self.assertGreater(b[1], a[1] + 0.1)  # (psm: the pulsing's contrast over a phase-scrambled baseline)
        self.assertLess(a[2].max(), 1.5)
        self.assertGreater(b[2].max() / b[2].min(), 10)  # the residual's energy over the period

    def test_backtest_on_synthetic_verdicts(self):
        from awvafit import verdicts
        root = os.path.join(TMP, 'verd')
        os.makedirs(os.path.join(root, 'soft'), exist_ok=True)
        sched, parts, t = [], [], 0.0
        for n in (60, 64, 67):
            sched.append(dict(note=n, dyn='s0.1', t_on=0.1 + t))
            t += 2.5
        for name, rise in (('ref', 0.02), ('good', 0.025), ('bad', 0.12)):
            x = np.concatenate([np.concatenate([np.zeros(int(0.05 * SR)), synth.tone(e['note'], 1.6, SR, rise=rise), np.zeros(int(0.85 * SR))])
                                for e in sched])
            x.astype(np.float32).tofile(os.path.join(root, 'soft', f'synth__{name}.mono.f32'))
        json.dump(sched, open(os.path.join(root, 'soft', 'synth.notes.json'), 'w'))
        V = dict(references={}, pairs=[dict(inst='synth', better='good', worse='bad', set='soft', ref='ref')],
                 metrics=dict(attack=dict(role='ranker', use=['soft/ref'])))
        res = verdicts.run(['attack', 'mrstft'], log=lambda *a: None, V=V, root=root)
        self.assertEqual(res['pairs'][0]['metrics']['attack']['wins'], 3)
        self.assertTrue(res['gate']['attack']['passes'])
        self.assertEqual(verdicts.sign_p(10, 12), 79 / 4096)


@unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'AWVA_RENDER not set')
class Constraints(unittest.TestCase):
    def test_max_rise(self):
        from awvafit import constraints as C
        d = tempfile.mkdtemp(dir=TMP)
        ok = C.evaluate([C.MaxRise(strikes=(0.0, 1.0), ms=2000.0)], 'va', 0, notes=(60,), workdir=d)
        self.assertEqual(ok.penalty, 0.0)
        self.assertEqual(len(ok.rows), 4)
        bad = C.evaluate([C.MaxRise(strikes=(0.0, 1.0), ms=0.5, tol=1.0)], 'va', 0, notes=(60,), workdir=d)
        self.assertGreater(bad.penalty, 0.0)
        self.assertEqual(len(bad.violations()), 4)


@unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'AWVA_RENDER not set')
class Renderer(unittest.TestCase):
    def setUp(self):
        from awvafit.render import Renderer as R
        self.r = R()
        self.d = tempfile.mkdtemp(dir=TMP)
        self.keys = phrases.write_all(self.d, ['grid', 'slurs'])

    def test_params_and_overrides(self):
        t = self.r.params('va', 0)
        self.assertEqual(t['cut_base'].group, 'filter')
        self.assertEqual(self.r.params('va', 0, {'cut_base': 1e6})['cut_base'].value, t['cut_base'].hi)  # clamped by its rule
        self.assertEqual(self.r.params('va', 0, {'cut_base': 40.5})['cut_base'].value, 40.5)

    def test_inputs_manifest_ctl_roundtrip(self):
        o = os.path.join(self.d, 'a.f32')
        c = os.path.join(self.d, 'a.ctl')
        a = self.r.render('va', 0, rec=self.keys['grid'], out=o, ctl_out=c, tail=0.5)
        m = a.manifest
        for f in ('engine', 'preset', 'input', 'sample_rate', 'block', 'voices', 'blocks', 'hash', 'peak', 'deterministic'):
            self.assertIn(f, m)
        self.assertEqual((m['engine'], m['block'], m['input_kind']), ('va', 32, 'rec'))
        self.assertGreater(m['peak'], 0.0)
        b = self.r.render('va', 0, ctl=c, out=os.path.join(self.d, 'b.f32'))
        self.assertEqual(b.manifest['hash'], m['hash'])  # the controls it got reproduce the render
        again = self.r.render('va', 0, rec=self.keys['grid'], out=os.path.join(self.d, 'c.f32'), tail=0.5)
        self.assertEqual(again.manifest['hash'], m['hash'])  # deterministic

    def test_transpose_notes_settle(self):
        c0, c1 = os.path.join(self.d, 't0.ctl'), os.path.join(self.d, 't1.ctl')
        self.r.render('va', 0, rec=self.keys['grid'], out=os.path.join(self.d, 't0.f32'), ctl_out=c0)
        self.r.render('va', 0, rec=self.keys['grid'], transpose=-12, out=os.path.join(self.d, 't1.f32'), ctl_out=c1)
        v0, v1 = controls.read(c0)[0], controls.read(c1)[0]
        held = v0[:, :, 1] > 0
        np.testing.assert_allclose(v1[:, :, 0][held], v0[:, :, 0][held] - 12)
        cn = os.path.join(self.d, 'n.ctl')
        self.r.render('va', 0, rec=self.keys['grid'], notes=[50, 52], out=os.path.join(self.d, 'n.f32'), ctl_out=cn)
        ev = controls.note_events(controls.read(cn)[0])
        self.assertEqual([round(e['note']) for e in ev[:4]], [50, 52, 50, 52])
        s = self.r.render('va', 0, rec=self.keys['grid'], settle=1.0, out=os.path.join(self.d, 's.f32'))
        self.assertEqual(s.manifest['blocks'], controls.read(c0)[0].shape[0])  # settling isn't written

    def test_framewise_and_fit_finds_a_parameter(self):
        """the reference: the va engine with its filter cutoff moved; a CMA-ES fit finds it again"""
        ref, ctl = os.path.join(self.d, 'ref.f32'), os.path.join(self.d, 'ref.ctl')
        self.r.render('va', 0, rec=self.keys['slurs'], sets={'cut_base': 50.0}, settle=1.0, out=ref, ctl_out=ctl)
        sc = CutoffScore(self.r.exe, ref, ctl, self.d)
        self.assertEqual(sc({'cut_base': 50.0, 'cut_env': 46.0}, 0)[0], 0.0)  # the same render: no distance
        self.assertGreater(sc({'cut_base': 10.0, 'cut_env': 46.0}, 0)[0], 1.0)
        space = Space({'cut_base': (-20.0, 80.0, False), 'cut_env': (0.0, 80.0, False)})
        best = Fit(os.path.join(self.d, 'fit'), space, sc, {'cut_base': 10.0, 'cut_env': 46.0}, sigma=0.2, pop=8, seed=2, workers=2).run(12)
        self.assertAlmostEqual(best[1]['cut_base'], 50.0, delta=4.0)
        with self.assertRaises(ValueError):  # CMA-ES needs two dimensions or more
            Fit(self.d, Space({'cut_base': (-20.0, 80.0, False)}), sc, {'cut_base': 10.0}).run(1)


class CutoffScore:
    """framewise distance of the va engine at a cutoff from the reference (picklable for the workers)"""

    def __init__(self, exe, ref, ctl, d):
        self.exe, self.ref, self.ctl, self.d = exe, ref, ctl, d
        self.obj = objective.Objective([('spec_s', 2.0, 1.5), ('spec_a', 3.0, 1.0), ('lvl', 2.5, 1.5)])

    def __call__(self, params, wid):
        from awvafit.render import Renderer as R
        A = FrameAnalysis()
        vc = controls.read(self.ctl)[0]
        o = os.path.join(self.d, f'w{wid}.f32')
        R(self.exe).render('va', 0, ctl=self.ctl, sets=params, settle=1.0, out=o)
        c = framewise.compare(A.analyse(np.fromfile(self.ref, np.float32), vc), A.analyse(np.fromfile(o, np.float32), vc), self.obj)
        return self.obj.total(c), c


@unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'AWVA_RENDER not set')
class Pack(unittest.TestCase):
    def test_pack(self):
        from awvafit import pack
        cfg = {'title': 'test pack', 'instruments': [
            {'name': 'pad', 'centre': 62, 'octave': 0, 'reference': {'label': 'the va engine, preset 0', 'renderer': 'r', 'engine': 'va', 'preset': 0},
             'versions': [{'label': 'v1', 'renderer': 'r', 'engine': 'va', 'preset': 1}, {'label': 'v2', 'renderer': 'r', 'engine': 'va', 'preset': 2}]}]}
        out = os.path.join(TMP, 'pack')
        man = pack.build(cfg, out, {'r': os.environ['AWVA_RENDER']}, workers=2, log=lambda *a: None)
        self.assertEqual(len(man), 15)
        self.assertTrue(os.path.exists(os.path.join(out, 'pad', '1_scale__0_reference_va_engine.wav')))
        with open(os.path.join(out, 'index.md'), encoding='utf-8') as f:
            idx = f.read()
        self.assertIn('| `5_bend__2_v2.wav` |', idx)
        with open(os.path.join(out, 'index.json'), encoding='utf-8') as f:
            self.assertEqual(len(json.load(f)), 15)


if __name__ == '__main__':
    unittest.main()
