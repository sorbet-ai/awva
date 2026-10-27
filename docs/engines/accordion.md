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
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/accordion.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | Level | `level` | register | 0 | 4 | 0.95 | continuous | The overall loudness of the sound. Range 0 to 4. |
| `0238396e` | `r16` | 16' reeds | `reed-bank+low` | register | 0 | 1.5 | 0 | continuous | The level of the 16' (bassoon) reed bank: 0 off. Range 0 to 1.5. |
| `15540bd7` | `r8` | 8' reeds | `reed-bank` | register | 0 | 1.5 | 1 | continuous | The level of the 8' (clarinet) reed bank: 0 off. Range 0 to 1.5. |
| `ab239d4c` | `r8_sharp` | 8' sharp reeds | `reed-bank+pitch` | register | 0 | 1.5 | 0 | continuous | The level of the 8' reeds tuned sharp (violin, musette): 0 off. Range 0 to 1.5. |
| `980f8757` | `r8_flat` | 8' flat reeds | `reed-bank+pitch` | register | 0 | 1.5 | 0 | continuous | The level of the 8' reeds tuned flat (musette): 0 off. Range 0 to 1.5. |
| `0953f8f3` | `r4` | 4' reeds | `reed-bank+high` | register | 0 | 1.5 | 0 | continuous | The level of the 4' (piccolo) reed bank: 0 off. Range 0 to 1.5. |
| `905255c5` | `beat` | Musette beat | `beat` | musette | 0 | 12 | 1.3 | continuous | How fast the musette reeds beat against each other on A4. Range 0 to 12 Hz. |
| `b4227ac9` | `beat_flat` | Flat reed beat | `beat+low` | musette | 0 | 2 | 1 | continuous | How fast the flat reed beats, relative to the sharp one. Range 0 to 2. |
| `46712a5d` | `beat_track` | Beat across the range | `beat+register` | musette | 0 | 1 | 0 | continuous | Whether the beat stays the same in Hz (0) or in cents (1) across the range. Range 0 to 1. |
| `fed1d04d` | `cassotto` | Cassotto | `chamber` | register | 0 | 1 | 0 | toggle | Whether the 16' and first 8' reeds sound through the tone chamber: on or off. Range off or on. |
| `6fecad4f` | `bellows` | Shared bellows | `bellows` | bellows | 0 | 1 | 0.5 | continuous | Whether each note has its own air (0) or every note shares one bellows (1). Range 0 to 1. |
| `99380614` | `curve` | Bellows curve | `dynamics` | bellows | 0.3 | 1.5 | 0.6 | continuous | The shape of the curve from key pressure to bellows pressure: below 1 normal playing sits at mezzo-forte, above 1 it stays soft until you push. Range 0.3 to 1.5. |
| `15429315` | `bel_ms` | Bellows response | `response` | bellows | 5 | 300 | 40 | continuous | How quickly the bellows pressure follows the keys. Range 5 to 300 ms. |
| `626144c1` | `valve` | Pallet opening | `pallet+open` | bellows | 0.05 | 1 | 0.25 | continuous | The key pressure that opens the key's valve fully. Range 0.05 to 1. |
| `378f32dd` | `throttle` | Pallet throttle | `pallet+amount` | bellows | 0.05 | 1.5 | 0.35 | continuous | How much a half-open valve chokes the reed. Range 0.05 to 1.5. |
| `ac435b12` | `chamber_ms` | Pallet build-up | `pallet+time` | bellows | 1 | 100 | 6 | continuous | How long the pressure takes to build behind an opening valve. Range 1 to 100 ms. |
| `db99896b` | `release_ms` | Pallet closing | `pallet+time` | bellows | 3 | 200 | 18 | continuous | How long the key's valve takes to close when the key is released. Range 3 to 200 ms. |
| `17359e2a` | `speak_ms` | Speaking time | `reed+time` | reed | 5 | 300 | 22 | continuous | How long a reed takes to speak at a medium touch. Range 5 to 300 ms. |
| `30eb4bcb` | `low_slow` | Slow low reeds | `reed+register` | reed | 0 | 1 | 0.35 | continuous | How much slower the low reeds speak than the high ones. Range 0 to 1. |
| `99dfc797` | `thresh` | Reed threshold | `breath+soft` | reed | 0.005 | 0.3 | 0.025 | continuous | The pressure where the reed starts to sound. Range 0.005 to 0.3. |
| `1d279a19` | `amp` | Reed swing | `reed+amount` | reed | 0.5 | 5 | 3.3 | continuous | How far the reed tongue swings at a full press. Range 0.5 to 5. |
| `eabd3d97` | `rest` | Reed rest | `reed+open` | reed | 0 | 2 | 0.9 | continuous | How high the reed tongue rests above the plate. Range 0 to 2. |
| `876fffdd` | `push` | Reed push | `reed+press` | reed | 0 | 1.2 | 0.3 | continuous | How far the tongue is pushed into the slot at a full press. Range 0 to 1.2. |
| `481721cf` | `gap` | Reed gap | `reed+size` | reed | 0.02 | 0.6 | 0.12 | continuous | The reed's clearance gap: rounds off the cut-off of the air. Range 0.02 to 0.6. |
| `cded1a85` | `exit` | Reed exit | `reed+low` | reed | 0.05 | 1.5 | 0.6 | continuous | How the air leaves below the plate as the tongue swings through, tip first. Range 0.05 to 1.5. |
| `3db9b915` | `down` | Reed downstroke | `reed+low` | reed | 0 | 2.5 | 1.5 | continuous | How open the reed is below the plate relative to above. Range 0 to 2.5. |
| `c2c78296` | `constrict` | Reed constriction | `reed+q` | reed | 0.3 | 4 | 1.6 | continuous | The narrowing where the air flow saturates. Range 0.3 to 4. |
| `932f6b6b` | `pump` | Reed pumping | `reed+rate` | reed | 0 | 1.5 | 0.5 | continuous | How much air the tongue itself pumps. Range 0 to 1.5. |
| `ceeac39a` | `flat` | Pitch drop | `reed+pitch` | reed | 0 | 30 | 3 | continuous | How far the pitch drops at a full press. Range 0 to 30 cents. |
| `8e72a64a` | `bend` | Pallet bend | `pallet+pitch` | reed | 0 | 100 | 30 | continuous | How far the pitch bends down at full bellows on a barely open valve. Range 0 to 100 cents. |
| `494a00ce` | `spread` | Reed tolerance | `detune` | reed | 0 | 3 | 0.5 | continuous | How far the reeds are allowed to be out of tune with each other. Range 0 to 3 cents. |
| `ba80a8ea` | `voicing` | Voicing | `keytrack+level` | reed | 0 | 2 | 1.1 | continuous | How the level changes across the compass: above 0 the low notes are louder. Range 0 to 2. |
| `8c752c11` | `ch_hz` | Reed chamber frequency | `resonance+rate` | tone | 500 | 5000 | 2500 | continuous | Where the reed chamber's resonance sits. Range 500 to 5000 Hz. |
| `7c6a8585` | `ch_db` | Reed chamber peak | `resonance+level` | tone | 0 | 20 | 13 | continuous | How strong the reed chamber's resonance is. Range 0 to 20 dB. |
| `91023d36` | `ch_q` | Reed chamber sharpness | `resonance+q` | tone | 0.4 | 5 | 1.4 | continuous | How narrow the reed chamber's resonance is. Range 0.4 to 5. |
| `3d011411` | `cas_hz` | Cassotto tone | `chamber+bright` | tone | 400 | 4000 | 1600 | continuous | How dark the tone chamber makes its reeds: the frequency above which it rolls off. Range 400 to 4000 Hz. |
| `fe151220` | `cas_leak` | Cassotto leak | `chamber+amount` | tone | 0 | 1 | 0.45 | continuous | How much of the highs still pass the tone chamber. Range 0 to 1. |
| `fbc40a41` | `grille_hz` | Grille | `tone` | tone | 2000 | 12000 | 7500 | continuous | The brightness of the grille and body: the frequency above which they roll off. Range 2000 to 12000 Hz. |
| `603a6206` | `tilt` | Voicing by timbre | `timbre` | expr | 0 | 1 | 0.3 | continuous | How much timbre changes the reeds' voicing. Range 0 to 1. |
| `c947e9a1` | `rev_pedal` | Reverse on pedal | `bellows+pedal` | expr | 0 | 1 | 1 | toggle | Whether pressing the pedal reverses the bellows: on or off. Range off or on. |
| `c2642da3` | `rev_shake` | Reverse on shake | `bellows+shake` | expr | 0 | 1 | 1 | toggle | Whether a shake (the motion input) reverses the bellows: on or off. Range off or on. |
| `94e2d6a7` | `rev_ms` | Reversal gap | `bellows+time` | expr | 20 | 300 | 80 | continuous | How long the gap in the sound lasts when the bellows reverse. Range 20 to 300 ms. |
| `e7c742df` | `life` | Life | `life` | expr | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 100 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 1 s of the last release |
| hot (the engine object) | 3.54688 KB (3632 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | none |

<!-- awva_docs: end -->

## How it works

Physically modelled free reeds, with registers, musette tuning, reed chambers, a cassotto and a shared bellows.

- **The free reed** (Millot & Baumann 2007; Ricot, Causse & Misdariis 2005; Cottingham 2011): a tongue vibrating through a slot in a plate, blown closed. Its pitch is its own (no air column): a lightly damped, nearly sinusoidal oscillator, slightly flatter at high pressure. The sound comes from the air it lets through, through a section that is open above the plate, shut while the tongue is inside the slot (except a clearance gap that rounds the corners), and open again below it. Small amplitudes stay above the slot (soft is dark); larger ones cross it and cut the flow into pulses (loud is bright). The radiated sound is the flow's derivative (a monopole), summed per reed bank.
- **The amplitude** follows the self-excited oscillator's amplitude equation per block: growth above the threshold pressure (slower for big low reeds), decay below it, and a lower sustain threshold than onset threshold (the free reed's inverse bifurcation). The phase is a phasor at the reed's frequency, so the tuning is exact, minus the pressure flattening and the pallet bend.
- **Chambers:** one resonance per reed bank, the optional cassotto (a second chamber: darker, smoother), and the grille/body.
- **Bellows:** each note opens its pallet, and the pressure behind the pallets mixes each note's own pressure (its per-note pressure) with a shared bellows pressure (the strongest note, smoothed): 0 is per-note expression, 1 a real accordion. A bellows reversal makes the pressure dip through zero, and the other reed of each pair (with its own tuning error) speaks.
- **Musette:** the second 8' reed is sharp and the third flat by `beat` Hz at A4, either the same beat rate across the range (as tuners do) or constant cents.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the bellows and pallet: below the threshold only air; then the reeds speak, louder, brighter and a little flatter. With `bellows` up, the strongest note sets the pressure for the whole chord.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the reed voicing: above 0 a little brighter, below 0 softer.
- **sustain** (`sustain`, CC64): a bellows reversal. **acceleration** (`motion.accel_g`, from a host's motion sensor): a bellows shake.
- `pressure_rate` isn't read: the attack follows the pressure rise itself.
