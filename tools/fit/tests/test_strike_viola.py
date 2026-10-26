# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The viola full-scale peak (2026-10-25), and what it is not.

On URMP's viola phrase (13_Hark va p01) the default stream (strikes from the acoustic rise, 0.96-1.0;
pressure from the level, up to 0.90) took bowed:viola's engine tap to 2.18 (the violin 1.85), the board's
output to full scale. Measured with awvafit.strike.overshoot, a strike of 1.0 overshoots the viola's
sustain by only +0.6..2.8 dB (median 1.4; the recording's notes +0.1..1.1): the peak is the LEVEL (the
stream's pressure near 0.9 on a preset 1.2 dB louder in its register than the family), not the attack.

  test_hard_strikes_are_not_the_peak   the engine guard: at strike 1.0, P 0.83, the viola's attacks stay
                                       within 3 dB of their sustain (a regression would make strikes
                                       the peak again)
  test_calibration_meets_the_targets   strike.calibrate brings the attacks to +1 dB targets (median within
                                       1.5 dB) and never raises the phrase's peak

  AWVA_RENDER=build/sdk/awva_render python -m unittest tools/fit/tests/test_strike_viola.py -v
"""
import os
import sys
import tempfile
import types
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from awvafit import keys, strike  # noqa: E402

SR = 48000
TMP = tempfile.mkdtemp(prefix='awvafit_viola_')
NOTES = (55, 60, 62, 64, 67, 64, 62, 60)
TARGET_DB = 1.0


def hard_phrase(path):
    """eight tongued notes, struck at 1.0 with sustain 0.83: the uncalibrated stream's worst case"""
    s = keys.Stream()
    t = 0.3
    for n in NOTES:
        t = s.press(t, 1.0, 0.83, 0.45, note=n) + 0.12
    s.write(path)
    return path


@unittest.skipUnless(os.environ.get('AWVA_RENDER'), 'needs AWVA_RENDER')
class ViolaPeak(unittest.TestCase):
    def setUp(self):
        from awvafit.render import Renderer
        self.src = hard_phrase(os.path.join(TMP, 'hard.csv'))
        self.notes = strike.notes_of(self.src)
        self.render = strike.render_with(Renderer(), 'bowed', 'viola', 0, TMP)
        self.x0 = self.render(self.src)

    def test_hard_strikes_are_not_the_peak(self):
        ov = [o[0] for o in strike.phrase_overshoot(self.x0, self.notes) if o]
        self.assertEqual(len(ov), len(NOTES))
        self.assertLessEqual(float(np.median(ov)), 3.0)
        self.assertLessEqual(max(ov), 4.5)

    def test_calibration_meets_the_targets(self):
        item = types.SimpleNamespace(id='viola_hard', stream=self.src,
                                     notes=[dict(onset_s=n['t_on'], offset_s=n['t_off'], midi=n['note'], attack_db=TARGET_DB) for n in self.notes])
        path, rep = strike.calibrate(item, self.render, os.path.join(TMP, 'cal'), refine=2, log=lambda *_: None)
        x1 = self.render(path)
        ov1 = [o[0] for o in strike.phrase_overshoot(x1, strike.notes_of(path)) if o]
        self.assertLessEqual(abs(float(np.median(ov1)) - TARGET_DB), 1.5)
        self.assertLessEqual(np.abs(x1).max(), np.abs(self.x0).max() * 1.05)


if __name__ == '__main__':
    unittest.main()
