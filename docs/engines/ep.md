<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# ep

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**electric pianos: modal tines and reeds, pickups, damper (14 presets)**

Header `dsp/include/awva/voice_ep.h`, traits `dsp/include/awva/engines/ep.h`, component id `com.sorbet-ai.awva.ep`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | stage | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 1 | suitcase | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 2 | mk2 | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 3 | dyno | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 4 | ballad | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 5 | bark | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 6 | neosoul | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 7 | fm80s | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 8 | lofi | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 9 | wurli | `wurli`, `hard`, `decay`, `drive`, `grit`, `tone`, `wobble`, `dyn` |
| 10 | wurlibark | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 11 | pianet | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 12 | felt | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |
| 13 | celesta | `hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble` |

After `init()` the engine plays preset 0 (stage).

## Hero parameters

`hard`, `decay`, `bell`, `tick`, `drive`, `grit`, `tone`, `wobble`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `e4c35bdc` | `hard` | hammer | 0.3 | 3 | 1 | continuous |
| `139e3fda` | `vsens` | hammer | 0.5 | 4 | 1.4 | continuous |
| `ed3ba870` | `dyn` | hammer | 0 | 1 | 0.8 | continuous |
| `d315e55d` | `decay` | tine | 0.2 | 30 | 7 | continuous |
| `7e18316a` | `dkey` | tine | 0 | 1.5 | 0.65 | continuous |
| `dfe6493b` | `prompt` | tine | 0 | 1 | 0.35 | continuous |
| `826e1638` | `bell` | tine | 0 | 2 | 0.09 | continuous |
| `6d384024` | `bratio` | tine | 2 | 16 | 6.9 | continuous |
| `ab7b17fd` | `bdecay` | tine | 0.05 | 5 | 0.9 | continuous |
| `5b5630a4` | `tick` | tine | 0 | 1.5 | 0.08 | continuous |
| `bc9a9346` | `tratio` | tine | 6 | 30 | 19 | continuous |
| `905255c5` | `beat` | tonebar | 0 | 0.5 | 0.12 | continuous |
| `70b6c994` | `bcents` | tonebar | 0 | 25 | 1.2 | continuous |
| `602c63de` | `align` | pickup | 0.3 | 1.6 | 0.95 | continuous |
| `603a6206` | `tilt` | pickup | 0 | 0.6 | 0.3 | continuous |
| `1faee677` | `drive` | pickup | 0.2 | 4 | 1.1 | continuous |
| `d66293ab` | `dig` | pickup | 0 | 1 | 0.4 | continuous |
| `4d93b82a` | `wurli` | pickup | 0 | 1 | 0 | continuous |
| `a0f39ce0` | `damper` | damper | 0 | 3 | 1 | continuous |
| `1b5426fe` | `gain` | amp | 0 | 8 | 2.27 | continuous |
| `bf8733c1` | `grit` | amp | 0 | 1 | 0.15 | continuous |
| `5c714e81` | `tone` | amp | 800 | 16000 | 7000 | continuous |
| `ccddd3e7` | `bodyhz` | amp | 60 | 4000 | 250 | continuous |
| `dbaa7975` | `body` | amp | -0.9 | 2 | 0.25 | continuous |
| `cc8acdd0` | `wobble` | mod | 0 | 40 | 0 | continuous |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 39.0625 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 3 s of the last release |
| hot (the engine object) | 2.28906 KB (2344 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | none |

<!-- awva_docs: end -->

## How it works

A physically inspired electric piano: the Rhodes first, the Wurlitzer and others as presets.

- **Tine:** four damped modal resonators (coupled form: exact frequency, stable for any decay). They are the tine's fundamental (a fast "prompt" decay after the strike, then the long tonebar-fed sustain); its tonebar partner a cent or two away (the coupled pair's slow bloom); the second bending mode (about 7×, the "bell", kept modest); and the third (about 19×, a short attack tick).
- **Hammer:** a velocity impulse into the modes. The neoprene tip's contact time shortens with velocity and hardness, and the upper modes are weighted by the contact pulse's spectrum, so harder strikes ring brighter.
- **Pickup:** the Rhodes magnetic pickup, the flux through the coil as a function of the tine's tip position, φ(u) = 1/(1 + u²) with u = drive·x − align. Small motion stays almost linear (round, pure); a hard strike swings the tip across the pole piece and the curve's asymmetry turns it into harmonics and bark. The Wurlitzer's electrostatic pickup is a biased soft clip instead, and a parameter morphs between the two. The output is normalised by the pickup's small-signal slope, so alignment and drive change the timbre, not the soft level.
- **Damper:** on release a felt damper engages (stronger on the upper modes); the pedal lifts it.
- **Master:** body EQ, amp grit (an asymmetric soft clip), tone low-pass, DC blocker, a safety ceiling, and optional tape wobble.

## Playing

- **strike:** the velocity: soft is warm and round, hard is bright and barking.
- **pressure while holding:** digging in adds a little pickup drive (bark).
- **tilt forward/back:** the pickup alignment: back sweeter, forward barkier.
- **release and pedal:** the damper; the pedal is a sustain pedal (fully lifted at 0.8). Easing a held note's pressure a little and striking again re-strikes.
