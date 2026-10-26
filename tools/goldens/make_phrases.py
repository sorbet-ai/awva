#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# Writes the golden renders' MIDI phrases (sdk/tests/goldens/*.ump, awva_render --ump): timed
# Universal MIDI Packets at 48 kHz, one a line, "<frame> <word> ...", hex words, '#' comments.
#   mpe.ump     MIDI 1.0 in UMP (type 2): an MPE lower zone (member channels with their own
#               pitch bend, channel pressure and CC74; pressure before a Note On; a bend range by
#               RPN 0), and the master channel's notes with poly pressure and the mod wheel, its
#               pitch bend (the whole instrument) and the sustain pedal
#   midi2.ump   MIDI 2.0 (type 4): the same gestures in its resolution, with per-note pitch
#               (Registered Per-Note Controller 3, Pitch 7.25) and per-note timbre (Registered
#               Per-Note Controller 74), poly pressure, 16-bit velocities, the bend range as a
#               Registered Controller
#   sensor.ump  MIDI 2.0 from a controller with its own sensors: a sensor range of 4 (Assignable
#               Controller 1), each note's measured pressure rate and timbre (Assignable Per-Note
#               Controllers 1 and 2) and pitch (Registered Per-Note Controller 3) from a pressure,
#               a slide and a tilt in 14-bit codes, pressures past MIDI's full scale, motion
#               (Assignable Controllers 0x10, 0x14 .. 0x16) and the pedal
# At most 12 notes are held at once (the MIDI mapping holds 16; past that the oldest is stolen).
# Usage: tools/goldens/make_phrases.py  (writes the files; deterministic)
import math
import os

SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "..", "sdk", "tests", "goldens")


def frame(t):
    return int(round(t * SR))


class Phrase:
    def __init__(self, title):
        self.title = title
        self.ev = []  # (frame, order, words, comment)

    def add(self, t, words, comment=""):
        self.ev.append((frame(t), len(self.ev), words, comment))

    def write(self, name):
        self.ev.sort(key=lambda e: (e[0], e[1]))
        with open(os.path.join(OUT, name), "w", newline="\n") as f:
            f.write("# " + self.title + "\n# written by tools/goldens/make_phrases.py: <frame at 48 kHz> <UMP words, hex>\n")
            for fr, _, w, c in self.ev:
                f.write(str(fr) + " " + " ".join("%08x" % x for x in w) + (("  # " + c) if c else "") + "\n")


# --- MIDI 1.0 in UMP -------------------------------------------------------------------------------

def m1(status, d1=0, d2=0):
    return [0x20000000 | (status << 16) | ((d1 & 0x7f) << 8) | (d2 & 0x7f)]


def mpe():
    p = Phrase("MIDI 1.0 in UMP: an MPE lower zone, and the master channel")
    # the zone: RPN 6 (MCM) on the master channel, 15 members; the members' bend range 24 (RPN 0)
    p.add(0.0, m1(0xB0, 101, 0), "MCM: RPN 0/6 on channel 1")
    p.add(0.0, m1(0xB0, 100, 6))
    p.add(0.0, m1(0xB0, 6, 15), "15 member channels")
    p.add(0.0, m1(0xB1, 101, 0), "bend range: RPN 0/0 on a member channel")
    p.add(0.0, m1(0xB1, 100, 0))
    p.add(0.0, m1(0xB1, 6, 24), "24 semitones")
    p.add(0.0, m1(0xB1, 101, 127), "RPN deselected")
    p.add(0.0, m1(0xB1, 100, 127))
    # member notes: a chord swelling with channel pressure, vibrato by bend, CC74 sweeps
    keys = [48, 55, 60, 64, 67, 72]
    for n, k in enumerate(keys):
        ch = 1 + n
        on = 0.2 + 0.15 * n
        off = 4.0 + 0.1 * n
        p.add(on - 0.01, m1(0xD0 | ch, 20 + 5 * n), "pressure before the Note On")
        p.add(on, m1(0x90 | ch, k, 40 + 14 * n), "member Note On")
        t = on + 0.01
        while t < off:
            ph = (t - on) * (4.5 + 0.3 * n)
            pres = int(20 + 100 * min(1.0, (t - on) / 1.2) * (0.75 + 0.25 * math.sin(ph * 0.7)))
            p.add(t, m1(0xD0 | ch, max(0, min(127, pres))))
            bend = 8192 + int(300 * math.sin(2 * math.pi * ph)) if t > on + 0.6 else 8192
            p.add(t + 0.003, m1(0xE0 | ch, bend & 0x7f, bend >> 7))
            p.add(t + 0.006, m1(0xB0 | ch, 74, int(64 + 60 * math.sin(ph * 0.5))))
            t += 0.023
        p.add(off, m1(0x80 | ch, k, 30 + 10 * n), "member Note Off, release velocity")
    # a re-strike while held (the same key on its channel), a velocity-0 Note On as a Note Off
    p.add(4.8, m1(0x92, 62, 90), "Note On")
    p.add(5.0, m1(0x92, 62, 110), "the same key again while held")
    p.add(5.4, m1(0x92, 62, 0), "velocity 0: a Note Off")
    # a big member glide: a bend to the range's end and back
    p.add(5.6, m1(0x93, 57, 100))
    for i in range(60):
        b = 8192 + int(8191 * math.sin(math.pi * i / 59))
        p.add(5.65 + 0.01 * i, m1(0xE3, b & 0x7f, b >> 7))
    p.add(6.4, m1(0x83, 57, 64))
    # the master channel: notes with poly pressure and the mod wheel (y), the instrument's bend,
    # the pedal (CC64) while notes are released
    for n, k in enumerate([50, 57, 62, 69]):
        on = 6.8 + 0.05 * n
        p.add(on, m1(0x90, k, 70 + 10 * n), "master Note On")
        for i in range(40):
            p.add(on + 0.02 + 0.03 * i, m1(0xA0, k, int(30 + 90 * abs(math.sin(i * 0.15 + n)))), "poly pressure" if i == 0 else "")
    for i in range(50):
        p.add(6.9 + 0.025 * i, m1(0xB0, 1, int(127 * i / 49)), "mod wheel" if i == 0 else "")
        b = 8192 + int(4000 * math.sin(i * 0.25))
        p.add(6.91 + 0.025 * i, m1(0xE0, b & 0x7f, b >> 7), "the instrument's bend" if i == 0 else "")
    p.add(8.0, m1(0xB0, 64, 127), "pedal down")
    for n, k in enumerate([50, 57, 62, 69]):
        p.add(8.2 + 0.1 * n, m1(0x80, k, 20), "master Note Off")
    p.add(9.5, m1(0xB0, 64, 0), "pedal up")
    p.add(9.5, m1(0xE0, 0, 64), "the instrument's bend back to centre")
    # a keyboard without aftertouch: the velocity is the pressure; short repeated strikes
    for i in range(8):
        p.add(9.8 + 0.12 * i, m1(0x94, 60 + 2 * i, 20 + 14 * i))
        p.add(9.85 + 0.12 * i, m1(0x84, 60 + 2 * i, 0))
    return p


# --- MIDI 2.0 ---------------------------------------------------------------------------------------

def m2(op, ch, b2, b3, data):
    return [0x40000000 | (op << 20) | (ch << 16) | ((b2 & 0x7f) << 8) | (b3 & 0xff), data & 0xffffffff]


def u32(x):
    return max(0, min(0xffffffff, int(round(x * 4294967295.0))))


def bip(x):  # bipolar 32: 0x80000000 is 0
    x = max(-1.0, min(1.0, x))
    return int(round(0x80000000 + (x * 2147483648.0 if x < 0 else x * 2147483647.0)))


def midi2():
    p = Phrase("MIDI 2.0 (UMP type 4): per-note pitch and timbre, poly pressure, 16-bit velocities")
    # the zone: Registered Controller 0/6 (MCM) on channel 1, 15 members; bend range 0/0 = 12 semitones
    p.add(0.0, m2(0x2, 0, 0, 6, 15 << 25), "MCM, 15 member channels")
    p.add(0.0, m2(0x2, 1, 0, 0, 12 << 25), "bend range 12 semitones")
    # member notes: velocity, channel pressure swells, channel bend vibrato, per-note timbre
    keys = [45, 52, 57, 61, 64]
    for n, k in enumerate(keys):
        ch = 1 + n
        on = 0.15 + 0.2 * n
        off = 3.8 + 0.08 * n
        p.add(on, m2(0x9, ch, k, 0, (0x5000 + 0x1800 * n) << 16), "Note On, 16-bit velocity")
        t = on + 0.008
        i = 0
        while t < off:
            ph = (t - on) * (5.0 + 0.4 * n)
            p.add(t, m2(0xD, ch, 0, 0, u32(0.15 + 0.8 * min(1.0, (t - on) / 1.5) * (0.8 + 0.2 * math.sin(ph)))), "channel pressure" if i == 0 else "")
            if t > on + 0.5:
                p.add(t + 0.002, m2(0xE, ch, 0, 0, bip(0.02 * math.sin(2 * math.pi * ph))), "channel bend" if i == 0 else "")
            p.add(t + 0.004, m2(0x0, ch, k, 74, bip(0.8 * math.sin(ph * 0.4))), "per-note timbre (Registered PNC 74)" if i == 0 else "")
            t += 0.021
            i += 1
        p.add(off, m2(0x8, ch, k, 0, (0x2000 + 0x2000 * n) << 16), "Note Off, release velocity")
    # per-note pitch: two notes on one channel, each gliding by its own Pitch 7.25 (absolute)
    p.add(4.4, m2(0x9, 7, 60, 0, 0x9000 << 16), "two notes on one channel")
    p.add(4.45, m2(0x9, 7, 67, 0, 0x7000 << 16))
    for i in range(80):
        t = 4.5 + 0.0125 * i
        p.add(t, m2(0x0, 7, 60, 3, int((60 + 2.0 * i / 79) * (1 << 25))), "Pitch 7.25 of each" if i == 0 else "")
        p.add(t + 0.001, m2(0x0, 7, 67, 3, int((67 - 1.5 * i / 79 + 0.003 * math.sin(i)) * (1 << 25))))
        p.add(t + 0.002, m2(0xA, 7, 60, 0, u32(0.3 + 0.6 * i / 79)), "poly pressure of each" if i == 0 else "")
        p.add(t + 0.003, m2(0xA, 7, 67, 0, u32(0.9 - 0.5 * i / 79)))
    p.add(5.7, m2(0xB, 7, 74, 0, u32(0.9)), "CC74 on the channel: the notes have their own timbre (unmoved)")
    p.add(5.9, m2(0x8, 7, 60, 0, 0x8000 << 16))
    p.add(6.0, m2(0x8, 7, 67, 0, 0))
    # the master channel: the instrument's bend, CC1 and CC74 (y), CC64 (the pedal), a held chord
    for n, k in enumerate([48, 55, 64]):
        p.add(6.3 + 0.03 * n, m2(0x9, 0, k, 0, (0x6000 + 0x2000 * n) << 16), "master Note On")
    for i in range(60):
        t = 6.4 + 0.02 * i
        p.add(t, m2(0xB, 0, 1, 0, u32(i / 59)), "CC1 (the mod wheel)" if i == 0 else "")
        p.add(t + 0.005, m2(0xE, 0, 0, 0, bip(0.5 * math.sin(i * 0.2))), "the instrument's bend (x 2 semitones)" if i == 0 else "")
        p.add(t + 0.01, m2(0xA, 0, 55, 0, u32(0.4 + 0.5 * abs(math.sin(i * 0.1)))), "poly pressure" if i == 0 else "")
    p.add(7.7, m2(0xB, 0, 64, 0, u32(1.0)), "the pedal down")
    for n, k in enumerate([48, 55, 64]):
        p.add(7.9 + 0.1 * n, m2(0x8, 0, k, 0, 0x3000 << 16))
    p.add(8.9, m2(0xB, 0, 64, 0, 0), "the pedal up")
    p.add(8.9, m2(0xE, 0, 0, 0, 0x80000000), "the bend centred")
    p.add(9.0, m2(0xB, 0, 74, 0, u32(0.25)), "CC74 on the master channel")
    # a bend range change mid-note (members): the held note's pitch follows
    p.add(9.2, m2(0x9, 3, 62, 0, 0xc000 << 16))
    p.add(9.3, m2(0xE, 3, 0, 0, bip(0.5)), "half the bend range up")
    p.add(9.6, m2(0x2, 3, 0, 0, (24 << 25) | (50 << 18)), "bend range 24.5 semitones")
    p.add(9.7, m2(0xE, 3, 0, 0, bip(-0.25)))
    p.add(10.2, m2(0x8, 3, 62, 0, 0xffff << 16))
    # quick strikes of every velocity range
    for i in range(10):
        p.add(10.4 + 0.1 * i, m2(0x9, 4, 70 + i, 0, (0x1000 + 0x1900 * i) << 16))
        p.add(10.45 + 0.1 * i, m2(0x8, 4, 70 + i, 0, (0x1900 * i) << 16))
    return p


def bip_r(x, r):  # bipolar 32 with a range
    return bip(x / r)


def u32_r(x, r):
    return u32(x / r)


def sensor():
    p = Phrase("MIDI 2.0 from a controller's own sensors: measured rates and timbres, a sensor range of 4")
    R = 4.0
    p.add(0.0, m2(0x3, 0, 0, 1, int(R * (1 << 28))), "sensor range 4 (unsigned 4.28)")
    code = lambda x: round(x * 8191) / 8191  # (a 14-bit sensor's values)
    keys = [(0.1, 2.4, 50.0), (0.4, 2.0, 57.0), (0.7, 3.2, 62.25), (1.6, 2.6, 66.5), (2.9, 1.2, 70.0)]
    for n, (t0, dur, pitch) in enumerate(keys):
        key = n + 3  # (a note number per key: the key's own, not its pitch)
        p.add(t0, m2(0x9, 0, key, 0, 0x8000 << 16), "a key down (its velocity a nominal 0.5)")
        prev = 0.0
        t = t0
        i = 0
        while t < t0 + dur + 0.06:
            u = t - t0
            pr = 0.0 if u > dur else min(1.0, u / 0.015) * (0.5 + 0.5 * n / 4 + 0.35 * math.sin(2 * math.pi * 0.7 * u))
            pr = code(max(0.0, min(1.5, pr)))
            rate = code(max(-4.0, min(4.0, (pr - prev) / 0.002 * 0.0125)))
            if pr <= 0.0:
                rate = min(rate, 0.0)
            y = code(0.9 * math.sin(2 * math.pi * 0.4 * u + n))
            bend = 0.5 * (0.6 * math.sin(2 * math.pi * 5.0 * u)) ** 3 if u > 0.5 else 0.0
            p.add(t, m2(0x1, 0, key, 1, bip_r(rate, R)), "measured pressure rate" if i == 0 else "")
            p.add(t, m2(0xa, 0, key, 0, u32_r(pr, R)), "pressure (up to 1.5)" if i == 0 else "")
            p.add(t, m2(0x1, 0, key, 2, bip_r(y, R)), "timbre: a tilt" if i == 0 else "")
            p.add(t, m2(0x0, 0, key, 3, int(round((pitch + bend) * (1 << 25)))), "pitch with a slide's bend" if i == 0 else "")
            prev = pr
            t += 0.002
            i += 1
        p.add(t0 + dur + 0.06, m2(0x8, 0, key, 0, 0), "the key up")
    for i in range(120):
        t = 0.5 + 0.02 * i
        s1 = math.sin(2 * math.pi * 3.0 * t)
        p.add(t, m2(0x3, 0, 0, 0x10, u32(min(1.0, (1.0 + 1.5 * s1 * s1) / 4.0))), "accel_g (u32 x 4)" if i == 0 else "")
        for a in range(3):
            p.add(t, m2(0x3, 0, 0, 0x14 + a, bip(0.6 * math.sin(2 * math.pi * (0.9 + 0.4 * a) * t + a))), "spin" if i == 0 and a == 0 else "")
    p.add(2.2, m2(0xb, 0, 64, 0, u32(1.0)), "the pedal down")
    p.add(3.6, m2(0xb, 0, 64, 0, 0), "the pedal up")
    return p


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    mpe().write("mpe.ump")
    midi2().write("midi2.ump")
    sensor().write("sensor.ump")
