<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Playing with MIDI 2.0 and MPE

Every way of playing an engine ends in one place: the decoder of Universal MIDI Packets (`dsp/include/awva/ump.h`), which writes each note's controls. It reads standard MIDI 2.0 channel voice messages (UMP type 4) and MIDI 1.0 ones in UMP (type 2, MPE included), each at its own resolution; MIDI 1.0 bytes are taken as type 2. There is no other way in and no private scale. The engines see only the controls below, never MIDI itself.

## The controls

| control | meaning |
|---|---|
| `pitch` | semitones, fractional (69 = A4), with bends and tunings included |
| `pressure` | 0 released, 1 MIDI 2.0's full-scale pressure (engines take up to 1.5) |
| `pressure_rate` | how fast the pressure moves: above 0 while the note is struck, below 0 while it is released |
| `timbre` | a per-note controller, -1 .. 1 (MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74) |
| `velocity`, `release` | the Note On's and the Note Off's velocities, 0 .. 1 |
| `gate` | 1 from the Note On to the Note Off (the pressure stays above 0 while it is 1) |

These are `awva::NoteControls` (`dsp/include/awva/controls.h`).

The instrument's controls (`awva::InstrumentControls`) are `sustain` (CC64, 0 .. 1) and, from a host with a motion sensor, `motion.accel_g` (the acceleration's magnitude in g, 1 at rest) and `motion.spin[3]` (the angular rate about each axis, -1 .. 1). Each engine page lists which of them the engine reads.

## From MPE

The lower zone: member channels 2 .. 1 + members. An MCM (RPN 6 on channel 1) sets the number of members, at most 15; there are 15 at the start.

| MPE | control |
|---|---|
| note on, its key | the note; its velocity becomes a strike (`pressure_rate`), which the decoder fades (x0.6 every 2 ms while above 0.01). Until pressure arrives, `pressure` is the velocity, so a keyboard without aftertouch still sounds |
| the member channel's pitch bend × the bend range (RPN 0) | added to `pitch` |
| channel pressure (or poly pressure) | `pressure` (a held note's pressure never drops to 0) |
| CC74 | `timbre` |
| note off and its velocity | `release`, and `pressure_rate` < 0 |

## From plain MIDI 1.0

On every channel outside an MPE zone (the master channel, or all of them without a zone):

| MIDI 1.0 | control |
|---|---|
| note on / off | as in MPE |
| poly pressure (that note), channel pressure (all its notes) | `pressure` |
| CC1 (the mod wheel) | `timbre`, 0 .. 1 |
| CC74 | `timbre`, 64 = 0 |
| pitch bend | the instrument's bend (±2 semitones), which every note follows |
| CC64 | `sustain` |

With every note slot held (16), a new note takes the oldest.

## From standard MIDI 2.0

The same mapping, at MIDI 2.0's resolution:

- **Velocities** are 16 bits (v / 65535).
- **Pressures and controllers** are 32 bits; full scale is `pressure` = 1.0.
- **Pitch bend and CC74** are bipolar by the specification: the centre 0x80000000 is exactly 0, and each side is scaled to its own end.
- **Registered Per-Note Controller 3** (Pitch 7.25) sets a note's own absolute pitch.
- **Registered Per-Note Controller 74** sets a note's own `timbre`; the channel's CC74 then leaves that note alone.
- **Registered Controllers** 0 and 6 are the bend range and the MCM; CC64 is `sustain`.
- **Assignable Controllers 0x10 and 0x14 .. 0x16** are the motion: `motion.accel_g` (unsigned 32 x 4 g) and `motion.spin[0..2]` (bipolar).

Where MPE can carry a value, the engines get the same float from MPE and from MIDI 2.0. Where it can't (finer velocities, pressures and bends, a note's own pitch), they follow the finer value.

## From a controller with its own sensors

A controller that measures what the engines read sends standard MIDI 2.0 with three more controllers, and its codes arrive exactly (a 14-bit code / 8191 survives the 32-bit scales, verified for every code):

| control | message |
|---|---|
| the channel's sensor range | Assignable Controller 1, unsigned 4.28 (1.0 = 0x10000000, 1 .. 16): the full scale of the channel's pressures and sensor controllers, as MPE declares a bend range (a sensor with headroom above MIDI's full pressure sends 4: pressures up to 4.0) |
| `pressure_rate` | Assignable Per-Note Controller 1, bipolar x the sensor range: the note's rate as measured |
| `timbre` | Assignable Per-Note Controller 2, bipolar x the sensor range (or Registered Per-Note Controller 74, bipolar) |
| `pitch` | Registered Per-Note Controller 3 (Pitch 7.25: every float pitch from 0.25 to 128 exactly) |

A note that has sent its rate is a sensor's: it gets no strike, no pressure floor, and its Note Off ends its gate only (its controls stay what it sent). The note number is the note's identity (with the channel), not its pitch.
