<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# modal

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**tuned percussion: banks of damped resonators, struck or rubbed (6 presets)**

Header `dsp/include/awva/voice_modal.h`, traits `dsp/include/awva/engines/modal.h`, component id `com.sorbet-ai.awva.modal`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| spin | `motion.spin[3]`, the angular rate about each axis, -1 .. 1 | a host's gyroscope (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | vibraphone | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |
| 1 | marimba | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |
| 2 | glockenspiel | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |
| 3 | bell | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |
| 4 | kalimba | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |
| 5 | wine glass | `hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub` |

After `init()` the engine plays preset 0 (vibraphone).

## Hero parameters

`hard`, `decay`, `upper`, `keytrack`, `hfdamp`, `damper`, `tremdepth`, `rub`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | level | 0.1 | 3 | 0.87 | continuous |
| `99dfc797` | `thresh` | mallet | 0.005 | 0.2 | 0.03 | continuous |
| `290182c1` | `span` | mallet | 0.2 | 1.5 | 0.7 | continuous |
| `e4c35bdc` | `hard` | mallet | 0.2 | 1.2 | 0.6 | continuous |
| `1ff67637` | `hardvel` | mallet | 0 | 1 | 0.4 | continuous |
| `603a6206` | `tilt` | mallet | 0 | 0.5 | 0.2 | continuous |
| `d6069f54` | `mute` | mallet | 0 | 1 | 0.3 | continuous |
| `0a8c6a47` | `upper` | tone | 0 | 2 | 1 | continuous |
| `d315e55d` | `decay` | decay | 0.1 | 4 | 1 | continuous |
| `6677a083` | `keytrack` | decay | 0 | 1.5 | 0.8 | continuous |
| `73432219` | `hfdamp` | decay | 0 | 1 | 0 | continuous |
| `a0f39ce0` | `damper` | damper | 0 | 2 | 1 | continuous |
| `f6c75a7c` | `lift` | damper | 0.02 | 0.5 | 0.15 | continuous |
| `a1b490a3` | `pedal` | damper | 0 | 1 | 1 | continuous |
| `758b416d` | `trem` | trem | 0 | 1 | 1 | toggle |
| `45abe62f` | `tremrate` | trem | 1 | 10 | 5 | continuous |
| `f26aa452` | `tremdepth` | trem | 0 | 1 | 0.6 | continuous |
| `5a6356e1` | `tremthr` | trem | 0.2 | 1.2 | 0.7 | continuous |
| `1ecd3be6` | `rub` | rub | 0 | 1 | 0 | continuous |
| `2f656620` | `rubnoise` | rub | 0 | 0.3 | 0.05 | continuous |
| `b08b665a` | `model` | model | 0 | 5 | 0 | steps |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 56.25 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 5 s of the last release |
| hot (the engine object) | 5.125 KB (5248 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | none |

<!-- awva_docs: end -->

## How it works

Tuned percussion as a bank of up to 12 damped resonators per voice, one per partial of the instrument. Each is a coupled-form rotation, z[n+1] = r·e^{jω}·z[n] (plus noise when rubbed). The rotation is set by the note alone and the decay only changes r, so the partials never move except with the note (vibrato, bend), and the form is stable for any r < 1 and accurate down to the lowest notes.

The partial ratios, levels and decay times follow published measurements of the real instruments: Rossing, *Science of Percussion Instruments*; Fletcher & Rossing; church bell partials after Hibberts and Perrin. Each preset lists, per partial, its frequency ratio, its strike level, how much that level grows with mallet hardness, and its T60 at middle C; higher notes decay faster (T60 scales with f^-α).

Every voice has a NaN/energy guard, the decay changes are smoothed, and the master has a one-block look-ahead limiter.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): the loudness; a harder strike is also a little brighter (the upper partials grow with mallet hardness). Nothing changes the pitch.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74) at the strike: a harder (above 0) or softer (below 0) mallet.
- **holding and release** (the Note Off): the note rings with its natural decay; the Note Off engages the damper gradually.
- **sustain** (`sustain`, CC64): the sustain pedal (it lifts the damper).
- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the vibraphone's motor tremolo when pressed harder than a normal hold; the wine glass is rubbed by held pressure (a sustained singing tone); the other presets ignore pressure after the strike.
- **spin** (`motion.spin`, from a host's gyroscope): the vibraphone's tremolo too.
