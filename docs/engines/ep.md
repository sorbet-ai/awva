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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/ep.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `e4c35bdc` | `hard` | Hammer hardness | `mallet` | hammer | 0.3 | 3 | 1 | continuous | How hard the hammer tip is: harder is brighter with more bell and tick. Range 0.3 to 3. |
| `139e3fda` | `vsens` | Strike sensitivity | `strike` | hammer | 0.5 | 4 | 1.4 | continuous | How much the strike speed counts: higher turns a modest hit into a hard one. Range 0.5 to 4. |
| `ed3ba870` | `dyn` | Dynamic range | `dynamics` | hammer | 0 | 1 | 0.8 | continuous | How much louder a hard strike is than a soft one: 0 flat, 1 the full range. Range 0 to 1. |
| `d315e55d` | `decay` | Decay | `decay` | tine | 0.2 | 30 | 7 | continuous | How long the tine rings, measured on middle C. Range 0.2 to 30 s. |
| `7e18316a` | `dkey` | Decay by register | `decay+register` | tine | 0 | 1.5 | 0.65 | continuous | How much shorter the high notes ring than the low ones. Range 0 to 1.5. |
| `dfe6493b` | `prompt` | Prompt decay | `decay+early` | tine | 0 | 1 | 0.35 | continuous | How much the note drops in its first moment before the long sustain: 0 none, 1 a strong initial fall. Range 0 to 1. |
| `826e1638` | `bell` | Bell | `overtone` | tine | 0 | 2 | 0.09 | continuous | The level of the tine's bell-like second mode. Range 0 to 2. |
| `6d384024` | `bratio` | Bell pitch | `overtone+pitch` | tine | 2 | 16 | 6.9 | continuous | Where the bell mode sits, as a multiple of the fundamental. Range 2 to 16. |
| `ab7b17fd` | `bdecay` | Bell decay | `overtone+decay` | tine | 0.05 | 5 | 0.9 | continuous | How long the bell mode rings, measured on middle C. Range 0.05 to 5 s. |
| `5b5630a4` | `tick` | Tick | `overtone+strike` | tine | 0 | 1.5 | 0.08 | continuous | The level of the short attack tick. Range 0 to 1.5. |
| `bc9a9346` | `tratio` | Tick pitch | `overtone+rate` | tine | 6 | 30 | 19 | continuous | Where the tick sits, as a multiple of the fundamental. Range 6 to 30. |
| `905255c5` | `beat` | Tonebar beat | `beat` | tonebar | 0 | 0.5 | 0.12 | continuous | The level of the tonebar's partner mode, which blooms and wavers against the tine. Range 0 to 0.5. |
| `70b6c994` | `bcents` | Tonebar detune | `beat+pitch` | tonebar | 0 | 25 | 1.2 | continuous | How far the tonebar partner is detuned: the speed of the bloom. Range 0 to 25 cents. |
| `602c63de` | `align` | Pickup alignment | `pickup` | pickup | 0.3 | 1.6 | 0.95 | continuous | Where the tine sits over the pickup: about 0.58 is sweet, above 0.8 barks, below 0.5 sounds hollow. Range 0.3 to 1.6. |
| `603a6206` | `tilt` | Alignment by timbre | `pickup+timbre` | pickup | 0 | 0.6 | 0.3 | continuous | How far timbre moves the tine over the pickup. Range 0 to 0.6. |
| `1faee677` | `drive` | Pickup drive | `pickup+amount` | pickup | 0.2 | 4 | 1.1 | continuous | How far the tine swings into the pickup: more bark at the same strike. Range 0.2 to 4. |
| `d66293ab` | `dig` | Dig | `pickup+press` | pickup | 0 | 1 | 0.4 | continuous | How much pressing into a held key drives the pickup harder. Range 0 to 1. |
| `4d93b82a` | `wurli` | Pickup type | `pickup+timbre` | pickup | 0 | 1 | 0 | continuous | The pickup's character: 0 a magnetic Rhodes, 1 an electrostatic Wurlitzer. Range 0 to 1. |
| `a0f39ce0` | `damper` | Damper | `damper` | damper | 0 | 3 | 1 | continuous | How firmly the damper stops the note on release: 0 no dampers. Range 0 to 3. |
| `1b5426fe` | `gain` | Level | `level` | amp | 0 | 8 | 2.27 | continuous | The overall loudness of the sound. Range 0 to 8. |
| `bf8733c1` | `grit` | Amp grit | `drive` | amp | 0 | 1 | 0.15 | continuous | How hard the amp is driven. Range 0 to 1. |
| `5c714e81` | `tone` | Amp tone | `tone` | amp | 800 | 16000 | 7000 | continuous | The amp and speaker's brightness: the frequency above which they roll off. Range 800 to 16000 Hz. |
| `ccddd3e7` | `bodyhz` | Body EQ frequency | `resonance+rate` | amp | 60 | 4000 | 250 | continuous | Where the body EQ band sits. Range 60 to 4000 Hz. |
| `dbaa7975` | `body` | Body EQ | `resonance+level` | amp | -0.9 | 2 | 0.25 | continuous | How much the body EQ band is cut (below 0) or boosted. Range -0.9 to 2. |
| `cc8acdd0` | `wobble` | Wobble | `drift` | mod | 0 | 40 | 0 | continuous | A tape-like wow and flutter of the pitch. Range 0 to 40 cents. |
| `e7c742df` | `life` | Life | `life` | mod | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 39.0625 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 3 s of the last release |
| hot (the engine object) | 2.68359 KB (2748 bytes) (on a 64-bit host) |
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

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): the velocity: soft is warm and round, hard is bright and barking.
- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure) while holding: digging in adds a little pickup drive (bark).
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the pickup alignment: below 0 sweeter, above 0 barkier.
- **release** (the Note Off) and **sustain** (`sustain`, CC64): the damper; `sustain` is a sustain pedal (fully lifted at 0.8). Easing a held note's pressure a little and striking again re-strikes.
