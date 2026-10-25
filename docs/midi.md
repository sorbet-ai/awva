<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Playing with MIDI 2.0 and MPE

Every way of playing an engine ends in one place: a decoder of MIDI 2.0 Universal MIDI Packets (`dsp/include/awva/ump.h`), which writes each note's controls. MIDI 1.0 and MPE are up-converted into those packets first (`dsp/include/awva/midi_in.h`, `NoteInput`), and so is standard MIDI 2.0. The engines see only the controls below, never MIDI itself.

## The controls

| control | meaning |
|---|---|
| `note` | the pitch in semitones, fractional (69 = A4), with bends and tunings included |
| `pres` | pressure: 0 released, 1 a firm press, up to about 1.5 |
| `vpres` | the pressure's velocity: above 0 while the key goes down (the strike), below 0 while it comes up (the release) |
| `but_x`, `but_y` | the key's tilt sideways and forward/back, -1 .. 1 |
| `gate`, `strike`, `release` | derived once per block: held or not, and the velocities of the strike that opened the note and of the release that ended it |

The globals are `acc_abs`, `acc_x/y/z` (the accelerometer, in g), `rot_x/y/z` (the gyroscope) and `pedal` (0 .. 1). Each engine page lists which of them the engine reads.

## From MPE

The lower zone: member channels 2 .. 1 + members. An MCM (RPN 6 on channel 1) sets the number of members, at most 15; there are 15 at the start.

| MPE | control |
|---|---|
| note on, its key | the note; its velocity becomes a strike (`vpres`), which the decoder fades (x0.6 every 2 ms while above 0.01). Until pressure arrives, `pres` is the velocity, so a keyboard without aftertouch still sounds |
| the member channel's pitch bend × the bend range (RPN 0) | added to `note` |
| channel pressure (or poly pressure) | `pres` (a held note's pressure never drops to 0) |
| CC74 | `but_y` |
| note off and its velocity | the release (`vpres` < 0) |

## From plain MIDI 1.0

On every channel outside an MPE zone (the master channel, or all of them without a zone):

| MIDI 1.0 | control |
|---|---|
| note on / off | as in MPE |
| poly pressure (that note), channel pressure (all its notes) | `pres` |
| CC1 (the mod wheel) | `but_y`, 0 .. 1 |
| CC74 | `but_y`, 64 = 0 |
| pitch bend | the instrument's bend (±2 semitones), which every note follows |
| CC64 | `pedal` |

With every note slot held (16), a new note takes the oldest.

## From standard MIDI 2.0

The same mapping, at MIDI 2.0's resolution:

- **Velocities** are 16 bits (v / 65535).
- **Pressures and controllers** are 32 bits; full scale is the Striso's full press, `pres` = 1.0.
- **Pitch bend and CC74** are bipolar by the specification: the centre 0x80000000 is exactly 0, and each side is scaled to its own end.
- **Registered Per-Note Controller 3** (Pitch 7.25) sets a note's own absolute pitch.
- **Registered Controllers** 0 and 6 are the bend range and the MCM; CC64 is the pedal.

Where MPE can carry a value, the engines get the same float from MPE and from MIDI 2.0. Where it can't (finer velocities, pressures and bends, a note's own pitch), they follow the finer value.

## Not from MIDI

- **`but_x`, the sideways tilt:** the Striso key's own. As a pitch bend it is already in `note`; an engine that reads `but_x` itself declares it.
- **The motion sensor and the gyroscope:** the instrument writes them. The profile has numbers reserved for them (Assignable Controllers 0x10 .. 0x16), but MIDI in doesn't take them yet.
- **Per-note `but_y`:** in MPE it is per channel, so per note. A plain MIDI 2.0 host currently sets it per channel (CC74); a per-note controller for it is planned (see the component README).

## Inside the instrument

On the Striso the keys themselves travel as packets of the same profile: standard MIDI 2.0 channel voice messages (type 4, group 0, channel 0) with the Striso's scaling, so the board's values arrive exactly. The note number is the host's note id, not a pitch.

| control | message |
|---|---|
| note on / off | Note On / Note Off with an attribute (type 0x02) whose bit 0 says "a strike" |
| `note` | Assignable Per-Note Controller 1, signed 9.23 semitones |
| `pres` | Poly Pressure, `pres` × 8191 × 2^17 |
| `vpres`, `but_x`, `but_y` | Assignable Per-Note Controllers 2, 3, 4, × 8191 × 2^16, signed |
| a value the scale can't hold | Assignable Per-Note Controllers 0x11 .. 0x15: the float32's bits |
| the sound ends, the note is dropped | Per-Note Management, flags D and S |
