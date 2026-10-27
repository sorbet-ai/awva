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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/modal.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | Level | `level` | level | 0.1 | 3 | 0.87 | continuous | The overall loudness of the sound. Range 0.1 to 3. |
| `99dfc797` | `thresh` | Strike threshold | `strike+soft` | mallet | 0.005 | 0.2 | 0.03 | continuous | How fast the key must be pressed for a strike to count as a hit. Range 0.005 to 0.2. |
| `290182c1` | `span` | Strike range | `strike+range` | mallet | 0.2 | 1.5 | 0.7 | continuous | How much faster than the threshold a strike must be to count as a full-strength hit: lower is more sensitive. Range 0.2 to 1.5. |
| `e4c35bdc` | `hard` | Mallet hardness | `mallet` | mallet | 0.2 | 1.2 | 0.6 | continuous | How hard the mallet is at the softest strike. Range 0.2 to 1.2. |
| `1ff67637` | `hardvel` | Hardness by strike | `mallet+strike` | mallet | 0 | 1 | 0.4 | continuous | How much harder the mallet gets with a stronger strike. Range 0 to 1. |
| `603a6206` | `tilt` | Hardness by timbre | `mallet+timbre` | mallet | 0 | 0.5 | 0.2 | continuous | How much timbre changes the mallet's hardness at the strike. Range 0 to 0.5. |
| `d6069f54` | `mute` | Strike mute | `damper+strike` | mallet | 0 | 1 | 0.3 | continuous | How much of the ringing a full strike stops, like a hand on the bar. Range 0 to 1. |
| `0a8c6a47` | `upper` | Upper partials | `overtone` | tone | 0 | 2 | 1 | continuous | The level of all partials but the first, relative to the instrument's own. Range 0 to 2. |
| `d315e55d` | `decay` | Decay | `decay` | decay | 0.1 | 4 | 1 | continuous | How long the bar rings, as a scale on the instrument's own. Range 0.1 to 4. |
| `6677a083` | `keytrack` | Decay by register | `decay+register` | decay | 0 | 1.5 | 0.8 | continuous | How much shorter the high notes ring than the low ones. Range 0 to 1.5. |
| `73432219` | `hfdamp` | Upper damping | `decay+bright` | decay | 0 | 1 | 0 | continuous | How much faster the upper partials die than the fundamental. Range 0 to 1. |
| `a0f39ce0` | `damper` | Damper | `damper` | damper | 0 | 2 | 1 | continuous | How firmly the damper stops the note on release: 0 no dampers. Range 0 to 2. |
| `f6c75a7c` | `lift` | Damper lift | `damper+press` | damper | 0.02 | 0.5 | 0.15 | continuous | The key pressure above which the damper is fully lifted. Range 0.02 to 0.5. |
| `a1b490a3` | `pedal` | Damper by pedal | `damper+pedal` | damper | 0 | 1 | 1 | continuous | How far the pedal lifts the damper: 1 fully. Range 0 to 1. |
| `758b416d` | `trem` | Motor tremolo | `tremolo` | trem | 0 | 1 | 1 | toggle | The vibraphone's motor tremolo: on or off. Range off or on. |
| `45abe62f` | `tremrate` | Tremolo speed | `tremolo+rate` | trem | 1 | 10 | 5 | continuous | How fast the motor turns. Range 1 to 10 Hz. |
| `f26aa452` | `tremdepth` | Tremolo depth | `tremolo+depth` | trem | 0 | 1 | 0.6 | continuous | How deep the tremolo pulses the loudness. Range 0 to 1. |
| `5a6356e1` | `tremthr` | Tremolo by pressure | `tremolo+press` | trem | 0.2 | 1.2 | 0.7 | continuous | The key pressure above which pressing harder adds tremolo. Range 0.2 to 1.2. |
| `1ecd3be6` | `rub` | Rub | `rub` | rub | 0 | 1 | 0 | continuous | The level of the rubbed tone per unit of pressure, like a finger on a glass: 0 off. Range 0 to 1. |
| `2f656620` | `rubnoise` | Rub noise | `rub+noise` | rub | 0 | 0.3 | 0.05 | continuous | How much of the rubbed tone is noise. Range 0 to 0.3. |
| `b08b665a` | `model` | Instrument | `instrument` | model | 0 | 5 | 0 | steps | Which instrument: vibraphone, marimba, glockenspiel, church bell, kalimba or wine glass. Range steps 0 to 5. |

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
