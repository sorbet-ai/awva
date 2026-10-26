<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# Playing with MIDI 2.0 and MPE

Every way of playing an engine ends in one place: a decoder of MIDI 2.0 Universal MIDI Packets (`dsp/include/awva/ump.h`), which writes each note's controls. MIDI 1.0 and MPE are up-converted into those packets first (`dsp/include/awva/midi_in.h`, `NoteInput`), and so is standard MIDI 2.0. The engines see only the controls below, never MIDI itself.

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

Where MPE can carry a value, the engines get the same float from MPE and from MIDI 2.0. Where it can't (finer velocities, pressures and bends, a note's own pitch), they follow the finer value.

## Not from MIDI

- **The motion sensor and the gyroscope:** global inputs that a host with such sensors writes. The profile has numbers reserved for them (Assignable Controllers 0x10 .. 0x16), but MIDI in doesn't take them yet.

## awva's UMP profile

A host with its own sensors (an embedded instrument) can send its notes directly as packets of awva's profile: standard MIDI 2.0 channel voice messages (type 4, group 0, channel 0) in the engine scale, in which a 13-bit sensor code (code / 8191) arrives exactly. The note number is the host's note id, not a pitch.

| control | message |
|---|---|
| note on / off | Note On / Note Off with an attribute (type 0x02) whose bit 0 says "a strike" |
| `note` | Assignable Per-Note Controller 1, signed 9.23 semitones |
| `pres` | Poly Pressure, `pres` × 8191 × 2^17 |
| `vpres`, `but_x`, `but_y` | Assignable Per-Note Controllers 2, 3, 4, × 8191 × 2^16, signed |
| a value the scale can't hold | Assignable Per-Note Controllers 0x11 .. 0x15: the float32's bits |
| the sound ends, the note is dropped | Per-Note Management, flags D and S |
