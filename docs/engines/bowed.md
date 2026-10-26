<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# bowed

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**bowed strings: friction-driven waveguide strings, modal body, section (9 presets)**

Header `dsp/include/awva/voice_bowed.h`, traits `dsp/include/awva/engines/bowed.h`, component id `com.sorbet-ai.awva.bowed`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (3/16 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |
| spin | `motion.spin[3]`, the angular rate about each axis, -1 .. 1 | a host's gyroscope (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | violin | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 1 | viola | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 2 | cello | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 3 | bass | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 4 | section | `ensemble`, `ens_detune`, `ens_spread`, `force_hi`, `pos`, `bright`, `body_amt`, `vibrato` |
| 5 | ponticello | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 6 | flautando | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 7 | sordino | `force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble` |
| 8 | pizzicato | `pizz_decay`, `pizz_bright`, `pos`, `bright`, `body_amt`, `damp`, `rel`, `vibrato` |

After `init()` the engine plays preset 0 (violin).

## Hero parameters

`force_hi`, `pos`, `rosin`, `bright`, `body_amt`, `damp`, `vibrato`, `ensemble`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0 | 4 | 1 | continuous |
| `16ec7828` | `force_lo` | bow | 0.3 | 1.6 | 0.75 | continuous |
| `f4e34846` | `force_hi` | bow | 0.3 | 1.6 | 0.95 | continuous |
| `3947a3c3` | `force_over` | bow | 0 | 2 | 1 | continuous |
| `d9a20dec` | `speed_curve` | bow | 0 | 1 | 0.7 | continuous |
| `0028edcc` | `rosin` | bow | 0 | 0.2 | 0.01 | continuous |
| `54336489` | `pos` | position | 0.05 | 0.2 | 0.11 | continuous |
| `ea92abc9` | `pos_tilt` | position | 0 | 1.5 | 1 | continuous |
| `a2bf5ae1` | `pos_press` | position | 0 | 1 | 0.5 | continuous |
| `87024fcd` | `tilt_dead` | position | 0 | 0.8 | 0.35 | continuous |
| `ddafc043` | `damp` | string | 0.5 | 10 | 4 | continuous |
| `25094f85` | `bright` | string | 0.3 | 3 | 1 | continuous |
| `40d3c7b7` | `tone_lo` | string | 0 | 3 | 0.3 | continuous |
| `66c9762d` | `tone_hi` | string | 0 | 3 | 1.2 | continuous |
| `35c43f5f` | `body_size` | body | 0.2 | 1.5 | 1 | continuous |
| `a306632d` | `body_track` | body | 0 | 1 | 0 | continuous |
| `97b7e91c` | `body_amt` | body | 0 | 2 | 1 | continuous |
| `d5fe35bd` | `body_q` | body | 0.3 | 3 | 1 | continuous |
| `37cc676a` | `hill` | body | 0 | 3 | 0.75 | continuous |
| `5fe7049c` | `direct` | body | 0 | 2 | 0.3 | continuous |
| `dabb186a` | `acc_force` | accent | 0 | 3 | 0 | continuous |
| `e147bd2e` | `acc_time` | accent | 0.002 | 0.2 | 0.02 | continuous |
| `1af6738b` | `acc_noise` | accent | 0 | 1 | 0.04 | continuous |
| `0ffdea03` | `acc_bright` | accent | 0 | 2 | 0.4 | continuous |
| `48f48d74` | `rel` | release | 0.02 | 3 | 0.1 | continuous |
| `be82e912` | `rel_ped` | release | 0 | 10 | 3 | continuous |
| `20d57cd0` | `rel_lift` | release | 0 | 20 | 0 | continuous |
| `e4d88d72` | `vibrato` | mod | 0 | 60 | 0 | continuous |
| `9e2c50e9` | `vib_hz` | mod | 2 | 9 | 5.5 | continuous |
| `a52aeefa` | `vib_delay` | mod | 0 | 2 | 0.35 | continuous |
| `9d64fcc3` | `tremolo` | mod | 0 | 2 | 1.3 | continuous |
| `86a498df` | `body_rot` | mod | -2 | 2 | 0.7 | continuous |
| `cf9f7e5a` | `ensemble` | section | 0 | 1.5 | 0 | continuous |
| `7b02ff91` | `ens_detune` | section | 0 | 25 | 7 | continuous |
| `7c15eb4b` | `ens_spread` | section | 2 | 25 | 18 | continuous |
| `092c3f46` | `pizz` | pizz | 0 | 1 | 0 | toggle |
| `d88458fb` | `pizz_decay` | pizz | 0.1 | 5 | 1 | continuous |
| `2a09f08f` | `pizz_bright` | pizz | 0.2 | 5 | 1 | continuous |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 100 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 2.74219 KB (2808 bytes) (on a 64-bit host) |
| stream (StreamMemory) | 100 KB |
| bulk (BulkMemory) | 8 KB |

<!-- awva_docs: end -->

## How it works

A digital waveguide string driven by a friction bow, with a small modal body.

- **String:** two velocity-wave delay loops meeting at the bow. One is the neck side (bow → finger → bow), read with a 4-tap Lagrange fractional delay that carries the pitch and vibrato. The other is the bridge side, whose length is the bow position. Both ends reflect through a one-pole low-pass whose time constant is a fixed fraction of the period: corner rounding, like a finite bow width, so the timbre is the same in every register. The filters' phase delay at the fundamental is subtracted from the loop.
- **Bow:** an explicit friction junction (McIntyre, Schumacher & Woodhouse; Smith). The string sticks while the force stays below the maximum static friction, and otherwise slips on a hyperbolic friction curve of the slip speed, solved exactly (one square root), with hysteresis. Slip onsets and captures are blended at sub-sample precision. The junction is a contraction for any bow force and speed, so the loop is stable. The model is homogeneous: the timbre depends on the force/speed ratio (Schelleng), the bow position and the losses, and the speed sets the loudness. A slow correction keeps the slip periods at the note's period.
- **Onset:** a bowed note starts already in Helmholtz motion. Both delay lines are loaded with the steady sawtooth waves for the note and bow position, and the force is eased for the first two periods: a clean, pitched start.
- **Pizzicato:** no bow; a strike plucks the string with a half-sine velocity pulse at the bow position, and a release damps it.
- **Body:** a tone shelf and a parallel bank of five resonators at violin body modes scaled by the instrument's size, plus a direct path. The section adds two more players from a short, slowly modulated delay of the mix (a few cents of detune, 10-30 ms of spread).

## Playing

- **pressure:** the bow: loudness, and the bow force (a heavy bow drifts towards the bridge, a light one towards the fingerboard).
- **strike:** an accented start; in pizzicato, the pluck.
- **tilt forward/back:** the bow position: forward towards the bridge (sul ponticello), back towards the fingerboard (sul tasto). A dead zone keeps fast playing from drifting.
- **shake:** moves the bow: speed modulation, and strong shakes reverse it (tremolo).
- **rotation:** shifts the body resonances.
- **pedal:** lengthens the release.
