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
| pressure | `pres`, 0 released .. 1 a firm press (up to ~1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| strike | `vpres`, the pressure's velocity: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| tilt y | `but_y`, forward/back tilt -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |
| rotation | `rot_x`, `rot_y`, `rot_z`, about -1 .. 1 | the instrument's gyroscope (not from MIDI) |
| pedal | `pedal`, 0 .. 1 (3/16 at rest) | CC64 |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0.5 | 5 | 2.3 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.3 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 0.7 | continuous |
| `14a92b36` | `pres_hz` | level | 1 | 200 | 70 | continuous |
| `35115ebd` | `hold_decay` | level | 0.005 | 1 | 0.045 | continuous |
| `1036ae7e` | `release` | release | 0.02 | 4 | 0.1 | continuous |
| `c7d91eff` | `rel_pedal` | release | 0 | 6 | 2.9 | continuous |
| `fd9c824c` | `rel_speed` | release | 0 | 8 | 3 | continuous |
| `6f1a0e5f` | `acc_level` | accent | 0 | 2.5 | 0.75 | continuous |
| `6f9d5d57` | `acc_decay` | accent | 0.005 | 0.4 | 0.04 | continuous |
| `28968fa6` | `acc_key` | accent | 0 | 0.06 | 0.0277778 | continuous |
| `3fa8c62a` | `acc_cutoff` | accent | 0 | 48 | 28 | continuous |
| `eb5316a5` | `acc_drive` | accent | 0 | 2 | 0.7 | continuous |
| `cabfdbb8` | `acc_sync` | accent | 0 | 24 | 10 | continuous |
| `e3b71f2b` | `acc_pitch` | accent | -1 | 1 | 0 | continuous |
| `e9d7f461` | `cut_base` | filter | -20 | 80 | 26 | continuous |
| `bc358333` | `cut_key` | filter | 0 | 1.2 | 0.75 | continuous |
| `c6bbb493` | `cut_env` | filter | 0 | 80 | 46 | continuous |
| `c4005aff` | `cut_back` | filter | -24 | 24 | 9 | continuous |
| `4556ec55` | `cut_fwd` | filter | -24 | 24 | 5 | continuous |
| `2ff46619` | `res` | filter | 0 | 0.98 | 0.2 | continuous |
| `b16cea51` | `res_back` | filter | -0.5 | 0.98 | 0.72 | continuous |
| `c1379e2f` | `res_fwd` | filter | -0.5 | 0.98 | 0.08 | continuous |
| `b0b643fc` | `bass_comp` | filter | 0 | 1 | 0.45 | continuous |
| `1faee677` | `drive` | filter | 0.1 | 3 | 0.6 | continuous |
| `96732c27` | `drive_env` | filter | 0 | 3 | 0.9 | continuous |
| `d71c946a` | `saw` | osc | 0 | 1 | 0.5 | continuous |
| `ed652ba8` | `saw_back` | osc | 0 | 1 | 0.75 | continuous |
| `6d08a810` | `saw_fwd` | osc | 0 | 1 | 0.65 | continuous |
| `f9fd8688` | `saw2` | osc | 0 | 1 | 0.36 | continuous |
| `35cc2d12` | `saw2_back` | osc | 0 | 1 | 0.5 | continuous |
| `47ee967a` | `saw2_fwd` | osc | 0 | 1 | 0.6 | continuous |
| `44a94f5c` | `saw2_detune` | osc | -0.5 | 0.5 | 0.07 | continuous |
| `550bfe9e` | `pulse` | osc | 0 | 1 | 0 | continuous |
| `77d0445c` | `pulse_back` | osc | 0 | 1 | 0.45 | continuous |
| `4e6df23f` | `pwm` | osc | 0 | 0.4 | 0.13 | continuous |
| `9751ed86` | `pwm_hz` | osc | 0.05 | 8 | 0.37 | continuous |
| `dc4e3915` | `sub` | osc | 0 | 1 | 0.5 | continuous |
| `cca841a3` | `sub_base` | osc | 0 | 1 | 0.2 | continuous |
| `ba8115dd` | `sub_back` | osc | 0 | 1 | 0.25 | continuous |
| `9001726b` | `sub_fwd` | osc | 0 | 1 | 0.6 | continuous |
| `383a392d` | `b_sync` | osc | 0 | 1 | 1 | toggle |
| `4837952c` | `b_level` | osc | 0 | 1 | 0 | continuous |
| `c1c58dfb` | `b_fwd` | osc | 0 | 1 | 0.5 | continuous |
| `ff6d6c5f` | `b_detune` | osc | -0.5 | 0.5 | 0 | continuous |
| `097acf9a` | `sync` | osc | 0 | 24 | 5 | continuous |
| `5e2c9e80` | `sync_fwd` | osc | 0 | 24 | 12 | continuous |
| `def28d62` | `sync_env` | osc | 0 | 24 | 9 | continuous |
| `2ee0e898` | `tilt_home` | osc | -1 | 1 | 0 | continuous |
| `2e87eea4` | `drift` | mod | 0 | 0.3 | 0.03 | continuous |
| `55c89874` | `saw2_drift` | mod | 0 | 0.3 | 0.025 | continuous |
| `a5b424ef` | `cut_drift` | mod | 0 | 2 | 0.3 | continuous |
| `f15d1b51` | `gyro_chorus` | mod | 0 | 1 | 0.22 | continuous |
| `e4d88d72` | `vibrato` | mod | 0 | 1.5 | 0.35 | continuous |
| `9e2c50e9` | `vib_hz` | mod | 1 | 9 | 5.3 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 16 |
| channels | 1 (mono) |
| CPU | 24000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 10 s of the last release |
| engine object | 3.42578 KB (3508 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | none |

<!-- awva_docs: end -->

## How it works

An expressive virtual-analog voice.

- **Oscillators:** A is a band-limited saw, a pulse (PWM) and a square sub an octave down, all from one phase. B is a saw hard-synced to A (the ratio swept by forward tilt, pressure and strike), or in some presets a free-running saw detuned downwards (a three-saw ensemble). C is a free-running saw a few cents off (chorus, widened by rotation). Every discontinuity (saw wraps, pulse edges, sub flips, sync resets) gets a polyBLEP correction; the output is delayed one sample so sync resets are corrected exactly.
- **Filter:** a 4-pole zero-delay-feedback (TPT) ladder. The feedback is solved linearly and then soft-clipped, so loud notes squash the resonance as an analog ladder does. Every stage is a passive one-pole and the feedback is bounded, so it is stable for any control values.
- **Output:** a soft saturation after the filter (driven harder by pressure and strikes), then the level.
- **Modulation:** slow random pitch and cutoff drift per voice, and vibrato from shaking.
- **Envelope:** the level follows pressure (smoothed); a strike adds an accent that decays in about 0.16 s; after the release the level and the cutoff decay together.

## Playing

- **pressure:** the level and, through the accent and the drive, the brightness.
- **strike:** an accent: louder, brighter, more sync sweep.
- **tilt forward/back:** the sync sweep and the filter (per preset).
- **shake:** vibrato. **rotation:** the chorus width.
- **pedal:** the release length; a fast release shortens it.
