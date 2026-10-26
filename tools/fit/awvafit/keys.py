# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Key streams as a pressure-sensing keyboard's scanner reports them: CSV t,src,id,pres,velo,x,y (raw
14 bit, 8191 = 1.0), which awva_render --rec plays through the key path (tools/harness/rec holds
recordings in the same format).

A key press is built as the scanner reports it: the pressure rises along a raised cosine to the
sustain level P in Tr, the strike (velo) is the pressure's slope times K (K = 0.033 s, fitted on the
recorded takes: velo ~ K dp/dt, saturating at 1), so a strike v at sustain P takes
Tr = K pi P / (2 v). Rows every 2 ms per key; the scanner's velocity lags a little (a one-pole).

  s = Stream()
  t = s.press(t0, v=0.5, P=0.4, hold=0.6, note=60)       # on a key that plays note 60 (LAYOUT)
  t = s.press(t0, v=0.5, P=0.4, hold=0.6, kid=23)        # on key 23 (the note set at render time)
  s.write('x.csv')                                        # and x.notes.json, every note's ground truth
"""
import json
import math

K = 0.033
DT = 0.002
# the default key layout (68 keys in four rows): key id -> note (note_of)
C0 = [6, 5, 5, 4, 4, 3, 2, 2, 1, 1, 0, 0, -1, -2, -2, -3, -3,
      5, 4, 4, 3, 3, 2, 1, 1, 0, 0, -1, -1, -2, -3, -3, -4, -4,
      4, 3, 3, 2, 2, 1, 0, 0, -1, -1, -2, -2, -3, -4, -4, -5, -5,
      3, 2, 2, 1, 1, 0, -1, -1, -2, -2, -3, -3, -4, -5, -5, -6, -6]
NKEYS = 68


def note_of(i):
    """the note key i plays in the default layout"""
    return 62 + 12 * C0[i] + 7 * ((i % 17) - 8)


KEYS_OF = {}
for _i in range(NKEYS):
    KEYS_OF.setdefault(note_of(_i), []).append(_i)


def key_for(note, avoid=()):
    """a key that plays the note (the first not in avoid)"""
    for i in KEYS_OF[note]:
        if i not in avoid:
            return i
    return KEYS_OF[note][0]


class Stream:
    def __init__(self):
        self.rows = []
        self.notes = []

    def press(self, t0, v, P, hold, rel=0.02, note=None, kid=None, bend=None, tilt=None, shape=None, avoid=()):
        """A key press at t0 (s): strike v (0..1), sustain pressure P, held `hold` s after the rise,
        released in `rel` s. On kid, else on a key that plays `note`. bend(t), tilt(t): x, y (-1..1)
        over the press; shape(t): a pressure multiplier over the hold. Returns the release's end."""
        if kid is None:
            kid = key_for(note, avoid)
        v = max(v, 0.01)
        tr = K * math.pi * P / (2 * v)
        tr = max(tr, 0.004)
        t_end = tr + hold
        t = 0.0
        vel_lp = 0.0
        prev = 0.0
        while True:
            if t < tr:
                p = P * 0.5 * (1 - math.cos(math.pi * t / tr))
            elif t < t_end:
                p = P * (shape(t - tr) if shape else 1.0)
            elif t < t_end + rel:
                pe = P * (shape(t_end - tr) if shape else 1.0)
                p = pe * 0.5 * (1 + math.cos(math.pi * (t - t_end) / rel))
            else:
                p = 0.0
            slope = (p - prev) / DT if t > 0 else p / DT
            prev = p
            vel_lp += 0.5 * (K * slope - vel_lp)  # the scanner's velocity lags a little
            velo = max(min(vel_lp, 1.0), -1.0)
            x = bend(t) if bend else 0.0
            y = tilt(t) if tilt else 0.0
            pr = int(round(p * 8191))
            self.rows.append((t0 + t, 0, kid, max(pr, 0), int(round(velo * 8191)), int(round(x * 8191)), int(round(y * 8191))))
            if t >= t_end + rel:
                break
            t += DT
        self.notes.append(dict(t_on=t0, t_off=t0 + t_end, note=note if note is not None else note_of(kid), kid=kid, v=v, P=P, rise=tr))
        return t0 + t_end + rel

    def write(self, path, notes=True):
        """the CSV (times + 0.1 s), and with notes <path>.notes.json (times re the first row)"""
        self.rows.sort(key=lambda r: r[0])
        with open(path, 'w') as f:
            f.write('t,src,id,pres,velo,x,y\n')
            for r in self.rows:
                f.write(f"{r[0] + 0.1:.4f},{r[1]},{r[2]},{r[3]},{r[4]},{r[5]},{r[6]}\n")
        if notes:
            with open(path.replace('.csv', '.notes.json'), 'w') as f:
                json.dump(self.notes, f, indent=0)

    @property
    def seconds(self):
        return max(r[0] for r in self.rows) if self.rows else 0.0
