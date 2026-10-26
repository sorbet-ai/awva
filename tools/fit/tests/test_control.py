# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The stream calibration's tests (awvafit.strike, awvafit.control), on generated data only.

  python -m unittest discover -s tools/fit/tests -v
"""
import atexit
import json
import math
import os
import shutil
import sys
import tempfile
import types
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from awvafit import control, keys, strike  # noqa: E402

SR = 48000
TMP = tempfile.mkdtemp(prefix='awvafit_control_')
atexit.register(shutil.rmtree, TMP, ignore_errors=True)  # never left in %TEMP%


def tone(f0, dur, level=0.3, attack_db=0.0, sr=SR, noise=0.0, seed=0):
    t = np.arange(int(dur * sr)) / sr
    x = level * (np.sin(2 * np.pi * f0 * t) + 0.5 * np.sin(4 * np.pi * f0 * t))
    env = np.ones_like(t)
    a = int(0.05 * sr)
    env[:a] = 10 ** (attack_db / 20)  # a flat attack at attack_db over the sustain
    x = x * env
    if noise:
        x = x + noise * np.random.default_rng(seed).standard_normal(len(x))
    return x


def stream(path):
    s = keys.Stream()
    t = 0.3
    for n, v in ((62, 0.8), (67, 0.3), (64, 1.0)):
        end = s.press(t, v, 0.4, 0.6, note=n, shape=lambda u: 1.0 + 0.2 * math.sin(6 * u))
        t = end + 0.1
    s.write(path)
    return path


class Measures(unittest.TestCase):
    def test_overshoot_reads_the_attack(self):
        x = np.concatenate([np.zeros(SR // 10), tone(220, 1.0, attack_db=6.0)])
        pk, rr = strike.overshoot(x, 0.1, 1.1)
        self.assertAlmostEqual(pk, 6.0, delta=0.6)
        self.assertAlmostEqual(rr, 6.0, delta=1.0)
        flat = np.concatenate([np.zeros(SR // 10), tone(220, 1.0)])
        self.assertLess(abs(strike.overshoot(flat, 0.1, 1.1)[0]), 0.6)

    def test_periodicity_tells_a_tone_from_noise(self):
        self.assertGreater(control.periodicity(tone(220, 0.5), 220), 0.99)
        self.assertLess(control.periodicity(np.random.default_rng(1).standard_normal(SR // 2), 220), 0.3)
        self.assertLess(control.periodicity(tone(220, 0.5, noise=0.4), 220), control.periodicity(tone(220, 0.5, noise=0.05), 220))

    def test_brightness_is_the_centroid_over_f0(self):
        t = np.arange(SR) / SR
        self.assertAlmostEqual(control.brightness(np.sin(2 * np.pi * 300 * t), 300), 1.0, delta=0.05)
        self.assertGreater(control.brightness(tone(300, 1.0), 300), 1.2)


class Choose(unittest.TestCase):
    def curve(self):
        # overshoot rising 0 -> 12 dB over the strikes; the note speaks from 0.08 up
        return {(60, 0.4): [(v, 12 * (math.log(v / 0.05) / math.log(20)), (-20.0 if v < 0.08 else 3.0)) for v in strike.STRIKES]}

    def test_the_strike_that_gives_the_target(self):
        cv = self.curve()
        v = strike.choose(cv, 60, 0.4, 6.0)
        self.assertAlmostEqual(12 * math.log(v / 0.05) / math.log(20), 6.0, delta=0.5)

    def test_a_target_below_reach_takes_the_softest_that_speaks(self):
        self.assertGreater(strike.choose(self.curve(), 60, 0.4, -10.0), 0.06)  # where the RMS reaches -6 dB
        self.assertLess(strike.choose(self.curve(), 60, 0.4, -10.0, must_speak=False), 0.06)

    def test_pressure_for_inverts_the_level_curve(self):
        c = dict(P=[0.05, 0.1, 0.2, 0.4, 0.8], level=[-40, -30, -20, -10, 0], floor=0.08)
        self.assertAlmostEqual(control.pressure_for(c, -15), math.sqrt(0.2 * 0.4), delta=0.01)
        self.assertEqual(control.pressure_for(c, -60), 0.08)  # the floor
        self.assertEqual(control.pressure_for(c, 10), 0.8)  # no further than the sweep went


class Restrike(unittest.TestCase):
    def test_the_same_strikes_give_the_same_stream(self):
        src = stream(os.path.join(TMP, 'a.csv'))
        notes = strike.notes_of(src)
        out = os.path.join(TMP, 'a_same.csv')
        strike.restrike(src, out, {j: n['v'] * (1 + 1e-9) for j, n in enumerate(notes)})
        a = {(round(r[0], 4), r[2]): r[3] for r in strike.read_csv(out)[1]}
        b = {(round(r[0], 4), r[2]): r[3] for r in strike.read_csv(src)[1]}
        self.assertLessEqual(abs(len(a) - len(b)), len(notes))  # the release's last 2 ms step at most
        common = set(a) & set(b)
        self.assertGreater(len(common), 0.99 * len(b))
        self.assertLessEqual(max(abs(a[k] - b[k]) for k in common), 12)

    def test_a_softer_strike_rises_slower_and_keeps_the_end(self):
        src = stream(os.path.join(TMP, 'b.csv'))
        out = os.path.join(TMP, 'b_soft.csv')
        new = strike.restrike(src, out, {0: 0.1})
        old = strike.notes_of(src)
        self.assertAlmostEqual(new[0]['rise'], keys.K * math.pi * new[0]['P'] / 0.2, places=6)
        self.assertGreater(new[0]['rise'], 4 * old[0]['rise'])
        self.assertAlmostEqual(new[0]['t_off'], old[0]['t_off'], places=6)
        self.assertEqual(new[1], old[1])
        rows = strike.read_csv(out)[1]
        kid = old[0]['kid']
        v = [r[4] for r in rows if r[2] == kid and r[0] < 0.1 + old[0]['t_off']]
        self.assertLess(max(v) / 8191, 0.12)  # no step where the hold starts


class Ramp(unittest.TestCase):
    def test_a_soft_strike_on_a_quick_ramp(self):
        s = keys.Stream()
        s.press(0.3, 0.2, 0.5, 0.3, note=62, ramp=0.02)
        v = [r[4] / 8191 for r in s.rows]
        self.assertLess(max(v), 0.21)
        self.assertGreater(max(v), 0.15)
        self.assertAlmostEqual(s.notes[0]['rise'], 0.02)
        k = int(0.03 / keys.DT)
        self.assertLess(max(v[k:k + 100]), 0.06)  # no velocity jump after the ramp (a second strike)

    def test_restrike_sets_a_ramp(self):
        src = stream(os.path.join(TMP, 'r.csv'))
        new = strike.restrike(src, os.path.join(TMP, 'r2.csv'), {0: 0.15}, ramps={0: 0.03})
        self.assertAlmostEqual(new[0]['rise'], 0.03)
        self.assertTrue(new[0]['ramp'])

    def test_rise_time(self):
        x = np.concatenate([np.zeros(SR // 10), tone(220, 0.6)])
        ramp = np.ones(len(x))
        a = SR // 10
        ramp[a:a + int(0.05 * SR)] = np.linspace(0, 1, int(0.05 * SR))
        r = strike.rise_time(x * ramp, 0.1, 0.7)
        self.assertAlmostEqual(r, 0.04, delta=0.012)  # 10 -> 90 % of a 50 ms linear ramp


class Room(unittest.TestCase):
    def test_a_room_measured_back(self):
        from awvafit import room
        rng = np.random.default_rng(2)
        x = np.concatenate(sum([[0.1 * rng.standard_normal(int(0.8 * SR)), np.zeros(int(0.7 * SR))] for _ in range(6)], []))
        x += 1e-5 * rng.standard_normal(len(x))
        for rt, drr in ((0.3, 6.0), (0.6, 0.0), (0.4, 15.0)):
            m = room.measure(room.apply(x, room.Room(rt, drr), SR, seed=1), SR)
            self.assertAlmostEqual(m.rt60_s, rt, delta=0.05 * rt)
            self.assertAlmostEqual(m.drr_db, drr, delta=1.5)
        self.assertIsNone(room.measure(x, SR).rt60_s)  # dry: no decay to read


class Gate(unittest.TestCase):
    def per_item(self, cand, base, between):
        from awvafit import gate
        fl = dict(gate.MIN_FLOOR)
        return [dict(id='x', floor=fl, cand={m: cand * fl[m] for m in fl}, base={m: base * fl[m] for m in fl},
                     between={m: between * fl[m] for m in fl})]

    def test_a_shared_range_fail_is_no_regression(self):
        from awvafit import gate
        c7 = dict(midi=96, dyn='loud', why=['off by -410 c'])
        e2 = dict(midi=40, dyn='soft', why=['flips'])
        rng = lambda c, b: [dict(failures=c, notes=[]), dict(failures=b, notes=[])]  # noqa: E731
        r = gate.verdict('t', 'c', 'b', self.per_item(1.0, 1.2, 0.5), rng([c7], [c7]))
        self.assertEqual(r['verdict'], 'identical')
        self.assertIn('both builds fail C7 loud', gate.table(r))
        r = gate.verdict('t', 'c', 'b', self.per_item(1.0, 2.0, 1.5), rng([c7, e2], [c7]))
        self.assertEqual(r['verdict'], 'fail')
        self.assertEqual([f['midi'] for f in r['range_new']], [40])
        self.assertEqual(gate.verdict('t', 'c', 'b', self.per_item(1.0, 2.0, 1.5), rng([], [e2]))['verdict'], 'pass')

    def test_the_verdicts(self):
        from awvafit import gate
        self.assertEqual(gate.verdict('t', 'c', 'b', self.per_item(1.0, 2.0, 1.5))['verdict'], 'pass')
        self.assertEqual(gate.verdict('t', 'c', 'b', self.per_item(2.0, 1.0, 1.5))['verdict'], 'fail')
        self.assertEqual(gate.verdict('t', 'c', 'b', self.per_item(1.0, 1.2, 0.5))['verdict'], 'identical')
        p = self.per_item(1.0, 3.0, 2.0)
        p[0]['cand']['dip'] = p[0]['cand']['recovery'] = 9 * p[0]['floor']['dip']  # one group far worse
        p[0]['cand']['recovery'] = 9 * p[0]['floor']['recovery']
        r = gate.verdict('t', 'c', 'b', p)
        self.assertEqual(r['verdict'], 'fail')
        self.assertIn('slurs', r['worse_groups'])

    def test_the_comb_leaves_noise_not_tone(self):
        from awvafit import gate
        t = tone(220, 0.5)
        r, p, _ = gate.comb_split(t, 220.0)
        self.assertLess(10 * math.log10(np.mean(r ** 2) / np.mean(p ** 2)), -40)
        n = t + 0.03 * np.random.default_rng(4).standard_normal(len(t))
        r, p, _ = gate.comb_split(n, 220.0)
        self.assertAlmostEqual(10 * math.log10(np.mean(r ** 2) / np.mean(p ** 2)), 10 * math.log10(0.03 ** 2 / np.mean(t ** 2)), delta=2)

    def test_a_note_that_does_not_speak_measures_without_a_crash(self):
        from awvafit import gate
        rng = np.random.default_rng(5)
        for quiet in (np.zeros(SR // 2), 1e-9 * rng.standard_normal(SR // 2), 1e-4 * rng.standard_normal(SR // 2)):
            f0 = gate._f0(quiet, 41.2)
            self.assertTrue(41.2 * 0.94 <= f0 <= 41.2 * 1.06)
            gate.comb_split(quiet, f0)
            x = np.concatenate([np.zeros(SR // 10), tone(41.2, 0.8), quiet, tone(55.0, 0.8)])
            notes = [(0.1, 0.9, 28), (0.9, 1.4, 29), (1.4, 2.2, 33)]
            F = gate.features(x, notes)
            self.assertEqual(len(F['notes']), 3)

    def test_features_on_a_phrase(self):
        from awvafit import gate
        x = np.concatenate([np.zeros(SR // 10)] + [tone(220 * 2 ** (k / 12), 0.6) for k in (0, 2, 4, 5)])
        notes = [(0.1 + 0.6 * k, 0.1 + 0.6 * (k + 1) - 0.01, 57 + d) for k, d in enumerate((0, 2, 4, 5))]
        F = gate.features(x, notes)
        self.assertEqual(len(F['notes']), 4)
        self.assertAlmostEqual(F['phrase']['range'], 0.0, delta=0.5)
        d = gate.distance(F, F)
        self.assertTrue(all(v is None or v == 0 for v in d.values()))


class GateRuns(unittest.TestCase):
    """the gate end to end on a synthetic phrase and a stand-in build (tests/fake_player.py): parallel
    renders never share a file, so workers 4 = workers 1, and a build against itself is identical"""

    def setUp(self):
        from awvafit import gate, phraseset
        self.gate, self.phraseset = gate, phraseset
        d = os.path.join(TMP, 'gate_runs')
        os.makedirs(d, exist_ok=True)
        self.items = []
        rng = np.random.default_rng(7)
        for k in range(4):  # four phrases, their recordings tones with a little noise
            s = keys.Stream()
            midis = [57 + int(v) for v in rng.integers(0, 8, 7)]
            t = 0.3
            notes = []
            for m in midis:
                end = s.press(t, 0.3, 0.4, 0.5, note=m)
                notes.append(dict(onset_s=t, offset_s=end - 0.02, midi=m, cents=0.0, attack_db=0.0))
                t = end + 0.15
            p = os.path.join(d, f'ph{k}.csv')
            s.write(p)
            rec = np.zeros(int((t + 1.0) * SR))
            for n in notes:
                a, b = int(n['onset_s'] * SR), int(n['offset_s'] * SR)
                tt = np.arange(b - a) / SR
                f = 440 * 2 ** ((n['midi'] - 69) / 12)
                rec[a:b] += 0.08 * sum(np.sin(2 * np.pi * f * j * tt) * 0.5 ** (j - 1) for j in range(1, 9)) * np.minimum(1, tt / 0.03)
            rec += 1e-3 * rng.standard_normal(len(rec))
            it = phraseset.Item(f'ph{k}', 'legato_line', 'test', 'Test', 'e', 'p', 0.0, p, 0.3, 'none', f'ph{k}.wav', 0.0, t + 1.0, notes)
            self.items.append((it, rec))
        self.saved = (gate.items_for, phraseset.reference, control.calibrated)
        by = {it.id: rec for it, rec in self.items}
        gate.items_for = lambda inst: [it for it, _ in self.items]
        phraseset.reference = lambda it, sr_out=48000: (by[it.id], SR)

        def calibrated(item, render, workdir, **kw):  # the stream as it is, copied into the build's folder
            import shutil
            os.makedirs(os.path.join(workdir, 'strike'), exist_ok=True)
            p = os.path.join(workdir, 'strike', 'calibrated.csv')
            shutil.copyfile(item.stream, p)
            shutil.copyfile(item.stream[:-4] + '.notes.json', p[:-4] + '.notes.json')
            return p, {}
        control.calibrated = calibrated
        player = os.path.join(HERE, 'fake_player.py')
        self.spec = lambda b: f'cmd[b{b}]:"{sys.executable}" "{player}" "{{csv}}" "{{out}}" {{transpose}} {b} {{legato}}'

    def tearDown(self):
        self.gate.items_for, self.phraseset.reference, control.calibrated = self.saved

    def test_parallel_runs_match_and_a_build_is_identical_to_itself(self):
        g = self.gate
        a = g.run('test', self.spec(0.6), self.spec(0.3), os.path.join(TMP, 'gw1'), workers=1, log=lambda m: None)
        b = g.run('test', self.spec(0.6), self.spec(0.3), os.path.join(TMP, 'gw4'), workers=4, log=lambda m: None)
        self.assertEqual(json.dumps(a['measures'], sort_keys=True, default=g._json), json.dumps(b['measures'], sort_keys=True, default=g._json))
        self.assertEqual(a['verdict'], b['verdict'])
        self.assertNotEqual(a['verdict'], 'identical')
        same = g.run('test', self.spec(0.6), self.spec(0.6), os.path.join(TMP, 'gws'), workers=4, log=lambda m: None)
        self.assertEqual(same['verdict'], 'identical')

    def test_a_run_deletes_its_renders_unless_kept(self):
        g = self.gate
        for keep, left in ((False, []), (True, ['b0', 'b1', 'phrases.json'])):
            w = os.path.join(TMP, f'gwk{int(keep)}')
            g.run('test', self.spec(0.6), self.spec(0.3), w, workers=2, log=lambda m: None, keep=keep)
            runs = os.listdir(os.path.join(w, 'test')) if os.path.isdir(os.path.join(w, 'test')) else []
            self.assertEqual(sorted(x for r in runs for x in os.listdir(os.path.join(w, 'test', r))), left)

    def test_a_run_s_paths_stay_short_whatever_the_phrase_ids(self):
        """the per-phrase folders are short hashes (phrases.json maps them back): the deepest file below a
        run's folder stays within gate.PATH_TAIL for the longest held-out id in gate_set.json, and longer"""
        import dataclasses
        g = self.gate
        with open(g.GATE_SET, encoding='utf-8') as f:
            longest = max((p['id'] for v in json.load(f)['instruments'].values() for p in v['phrases']), key=len)
        ids = [longest] + [longest + f'__{k}' * 30 for k in range(1, len(self.items))]  # past it too
        self.items = [(dataclasses.replace(it, id=i), rec) for (it, rec), i in zip(self.items, ids)]
        by = {it.id: rec for it, rec in self.items}
        self.phraseset.reference = lambda it, sr_out=48000: (by[it.id], SR)
        w = os.path.join(TMP, 'gwl')
        g.run('test', self.spec(0.6), self.spec(0.3), w, workers=2, log=lambda m: None, keep=True)
        (run,) = os.listdir(os.path.join(w, 'test'))
        rd = os.path.join(w, 'test', run)
        files = [os.path.relpath(os.path.join(a, n), rd) for a, _, ns in os.walk(rd) for n in ns]
        self.assertTrue(any(f.endswith('.dry.f32') for f in files))
        # awva_render writes a manifest (<render>.json) beside each render: the tail leaves room for it
        self.assertLessEqual(max(len(f) for f in files) + len('.json'), g.PATH_TAIL)
        self.assertFalse([f for f in files if longest in f])
        with open(os.path.join(rd, 'phrases.json'), encoding='utf-8') as f:
            self.assertEqual(sorted(json.load(f).values()), sorted(ids))

    @unittest.skipUnless(os.name == 'nt', "Windows' 260-character limit")
    def test_a_run_under_too_long_a_folder_is_refused_before_it_renders(self):
        from awvafit import env
        w = os.path.join(TMP, 'x' * (250 - len(TMP)))
        with self.assertRaises(env.EnvError):
            self.gate.run('test', self.spec(0.6), self.spec(0.3), w, workers=1, log=lambda m: None)
        self.assertFalse(os.path.exists(w))

    def test_the_objective_is_the_gate_s_score(self):
        g = self.gate
        items = [it for it, _ in self.items]
        calls = []
        cal = control.calibrated

        def counting(*a, **k):
            calls.append(1)
            return cal(*a, **k)
        control.calibrated = counting
        streams = {}
        near = g.objective(self.spec(0.5), items, workdir=os.path.join(TMP, 'obj'), streams=streams, streams_frozen=True)
        far = g.objective(self.spec(0.9), items, workdir=os.path.join(TMP, 'obj'), streams=streams, streams_frozen=True)
        self.assertEqual(len(calls), len(items))  # frozen: calibrated once, reused by the second build
        self.assertLess(near['groups']['harmonics'], far['groups']['harmonics'])  # the recordings roll off as 0.5 per harmonic
        self.assertTrue(math.isfinite(near['total']))
        self.assertEqual(set(near['groups']), set(g.GROUPS))
        control.calibrated = cal
        res = g.run('test', self.spec(0.5), self.spec(0.9), os.path.join(TMP, 'objg'), workers=2, log=lambda m: None)
        for m, v in near['measures'].items():  # the same scores as the gate's candidate column
            if v is not None and res['measures'][m]['cand'] is not None:
                self.assertAlmostEqual(v, res['measures'][m]['cand'], places=6)

    def test_the_objective_calibrates_each_build_on_itself_unless_frozen(self):
        """the calibration depends on the build that plays it (here: the stream's pressures set so the build
        reaches one level, as control.calibrated sets the breath): by default each call calibrates on its own
        build, so a build's score doesn't depend on what was scored before it; frozen streams carry the
        first build's calibration into the next build's score (the trap)"""
        g = self.gate
        items = [it for it, _ in self.items]
        calls = []
        cal = control.calibrated

        def levelled(item, render, workdir, **kw):  # the stream, its pressures set by this build's level
            calls.append(1)
            p, rep = cal(item, render, workdir, **kw)
            peak = float(np.max(np.abs(render(p))))
            with open(p[:-4] + '.notes.json', encoding='utf-8') as f:
                notes = json.load(f)
            for n in notes:
                n['P'] *= 0.2 / peak
            with open(p[:-4] + '.notes.json', 'w', encoding='utf-8') as f:
                json.dump(notes, f)
            return p, rep
        control.calibrated = levelled
        try:
            w = os.path.join(TMP, 'objc')
            own = g.objective(self.spec(0.9), items, workdir=w)
            g.objective(self.spec(0.3), items, workdir=w)
            again = g.objective(self.spec(0.9), items, workdir=w)
            self.assertEqual(len(calls), 3 * len(items))  # every call calibrated on its own build
            self.assertEqual(json.dumps(own['measures'], sort_keys=True), json.dumps(again['measures'], sort_keys=True))
            self.assertEqual([x for x in os.listdir(w) if x.startswith('cal_')], [])  # its calibration deleted
            streams = {}
            g.objective(self.spec(0.3), items, workdir=w, streams=streams, streams_frozen=True)
            frozen = g.objective(self.spec(0.9), items, workdir=w, streams=streams, streams_frozen=True)
            self.assertEqual(len(calls), 4 * len(items))  # the second build reused the first's calibration
            self.assertNotEqual(json.dumps(own['measures'], sort_keys=True), json.dumps(frozen['measures'], sort_keys=True))
        finally:
            control.calibrated = cal

    def test_the_objective_refuses_held_out_recordings(self):
        from awvafit import env, gate_set
        s, t = sorted(gate_set.held_out())[0]
        it = types.SimpleNamespace(id='x', source=s, ref_path=t)
        with self.assertRaises(env.EnvError):
            self.gate.objective(self.spec(0.5), [it], workdir=os.path.join(TMP, 'objh'))

    def test_a_command_must_place_the_octave(self):
        from awvafit import env
        with self.assertRaises(env.EnvError):
            self.gate.renderer('cmd[x]:player {csv} {out}', TMP)

    def test_engine_renders_go_beside_their_streams(self):
        seen = []

        class R:
            def render(self, engine, preset, rec, out, transpose, legato=False):
                seen.append(out)
                return types.SimpleNamespace(x=np.zeros(4, np.float32))
        import types
        r = strike.render_with(R(), 'e', 'p', 0.0, TMP)
        r(os.path.join(TMP, 'a', 'calibrated.csv'))
        r(os.path.join(TMP, 'b', 'calibrated.csv'))
        self.assertEqual(len(set(seen)), 2)
class Range(unittest.TestCase):
    def test_yin_tells_the_partial(self):
        from awvafit import range_check as RC
        t = np.arange(SR) / SR
        for f, want in ((110.0, 0.0), (220.0, 1200.0), (165.0, 702.0)):
            x = sum(np.sin(2 * np.pi * f * k * t) / k for k in range(1, 8))
            tr = RC.yin(x, 110 / 3.2, 110 * 3.2)
            self.assertAlmostEqual(float(np.median(1200 * np.log2(tr[:, 0] / 110))), want, delta=3)

    def test_a_wrong_partial_and_a_flip_fail(self):
        from awvafit import range_check as RC
        bad = {45: 2.0, 47: 'flip'}  # 45 sounds an octave up; 47 jumps up an octave in the middle

        def render_for(o):
            def render(csv):
                rows = strike.read_csv(csv)[1]
                x = np.zeros(int((rows[-1][0] + 1.0) * SR))
                for d in RC._stream(os.path.join(TMP, 'plan.csv'), [(m, lay) for m, lay in RC.plan('trombone')[o]]):
                    a, b = int(d['t_on'] * SR), int(d['t_off'] * SR)
                    t = np.arange(b - a) / SR
                    f = 440 * 2 ** ((d['midi'] - 69) / 12)
                    k = bad.get(d['midi'])
                    if k == 'flip':
                        ff = np.where(t < 0.6, f, 2 * f)
                        ph = 2 * np.pi * np.cumsum(ff) / SR
                    else:
                        ph = 2 * np.pi * f * (k or 1.0) * t
                    x[a:b] = 0.2 * sum(np.sin(j * ph) / j for j in range(1, 6))
                return x
            return render

        res = RC.check(render_for, 'trombone', os.path.join(TMP, 'rng'))
        failing = {f['midi'] for f in res['failures']}
        self.assertEqual(failing, {45, 47})


class Sweeps(unittest.TestCase):
    def test_a_sweep_runs_once_for_a_build(self):
        from concurrent.futures import ThreadPoolExecutor
        import time
        c = control.SweepCache({0: [57, 62, 67]})
        calls = []

        def make():
            calls.append(1)
            time.sleep(0.05)
            return 'curve'
        with ThreadPoolExecutor(8) as ex:
            got = list(ex.map(lambda _: c.get(('pressure', 0, (57, 62, 67)), make), range(16)))
        self.assertEqual(got, ['curve'] * 16)
        self.assertEqual(len(calls), 1)
        self.assertEqual(c.get(('other',), lambda: 'x'), 'x')

    def test_the_instrument_s_sweep_notes(self):
        it = types.SimpleNamespace(transpose=12.0, notes=[dict(midi=m) for m in (60, 64, 72)])
        it2 = types.SimpleNamespace(transpose=12.0, notes=[dict(midi=m) for m in (55, 67)])
        self.assertEqual(control.sweep_notes_for([it, it2]), {1: [43, 52, 60]})


class Voicing(unittest.TestCase):
    def test_poly_runs_measure_only_clean_onsets(self):
        from awvafit import gate
        notes = [(0.3, 0.8, 60), (0.78, 1.2, 62), (1.4, 1.9, 64), (1.95, 2.4, 65)]  # a slur, a gap, a short gap
        self.assertEqual(gate.clean_onsets(notes), [True, False, True, False])
        F = dict(notes=[dict(rise=10.0, overshoot=1.0, onset_noise=0.1, level=0.0) for _ in notes])
        gate.mask_onsets(F, gate.clean_onsets(notes))
        self.assertIsNone(F['notes'][1]['rise'])
        self.assertEqual(F['notes'][2]['rise'], 10.0)
        self.assertEqual(F['notes'][1]['level'], 0.0)  # only the onset measures

    def test_a_mono_run_needs_a_command_that_plays_legato(self):
        from awvafit import env, gate
        with self.assertRaises(env.EnvError):
            gate.renderer('cmd[x]:player {csv} {out} {octave}', TMP, 'mono')
        gate.renderer('cmd[x]:player {csv} {out} {octave}', TMP, 'poly')

    @unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'needs AWVA_RENDER')
    def test_legato_keys_play_one_voice(self):
        """two keys overlapping 0.4 s: a voice per key sounds both pitches in the overlap, mono legato
        only the newest"""
        from awvafit.render import Renderer
        s = keys.Stream()
        s.press(0.3, 0.3, 0.5, 1.0, note=60)
        s.press(0.8, 0.3, 0.5, 1.0, note=67)
        p = os.path.join(TMP, 'overlap.csv')
        s.write(p)
        lv = {}
        for legato in (False, True):
            x = Renderer().render('sine', None, rec=p, out=os.path.join(TMP, f'ov{int(legato)}.f32'), legato=legato).x.astype(np.float64)
            seg = x[int(0.75 * SR):int(1.15 * SR)]  # the second key's rise done, the first key still held
            S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
            f = np.fft.rfftfreq(len(seg), 1 / SR)

            def band(hz):
                return S[(f > hz * 0.97) & (f < hz * 1.03)].sum() + 1e-20
            lv[legato] = 10 * math.log10(band(261.63) / band(392.0))  # the old note re the new
        self.assertGreater(lv[False], -10)  # poly: the old note still sounds
        self.assertLess(lv[True], lv[False] - 20)  # legato: it is gone


class FitSets(unittest.TestCase):
    """no gate on a candidate fitted on held-out recordings"""

    def setUp(self):
        from awvafit import gate_set
        self.gs = gate_set
        self.held = sorted(gate_set.held_out())
        self.assertTrue(self.held)
        self.free = ('choralebricks', 'no/such/track.wav')

    def write(self, entries):
        p = os.path.join(TMP, f'fit_{len(entries)}.json')
        with open(p, 'w', encoding='utf-8') as f:
            json.dump({'items': entries}, f)
        return p

    def test_an_overlap_is_named(self):
        s, t = self.held[0]
        p = self.write([dict(set=s, track=t), dict(set=self.free[0], track=self.free[1])])
        tracks, over = self.gs.check(p)
        self.assertEqual(over, [(s, t)])
        self.assertEqual(len(tracks), 2)
        self.assertEqual(self.gs.check(self.write([dict(set=self.free[0], track=self.free[1])]))[1], [])

    def test_the_gate_refuses_an_overlapping_fit_set(self):
        from awvafit import env, gate
        s, t = self.held[0]
        p = self.write([dict(set=s, track=t)])
        with self.assertRaises(env.EnvError) as e:
            gate.main(['--inst', 'trumpet', '--candidate', 'brass:trumpet', '--baseline', 'brass:trumpet', '--fit-set', p])
        self.assertIn(t, str(e.exception))
        with self.assertRaises(SystemExit):  # a fit set, or --no-fit: said, not assumed
            gate.main(['--inst', 'trumpet', '--candidate', 'brass:trumpet', '--baseline', 'brass:trumpet'])

    def test_fit_tracks_allowed(self):
        s, t = self.held[0]
        items = [types.SimpleNamespace(source=s, ref_path=t), types.SimpleNamespace(source=self.free[0], ref_path=self.free[1]),
                 dict(set=s, track=t), (self.free[0], self.free[1])]
        ok = self.gs.fit_tracks_allowed(items)
        self.assertEqual(len(ok), 2)
        self.assertTrue(all(self.gs._pair(x) == self.free for x in ok))


class Bursts(unittest.TestCase):
    def test_a_bright_burst_in_a_held_note(self):
        from awvafit.features.transients import hf_bursts
        ref = tone(440, 2.0)
        x = ref.copy()
        a = int(1.0 * SR)
        x[a:a + int(0.1 * SR)] += 0.05 * np.random.default_rng(3).standard_normal(int(0.1 * SR))
        got = hf_bursts(x, ref, SR)
        self.assertEqual(len(got), 1)
        self.assertAlmostEqual(got[0][0], 1.0, delta=0.03)
        self.assertEqual(hf_bursts(x, ref, SR, onsets=[1.03]), [])  # at an onset: an attack, not a glitch
        self.assertEqual(hf_bursts(ref, ref, SR), [])


class Sweep(unittest.TestCase):
    def test_sweep_schedule_and_threshold(self):
        sched = control.sweep_stream(os.path.join(TMP, 'sw.csv'), [60], pressures=(0.05, 0.1, 0.2, 0.4))
        self.assertEqual(len(sched), 4)
        self.assertEqual(sched[0]['t_on'], 0.0)
        # a fake render: noise below P 0.1, a tone from there, louder with pressure
        x = np.zeros(int((sched[-1]['t_off'] + 1) * SR))
        for d in sched:
            a = int(d['t_on'] * SR)
            n = int((d['t_off'] - d['t_on']) * SR)
            seg = tone(261.63, n / SR, level=d['P']) if d['P'] >= 0.1 else 0.01 * np.random.default_rng(2).standard_normal(n)
            x[a:a + len(seg)] = seg
        cv = control.curves(x, sched, 0)
        self.assertEqual(cv[60]['threshold'], 0.1)
        self.assertAlmostEqual(cv[60]['floor'], 0.125)


if __name__ == '__main__':
    unittest.main()
