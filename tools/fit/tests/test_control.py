# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The stream calibration's tests (awvafit.strike, awvafit.control), on generated data only.

  python -m unittest discover -s tools/fit/tests -v
"""
import math
import os
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from awvafit import control, keys, strike  # noqa: E402

SR = 48000
TMP = tempfile.mkdtemp(prefix='awvafit_control_')


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
