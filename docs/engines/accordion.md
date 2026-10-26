<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# accordion

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**free-reed accordion: registers, musette tuning, reed chambers, cassotto, a shared bellows (9 presets)**

Header `dsp/include/awva/voice_accordion.h`, traits `dsp/include/awva/engines/accordion.h`, component id `com.sorbet-ai.awva.accordion`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pres`, 0 released .. 1 a firm press (up to ~1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| tilt y | `but_y`, forward/back tilt -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |
| pedal | `pedal`, 0 .. 1 (3/16 at rest) | CC64 |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | clarinet | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 1 | bassoon | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 2 | piccolo | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 3 | violin | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 4 | musette | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 5 | organ | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 6 | master | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 7 | bandoneon | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |
| 8 | harmonium | `beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt` |

After `init()` the engine plays preset 0 (clarinet).

## Hero parameters

`beat`, `bellows`, `curve`, `speak_ms`, `cassotto`, `ch_hz`, `bend`, `tilt`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | register | 0 | 4 | 0.95 | continuous |
| `0238396e` | `r16` | register | 0 | 1.5 | 0 | continuous |
| `15540bd7` | `r8` | register | 0 | 1.5 | 1 | continuous |
| `ab239d4c` | `r8_sharp` | register | 0 | 1.5 | 0 | continuous |
| `980f8757` | `r8_flat` | register | 0 | 1.5 | 0 | continuous |
| `0953f8f3` | `r4` | register | 0 | 1.5 | 0 | continuous |
| `905255c5` | `beat` | musette | 0 | 12 | 1.3 | continuous |
| `b4227ac9` | `beat_flat` | musette | 0 | 2 | 1 | continuous |
| `46712a5d` | `beat_track` | musette | 0 | 1 | 0 | continuous |
| `fed1d04d` | `cassotto` | register | 0 | 1 | 0 | toggle |
| `6fecad4f` | `bellows` | bellows | 0 | 1 | 0.5 | continuous |
| `99380614` | `curve` | bellows | 0.3 | 1.5 | 0.6 | continuous |
| `15429315` | `bel_ms` | bellows | 5 | 300 | 40 | continuous |
| `626144c1` | `valve` | bellows | 0.05 | 1 | 0.25 | continuous |
| `378f32dd` | `throttle` | bellows | 0.05 | 1.5 | 0.35 | continuous |
| `ac435b12` | `chamber_ms` | bellows | 1 | 100 | 6 | continuous |
| `db99896b` | `release_ms` | bellows | 3 | 200 | 18 | continuous |
| `17359e2a` | `speak_ms` | reed | 5 | 300 | 22 | continuous |
| `30eb4bcb` | `low_slow` | reed | 0 | 1 | 0.35 | continuous |
| `99dfc797` | `thresh` | reed | 0.005 | 0.3 | 0.025 | continuous |
| `1d279a19` | `amp` | reed | 0.5 | 5 | 3.3 | continuous |
| `eabd3d97` | `rest` | reed | 0 | 2 | 0.9 | continuous |
| `876fffdd` | `push` | reed | 0 | 1.2 | 0.3 | continuous |
| `481721cf` | `gap` | reed | 0.02 | 0.6 | 0.12 | continuous |
| `cded1a85` | `exit` | reed | 0.05 | 1.5 | 0.6 | continuous |
| `3db9b915` | `down` | reed | 0 | 2.5 | 1.5 | continuous |
| `c2c78296` | `constrict` | reed | 0.3 | 4 | 1.6 | continuous |
| `932f6b6b` | `pump` | reed | 0 | 1.5 | 0.5 | continuous |
| `ceeac39a` | `flat` | reed | 0 | 30 | 3 | continuous |
| `8e72a64a` | `bend` | reed | 0 | 100 | 30 | continuous |
| `494a00ce` | `spread` | reed | 0 | 3 | 0.5 | continuous |
| `ba80a8ea` | `voicing` | reed | 0 | 2 | 1.1 | continuous |
| `8c752c11` | `ch_hz` | tone | 500 | 5000 | 2500 | continuous |
| `7c6a8585` | `ch_db` | tone | 0 | 20 | 13 | continuous |
| `91023d36` | `ch_q` | tone | 0.4 | 5 | 1.4 | continuous |
| `3d011411` | `cas_hz` | tone | 400 | 4000 | 1600 | continuous |
| `fe151220` | `cas_leak` | tone | 0 | 1 | 0.45 | continuous |
| `fbc40a41` | `grille_hz` | tone | 2000 | 12000 | 7500 | continuous |
| `603a6206` | `tilt` | expr | 0 | 1 | 0.3 | continuous |
| `c947e9a1` | `rev_pedal` | expr | 0 | 1 | 1 | toggle |
| `c2642da3` | `rev_shake` | expr | 0 | 1 | 1 | toggle |
| `94e2d6a7` | `rev_ms` | expr | 20 | 300 | 80 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 32000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 1 s of the last release |
| engine object | 3.12109 KB (3196 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | none |

<!-- awva_docs: end -->

## How it works

Physically modelled free reeds, with registers, musette tuning, reed chambers, a cassotto and a shared bellows.

- **The free reed** (Millot & Baumann 2007; Ricot, Causse & Misdariis 2005; Cottingham 2011): a tongue vibrating through a slot in a plate, blown closed. Its pitch is its own (no air column): a lightly damped, nearly sinusoidal oscillator, slightly flatter at high pressure. The sound comes from the air it lets through, through a section that is open above the plate, shut while the tongue is inside the slot (except a clearance gap that rounds the corners), and open again below it. Small amplitudes stay above the slot (soft is dark); larger ones cross it and cut the flow into pulses (loud is bright). The radiated sound is the flow's derivative (a monopole), summed per reed bank.
- **The amplitude** follows the self-excited oscillator's amplitude equation per block: growth above the threshold pressure (slower for big low reeds), decay below it, and a lower sustain threshold than onset threshold (the free reed's inverse bifurcation). The phase is a phasor at the reed's frequency, so the tuning is exact, minus the pressure flattening and the pallet bend.
- **Chambers:** one resonance per reed bank, the optional cassotto (a second chamber: darker, smoother), and the grille/body.
- **Bellows:** each note opens its pallet, and the pressure behind the pallets mixes each note's own pressure (its per-note pressure) with a shared bellows pressure (the strongest note, smoothed): 0 is per-note expression, 1 a real accordion. A bellows reversal makes the pressure dip through zero, and the other reed of each pair (with its own tuning error) speaks.
- **Musette:** the second 8' reed is sharp and the third flat by `beat` Hz at A4, either the same beat rate across the range (as tuners do) or constant cents.

## Playing

- **pressure:** the bellows and pallet: below the threshold only air; then the reeds speak, louder, brighter and a little flatter. With `bellows` up, the strongest note sets the pressure for the whole chord.
- **tilt forward/back:** the reed voicing: forward a little brighter, back softer.
- **pedal:** a bellows reversal. **shake** (the instrument's motion, a global input): a bellows shake.
- The strike isn't read: the attack follows the pressure rise itself.
