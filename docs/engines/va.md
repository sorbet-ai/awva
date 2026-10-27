<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# va

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**virtual analog: saw/pulse/sub oscillators, sync, ladder-style filter, envelopes (8 presets)**

Header `dsp/include/awva/voice_va.h`, traits `dsp/include/awva/engines/va.h`, component id `com.sorbet-ai.awva.va`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |
| spin | `motion.spin[3]`, the angular rate about each axis, -1 .. 1 | a host's gyroscope (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | classic | `cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato` |
| 1 | pad | `cut_base`, `cut_env`, `release`, `saw2_detune`, `drift`, `res`, `sub`, `vibrato` |
| 2 | lead | `cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato` |
| 3 | bass | `cut_base`, `res`, `cut_env`, `drive`, `sub`, `acc_level`, `acc_cutoff`, `release` |
| 4 | brass | `cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato` |
| 5 | keys | `cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato` |
| 6 | acid | `cut_base`, `res`, `cut_env`, `drive`, `acc_cutoff`, `acc_level`, `acc_decay`, `release` |
| 7 | strings | `cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato` |

After `init()` the engine plays preset 0 (classic).

## Hero parameters

`cut_base`, `res`, `cut_env`, `drive`, `saw2_detune`, `sub`, `release`, `vibrato`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/va.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0.5 | 5 | 2.3 | continuous | The overall loudness of the sound. Range 0.5 to 5. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.3 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 0.7 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 1 | 200 | 70 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 1 to 200 Hz. |
| `35115ebd` | `hold_decay` | Fall while held | `decay+hold` | level | 0.005 | 1 | 0.045 | continuous | The fastest the level can fall while the key stays down, so a quick lift still leaves a tail. Range 0.005 to 1 s. |
| `1036ae7e` | `release` | Release | `release` | release | 0.02 | 4 | 0.1 | continuous | How long the sound takes to die away after the key is released. Range 0.02 to 4 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | release | 0 | 6 | 2.9 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 6 s. |
| `fd9c824c` | `rel_speed` | Release by lift speed | `release+lift` | release | 0 | 8 | 3 | continuous | How much a quick lift-off shortens the tail: 0 no effect, higher cuts it short. Range 0 to 8. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | accent | 0 | 2.5 | 0.75 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 2.5. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | accent | 0.005 | 0.4 | 0.04 | continuous | How long the accent lasts before the note settles. Range 0.005 to 0.4 s. |
| `28968fa6` | `acc_key` | Accent by register | `accent+register` | accent | 0 | 0.06 | 0.0277778 | continuous | How much longer the accent lasts on low notes than on high ones. Range 0 to 0.06. |
| `3fa8c62a` | `acc_cutoff` | Accent cutoff | `accent+bright` | accent | 0 | 48 | 28 | continuous | How far the filter opens at a strike, in semitones. Range 0 to 48 semitones. |
| `eb5316a5` | `acc_drive` | Accent drive | `accent+amount` | accent | 0 | 2 | 0.7 | continuous | The extra saturation at a strike. Range 0 to 2. |
| `cabfdbb8` | `acc_sync` | Accent sync | `accent+rate` | accent | 0 | 24 | 10 | continuous | The extra sync sweep at a strike, in semitones, when timbre is up. Range 0 to 24 semitones. |
| `e3b71f2b` | `acc_pitch` | Accent pitch | `accent+pitch` | accent | -1 | 1 | 0 | continuous | A pitch offset at the strike that fades with the accent: below 0 the note scoops up, above 0 it drops in. Range -1 to 1 semitones. |
| `e9d7f461` | `cut_base` | Cutoff | `filter` | filter | -20 | 80 | 26 | continuous | Where the filter sits when closed, as a note number. Range -20 to 80 note. |
| `bc358333` | `cut_key` | Cutoff tracking | `filter+register` | filter | 0 | 1.2 | 0.75 | continuous | How much the cutoff follows the note: 1 keeps the tone the same up the keyboard. Range 0 to 1.2. |
| `c6bbb493` | `cut_env` | Cutoff by pressure | `filter+press` | filter | 0 | 80 | 46 | continuous | How far pressure opens the filter. Range 0 to 80 semitones. |
| `c4005aff` | `cut_back` | Cutoff, timbre down | `filter+back` | filter | -24 | 24 | 9 | continuous | How far the filter closes with timbre down. Range -24 to 24 semitones. |
| `4556ec55` | `cut_fwd` | Cutoff, timbre up | `filter+fwd` | filter | -24 | 24 | 5 | continuous | How far the filter opens with timbre up. Range -24 to 24 semitones. |
| `2ff46619` | `res` | Resonance | `resonance` | filter | 0 | 0.98 | 0.2 | continuous | The filter's resonance: near 0.98 it rings on its own. Range 0 to 0.98. |
| `b16cea51` | `res_back` | Resonance, timbre down | `resonance+back` | filter | -0.5 | 0.98 | 0.72 | continuous | How much timbre down adds to the resonance. Range -0.5 to 0.98. |
| `c1379e2f` | `res_fwd` | Resonance, timbre up | `resonance+fwd` | filter | -0.5 | 0.98 | 0.08 | continuous | How much timbre up takes from the resonance. Range -0.5 to 0.98. |
| `b0b643fc` | `bass_comp` | Bass compensation | `filter+low` | filter | 0 | 1 | 0.45 | continuous | How much the bass is brought back as the resonance rises. Range 0 to 1. |
| `1faee677` | `drive` | Drive | `drive` | filter | 0.1 | 3 | 0.6 | continuous | The saturation after the filter. Range 0.1 to 3. |
| `96732c27` | `drive_env` | Drive by pressure | `drive+press` | filter | 0 | 3 | 0.9 | continuous | How much more saturation pressure adds. Range 0 to 3. |
| `d71c946a` | `saw` | Saw | `saw` | osc | 0 | 1 | 0.5 | continuous | The level of the main sawtooth. Range 0 to 1. |
| `ed652ba8` | `saw_back` | Saw, timbre down | `saw+back` | osc | 0 | 1 | 0.75 | continuous | How much timbre down reduces the saw. Range 0 to 1. |
| `6d08a810` | `saw_fwd` | Saw, timbre up | `saw+fwd` | osc | 0 | 1 | 0.65 | continuous | How much timbre up reduces the saw. Range 0 to 1. |
| `f9fd8688` | `saw2` | Second saw | `saw+pitch` | osc | 0 | 1 | 0.36 | continuous | The level of the detuned second sawtooth. Range 0 to 1. |
| `35cc2d12` | `saw2_back` | Second saw, timbre down | `saw+back` | osc | 0 | 1 | 0.5 | continuous | How much timbre down reduces the second saw. Range 0 to 1. |
| `47ee967a` | `saw2_fwd` | Second saw, timbre up | `saw+fwd` | osc | 0 | 1 | 0.6 | continuous | How much timbre up reduces the second saw. Range 0 to 1. |
| `44a94f5c` | `saw2_detune` | Second saw detune | `detune` | osc | -0.5 | 0.5 | 0.07 | continuous | How far the second saw is detuned. Range -0.5 to 0.5 semitones. |
| `550bfe9e` | `pulse` | Pulse | `pulse` | osc | 0 | 1 | 0 | continuous | The level of the pulse wave. Range 0 to 1. |
| `77d0445c` | `pulse_back` | Pulse, timbre down | `pulse+back` | osc | 0 | 1 | 0.45 | continuous | How much timbre down adds pulse. Range 0 to 1. |
| `4e6df23f` | `pwm` | Pulse width sweep | `pulse+depth` | osc | 0 | 0.4 | 0.13 | continuous | How far the pulse width sweeps. Range 0 to 0.4. |
| `9751ed86` | `pwm_hz` | Pulse width rate | `pulse+rate` | osc | 0.05 | 8 | 0.37 | continuous | How fast the pulse width sweeps. Range 0.05 to 8 Hz. |
| `dc4e3915` | `sub` | Sub | `sub` | osc | 0 | 1 | 0.5 | continuous | The level of the sub-oscillator an octave down. Range 0 to 1. |
| `cca841a3` | `sub_base` | Sub at rest | `sub+amount` | osc | 0 | 1 | 0.2 | continuous | How much of the sub sounds with the timbre at rest. Range 0 to 1. |
| `ba8115dd` | `sub_back` | Sub, timbre down | `sub+back` | osc | 0 | 1 | 0.25 | continuous | How much timbre down adds sub. Range 0 to 1. |
| `9001726b` | `sub_fwd` | Sub, timbre up | `sub+fwd` | osc | 0 | 1 | 0.6 | continuous | How much timbre up takes from the sub. Range 0 to 1. |
| `383a392d` | `b_sync` | B synced | `sync` | osc | 0 | 1 | 1 | toggle | Whether the second oscillator is hard-synced to the first (on) or runs free and detuned (off). Range off or on. |
| `4837952c` | `b_level` | B level | `sync+level` | osc | 0 | 1 | 0 | continuous | The level of the second oscillator. Range 0 to 1. |
| `c1c58dfb` | `b_fwd` | B, timbre up | `sync+fwd` | osc | 0 | 1 | 0.5 | continuous | How much timbre up adds of the second oscillator. Range 0 to 1. |
| `ff6d6c5f` | `b_detune` | B detune | `sync+pitch` | osc | -0.5 | 0.5 | 0 | continuous | How far down the second oscillator is detuned when it runs free. Range -0.5 to 0.5 semitones. |
| `097acf9a` | `sync` | Sync ratio | `sync+rate` | osc | 0 | 24 | 5 | continuous | How far above the note the synced oscillator runs, with timbre up. Range 0 to 24 semitones. |
| `5e2c9e80` | `sync_fwd` | Sync, timbre up | `sync+fwd` | osc | 0 | 24 | 12 | continuous | How much further timbre up sweeps the sync. Range 0 to 24 semitones. |
| `def28d62` | `sync_env` | Sync by pressure | `sync+press` | osc | 0 | 24 | 9 | continuous | How far pressure sweeps the sync. Range 0 to 24 semitones. |
| `2ee0e898` | `tilt_home` | Timbre home | `timbre` | osc | -1 | 1 | 0 | continuous | Where the timbre sits while the timbre control is centred: below 0 down, above 0 up. Range -1 to 1. |
| `2e87eea4` | `drift` | Pitch drift | `drift` | mod | 0 | 0.3 | 0.03 | continuous | A slow wander of the main oscillator's pitch. Range 0 to 0.3 semitones. |
| `55c89874` | `saw2_drift` | Second saw drift | `drift+pitch` | mod | 0 | 0.3 | 0.025 | continuous | A slow wander of the second saw's pitch. Range 0 to 0.3 semitones. |
| `a5b424ef` | `cut_drift` | Cutoff drift | `drift+bright` | mod | 0 | 2 | 0.3 | continuous | A slow wander of the filter cutoff. Range 0 to 2 semitones. |
| `f15d1b51` | `gyro_chorus` | Chorus by rotation | `spin+pitch` | mod | 0 | 1 | 0.22 | continuous | How much rotation (the motion input) detunes the second saw. Range 0 to 1. |
| `e4d88d72` | `vibrato` | Vibrato by shake | `vibrato+shake` | mod | 0 | 1.5 | 0.35 | continuous | How much vibrato a shake (the motion input) adds, in semitones at a full shake. Range 0 to 1.5 semitones. |
| `9e2c50e9` | `vib_hz` | Vibrato rate | `vibrato+rate` | mod | 1 | 9 | 5.3 | continuous | How fast the vibrato wavers. Range 1 to 9 Hz. |
| `e7c742df` | `life` | Life | `life` | mod | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |

## Cost and memory

| | |
|---|---|
| voices | up to 16 |
| channels | 1 (mono) |
| CPU | 75 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 15 s of the last release |
| hot (the engine object) | 4.10938 KB (4208 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | none |

<!-- awva_docs: end -->

## How it works

An expressive virtual-analog voice.

- **Oscillators:** A is a band-limited saw, a pulse (PWM) and a square sub an octave down, all from one phase. B is a saw hard-synced to A (the ratio swept by `timbre` above 0, pressure and strike), or in some presets a free-running saw detuned downwards (a three-saw ensemble). C is a free-running saw a few cents off (chorus, widened by spin). Every discontinuity (saw wraps, pulse edges, sub flips, sync resets) gets a polyBLEP correction; the output is delayed one sample so sync resets are corrected exactly.
- **Filter:** a 4-pole zero-delay-feedback (TPT) ladder. The feedback is solved linearly and then soft-clipped, so loud notes squash the resonance as an analog ladder does. Every stage is a passive one-pole and the feedback is bounded, so it is stable for any control values.
- **Output:** a soft saturation after the filter (driven harder by pressure and strikes), then the level.
- **Modulation:** slow random pitch and cutoff drift per voice, and vibrato from shaking.
- **Envelope:** the level follows pressure (smoothed); a strike adds an accent that decays in about 0.16 s; after the release the level and the cutoff decay together.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the level and, through the accent and the drive, the brightness.
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): an accent: louder, brighter, more sync sweep.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the sync sweep and the filter (per preset).
- **acceleration** (`motion.accel_g`, from a host's motion sensor): vibrato. **spin** (`motion.spin`, from a host's gyroscope): the chorus width.
- **sustain** (`sustain`, CC64): the release length; a fast release shortens it.
