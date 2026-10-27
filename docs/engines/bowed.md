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
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/bowed.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0 | 4 | 1 | continuous | The overall loudness of the sound. Range 0 to 4. |
| `16ec7828` | `force_lo` | Bow force, light | `bow+soft` | bow | 0.3 | 1.6 | 0.75 | continuous | The bow's force against its speed at a light touch: low is airy, high bites. Range 0.3 to 1.6. |
| `f4e34846` | `force_hi` | Bow force, full | `bow+loud` | bow | 0.3 | 1.6 | 0.95 | continuous | The bow's force against its speed at a full press: above about 1.1 it turns raucous. Range 0.3 to 1.6. |
| `3947a3c3` | `force_over` | Bow grit | `bow+press` | bow | 0 | 2 | 1 | continuous | The grit added when pressing beyond a full press: 0 none. Range 0 to 2. |
| `d9a20dec` | `speed_curve` | Bow speed curve | `dynamics` | bow | 0 | 1 | 0.7 | continuous | How the bow speed follows pressure: 0 even, 1 the loud end opens up faster. Range 0 to 1. |
| `0028edcc` | `rosin` | Rosin | `bow+noise` | bow | 0 | 0.2 | 0.01 | continuous | The bow's grainy noise texture. Range 0 to 0.2. |
| `54336489` | `pos` | Bow position | `bow-position` | position | 0.05 | 0.2 | 0.11 | continuous | Where the bow sits on the string, as a fraction of its length from the bridge: small is bright and glassy, large is soft and dark. Range 0.05 to 0.2. |
| `ea92abc9` | `pos_tilt` | Position by timbre | `bow-position+timbre` | position | 0 | 1.5 | 1 | continuous | How far timbre moves the bow: up towards the bridge, down towards the fingerboard. Range 0 to 1.5 octaves. |
| `a2bf5ae1` | `pos_press` | Position by pressure | `bow-position+press` | position | 0 | 1 | 0.5 | continuous | How far a light bow drifts towards the fingerboard. Range 0 to 1 octaves. |
| `ddafc043` | `damp` | String ring | `decay` | string | 0.5 | 10 | 4 | continuous | How long the string rings on its own, measured on a low note. Range 0.5 to 10 s. |
| `25094f85` | `bright` | Brightness | `tone` | string | 0.3 | 3 | 1 | continuous | How sharp the string's corners are: above 1 brighter, below 1 rounder. Range 0.3 to 3. |
| `40d3c7b7` | `tone_lo` | Treble, light | `tone+soft` | string | 0 | 3 | 0.3 | continuous | The treble at a light touch. Range 0 to 3. |
| `66c9762d` | `tone_hi` | Treble, full | `tone+loud` | string | 0 | 3 | 1.2 | continuous | The treble at a full press. Range 0 to 3. |
| `35c43f5f` | `body_size` | Body size | `body+size` | body | 0.2 | 1.5 | 1 | continuous | The size of the body: 1 a violin, 0.8 a viola, 0.45 a cello, 0.3 a bass. Range 0.2 to 1.5. |
| `a306632d` | `body_track` | Body follows register | `body+register` | body | 0 | 1 | 0 | continuous | Whether the body shrinks for low notes: 0 a fixed body, 1 it follows the note. Range 0 to 1. |
| `97b7e91c` | `body_amt` | Body | `body` | body | 0 | 2 | 1 | continuous | How much of the resonant body you hear. Range 0 to 2. |
| `d5fe35bd` | `body_q` | Body sharpness | `body+q` | body | 0.3 | 3 | 1 | continuous | How sharp the body's resonances are. Range 0.3 to 3. |
| `37cc676a` | `hill` | Bridge hill | `body+bright` | body | 0 | 3 | 0.75 | continuous | The brilliance of the bridge hill, a violin body's 2 to 3 kHz glow. Range 0 to 3. |
| `5fe7049c` | `direct` | Direct string | `body+off` | body | 0 | 2 | 0.3 | continuous | How much of the string you hear without the body. Range 0 to 2. |
| `dabb186a` | `acc_force` | Accent | `accent` | accent | 0 | 3 | 0 | continuous | How much firmer the bow bites at a strike: the start's scratch and bite. Range 0 to 3. |
| `e147bd2e` | `acc_time` | Accent length | `accent+time` | accent | 0.002 | 0.2 | 0.02 | continuous | How long the accent lasts before the note settles. Range 0.002 to 0.2 s. |
| `1af6738b` | `acc_noise` | Accent noise | `accent+noise` | accent | 0 | 1 | 0.04 | continuous | The extra bow noise at a strike. Range 0 to 1. |
| `0ffdea03` | `acc_bright` | Accent brightness | `accent+bright` | accent | 0 | 2 | 0.4 | continuous | How much brighter the start of a struck note is than the sustain. Range 0 to 2. |
| `48f48d74` | `rel` | Release | `release` | release | 0.02 | 3 | 0.1 | continuous | How long the string rings after a slow release, when the bow stops on the string. Range 0.02 to 3 s. |
| `be82e912` | `rel_ped` | Release with pedal | `release+pedal` | release | 0 | 10 | 3 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 10 s. |
| `20d57cd0` | `rel_lift` | Release on a lift | `release+lift` | release | 0 | 20 | 0 | continuous | How much longer the string rings after a quick lift-off, as a multiplier on the release. Range 0 to 20. |
| `e4d88d72` | `vibrato` | Vibrato | `vibrato` | mod | 0 | 60 | 0 | continuous | The depth of the automatic vibrato that sets in after the note starts. Range 0 to 60. |
| `9e2c50e9` | `vib_hz` | Vibrato rate | `vibrato+rate` | mod | 2 | 9 | 5.5 | continuous | How fast the vibrato wavers. Range 2 to 9 Hz. |
| `a52aeefa` | `vib_delay` | Vibrato delay | `vibrato+time` | mod | 0 | 2 | 0.35 | continuous | How long after the start the vibrato reaches its full depth. Range 0 to 2 s. |
| `e7c742df` | `life` | Life | `life` | mod | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |
| `9d64fcc3` | `tremolo` | Tremolo by shake | `bow+shake` | mod | 0 | 2 | 1.3 | continuous | How much a shake (the motion input) moves the bow; a strong shake reverses it into a tremolo. Range 0 to 2. |
| `86a498df` | `body_rot` | Body by rotation | `spin` | mod | -2 | 2 | 0.7 | continuous | How far rotation (the motion input) shifts the body resonances, in octaves. Range -2 to 2 octaves. |
| `cf9f7e5a` | `ensemble` | Section | `ensemble` | section | 0 | 1.5 | 0 | continuous | The level of two more players on the note: 0 solo. Range 0 to 1.5. |
| `7b02ff91` | `ens_detune` | Section detune | `ensemble+pitch` | section | 0 | 25 | 7 | continuous | How far the extra players wander from the pitch. Range 0 to 25 cents. |
| `7c15eb4b` | `ens_spread` | Section spread | `ensemble+time` | section | 2 | 25 | 18 | continuous | How far apart in time the extra players play. Range 2 to 25 ms. |
| `092c3f46` | `pizz` | Pizzicato | `pluck` | pizz | 0 | 1 | 0 | toggle | Plucked instead of bowed: on or off. Range off or on. |
| `d88458fb` | `pizz_decay` | Pluck ring | `pluck+decay` | pizz | 0.1 | 5 | 1 | continuous | How long a plucked note rings while held, measured on middle C. Range 0.1 to 5 s. |
| `2a09f08f` | `pizz_bright` | Pluck sharpness | `pluck+bright` | pizz | 0.2 | 5 | 1 | continuous | How sharp the pluck is: higher is brighter. Range 0.2 to 5. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 100 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 3.01562 KB (3088 bytes) (on a 64-bit host) |
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

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the bow: loudness, and the bow force (a heavy bow drifts towards the bridge, a light one towards the fingerboard).
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): an accented start; in pizzicato, the pluck.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the bow position: above 0 towards the bridge (sul ponticello), below 0 towards the fingerboard (sul tasto). A dead zone keeps fast playing from drifting.
- **acceleration** (`motion.accel_g`, from a host's motion sensor): moves the bow: speed modulation, and strong shakes reverse it (tremolo).
- **spin** (`motion.spin`, from a host's gyroscope): shifts the body resonances.
- **sustain** (`sustain`, CC64): lengthens the release.
