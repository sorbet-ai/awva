<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# wind

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**woodwinds: reed and jet driven bores, cylindrical and conical (9 presets)**

Header `dsp/include/awva/voice_wind.h`, traits `dsp/include/awva/engines/wind.h`, component id `com.sorbet-ai.awva.wind`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | clarinet | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 1 | bass clarinet | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 2 | soprano sax | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 3 | alto sax | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 4 | tenor sax | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 5 | baritone sax | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 6 | oboe | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 7 | bassoon | `reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue` |
| 8 | flute | `jet`, `jet_off`, `breath`, `turb`, `bright`, `attack`, `vib`, `body` |

After `init()` the engine plays preset 0 (clarinet).

## Hero parameters

`reed_open`, `attack`, `bright`, `breath`, `growl`, `vib`, `body`, `tongue`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/wind.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | Level | `level` | breath | 0.1 | 4 | 2.05 | continuous | The overall loudness of the sound. Range 0.1 to 4. |
| `a2780a89` | `speak` | Speaks at | `breath+soft` | breath | 0.03 | 0.5 | 0.12 | continuous | The pressure where the tone starts to speak. Range 0.03 to 0.5. |
| `ed3ba870` | `dyn` | Dynamics | `dynamics` | breath | 0.4 | 1.5 | 0.75 | continuous | How hard the breath drives the tone at a full press: higher is louder and rougher. Range 0.4 to 1.5. |
| `4595b8fd` | `attack` | Breath response | `response` | breath | 0.5 | 60 | 3 | continuous | How quickly the breath follows the key: higher is slower and smoother. Range 0.5 to 60 ms. |
| `b8de5976` | `dyn_gain` | Loudness by breath | `dynamics+level` | breath | 0 | 1 | 0.6 | continuous | How much the loudness follows the breath on top of the tone itself: 0 only the tone changes, 1 the level too. Range 0 to 1. |
| `25a7329d` | `tongue` | Strike | `strike` | artic | 0 | 1 | 0.35 | continuous | How much a fast strike kicks the breath at the start of the note. Range 0 to 1. |
| `9a6d88a0` | `tongue_noise` | Tongue noise | `tongue` | artic | 0 | 2 | 0.5 | continuous | How much "t" a hard strike adds at the start of the note. Range 0 to 2. |
| `afd734f2` | `reed_open` | Reed opening | `reed+open` | reed | 0.08 | 0.7 | 0.3 | continuous | How open the reed rests: looser is louder and freer. Range 0.08 to 0.7. |
| `f492e4fc` | `reed_freq` | Reed resonance | `reed+rate` | reed | 1500 | 8000 | 4000 | continuous | Where the reed's own resonance sits. Range 1500 to 8000 Hz. |
| `f439540c` | `reed_damp` | Reed damping | `reed+q` | reed | 0.2 | 1.5 | 0.5 | continuous | How much the lips damp the reed. Range 0.2 to 1.5. |
| `e7347b1a` | `loss` | Bore loss | `bore+decay` | bore | 0.9 | 0.995 | 0.985 | continuous | How much of the wave the tube reflects: lower is more damped. Range 0.9 to 0.995. |
| `5aae6265` | `lattice` | Tone-hole cutoff | `tone-holes` | bore | 300 | 6000 | 1100 | continuous | The frequency above which the open tone holes stop reflecting: the wind tone's brightness. Range 300 to 6000 Hz. |
| `41a065fd` | `lat_breath` | Cutoff by breath | `tone-holes+press` | bore | 0 | 1.5 | 0.4 | continuous | How many octaves brighter the tone-hole cutoff gets with pressure. Range 0 to 1.5 octaves. |
| `c6cd0dc1` | `apex` | Cone apex | `bore+size` | bore | 0.15 | 0.5 | 0.5 | continuous | The cone's apex as a share of the period at the lowest note. Range 0.15 to 0.5. |
| `f6d15cd6` | `apex_lp` | Apex tone | `bore+bright` | bore | 1.5 | 20 | 6 | continuous | How dark the cone's apex reflection is, in multiples of the fundamental. Range 1.5 to 20. |
| `d7e330f4` | `jet` | Jet timing | `jet+rate` | bore | 0.4 | 0.6 | 0.5 | continuous | The flute jet's travel time as a fraction of the period. Range 0.4 to 0.6. |
| `841975f6` | `jet_off` | Jet offset | `jet+amount` | bore | 0 | 0.8 | 0.3 | continuous | How far the flute jet aims off the edge: adds even harmonics. Range 0 to 0.8. |
| `dbaa7975` | `body` | Body | `body` | tone | 0 | 3 | 0.4 | continuous | How much of the wave inside the tube is mixed into the radiated sound. Range 0 to 3. |
| `11979395` | `rad_hp` | Radiation low cut | `radiate+low` | tone | 20 | 2000 | 250 | continuous | The frequency below which the instrument radiates less. Range 20 to 2000 Hz. |
| `25094f85` | `bright` | Brightness | `tone` | tone | 500 | 16000 | 5000 | continuous | The brightness of the tone: the frequency above which it rolls off. Range 500 to 16000 Hz. |
| `23672d90` | `br_breath` | Brightness by breath | `tone+press` | tone | 0 | 8 | 1.5 | continuous | How much brighter the tone gets with pressure. Range 0 to 8. |
| `00978477` | `f1_hz` | Formant 1 frequency | `resonance+rate` | tone | 150 | 5000 | 800 | continuous | Where the first formant sits. Range 150 to 5000 Hz. |
| `007b0263` | `f1_db` | Formant 1 level | `resonance+level` | tone | -12 | 15 | 0 | continuous | How much the first formant is boosted or cut. Range -12 to 15 dB. |
| `96297594` | `f2_hz` | Formant 2 frequency | `resonance+rate` | tone | 300 | 8000 | 2500 | continuous | Where the second formant sits. Range 300 to 8000 Hz. |
| `86477178` | `f2_db` | Formant 2 level | `resonance+level` | tone | -12 | 15 | 0 | continuous | How much the second formant is boosted or cut. Range -12 to 15 dB. |
| `af7cc6f9` | `f_q` | Formant sharpness | `resonance+q` | tone | 0.5 | 6 | 1.5 | continuous | How narrow the formants are. Range 0.5 to 6. |
| `235d9fc7` | `breath` | Breath noise | `air` | noise | 0 | 3 | 0.3 | continuous | The audible breath in the tone. Range 0 to 3. |
| `854bbf96` | `turb` | Turbulence | `air+noise` | noise | 0 | 0.15 | 0.02 | continuous | The turbulence in the air stream. Range 0 to 0.15. |
| `8e25e9f4` | `noise_hz` | Breath noise centre | `air+rate` | noise | 300 | 8000 | 2000 | continuous | Where the breath noise sits. Range 300 to 8000 Hz. |
| `492d7c4f` | `tilt_bright` | Brightness by timbre | `tone+timbre` | expr | 0 | 1 | 0.5 | continuous | How much timbre brightens the embouchure. Range 0 to 1. |
| `e98585d5` | `tilt_breath` | Breathiness by timbre | `air+back` | expr | 0 | 1 | 0.5 | continuous | How much breathiness and subtone timbre below its centre adds. Range 0 to 1. |
| `77bdc182` | `growl` | Growl | `growl` | expr | 0 | 0.8 | 0 | continuous | The depth of the throaty growl when the note is pushed hard: 0 off. Range 0 to 0.8. |
| `ef88e6bb` | `growl_from` | Growl from | `growl+press` | expr | 0.5 | 1.5 | 0.7 | continuous | The pressure where the growl sets in. Range 0.5 to 1.5. |
| `6a122da6` | `vib` | Vibrato | `vibrato` | mod | 0 | 0.6 | 0 | continuous | The depth of the automatic vibrato. Range 0 to 0.6 semitones. |
| `9e2c50e9` | `vib_hz` | Vibrato rate | `vibrato+rate` | mod | 2 | 8 | 5 | continuous | How fast the vibrato wavers. Range 2 to 8 Hz. |
| `2819a4b5` | `vib_breath` | Breath vibrato | `vibrato+level` | mod | 0 | 0.3 | 0 | continuous | How much the loudness wavers with the vibrato. Range 0 to 0.3. |
| `e7c742df` | `life` | Life | `life` | mod | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |
| `9a67fb30` | `instrument` | Instrument | `instrument` | bore | 0 | 8 | 0 | steps | Which instrument: clarinet, bass clarinet, soprano, alto, tenor or baritone sax, oboe, bassoon or flute. Range steps 0 to 8. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 62.5 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 2.875 KB (2944 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 120 KB |

<!-- awva_docs: end -->

## How it works

Physically modelled woodwinds, one excitation and bore per preset.

- **Cylinder (the clarinets):** a pressure-controlled single reed on one waveguide (closed at the reed, open at the bell): odd harmonics below the tone-hole lattice cutoff, weak above it.
- **Cone (the saxophones, oboe, bassoon):** the same reed on a truncated cone, modelled as the main tube in parallel with a short open tube for the missing apex (the "cylindrical saxophone"). The apex has a fixed length per instrument, so its share of the period grows towards high notes as on the real instrument; its reflection is low-passed (3 f0), which evens out the cone's impedance peaks and keeps low notes in their register even when blown hard.
- **Jet (the flute):** an air jet deflected by the acoustic flow at the mouth hole, travelling half a period to the edge, which splits it (saturating, with an offset for the even harmonics).

The reed is a mass-spring reed with lip damping and Bernoulli flow, solved exactly per sample (Guillemain, Kergomard & Voinier 2005). The bell and tone-hole lattice are a 2-pole low-pass in the loop: above its cutoff the bore has no resonances, so the internal spectrum falls and the reed can't squeak. The output is the wave radiated through the lattice plus some internal wave, then (shared by all voices) a radiation high-pass, a brightness low-pass, two formant bands for the instrument's body, a breath-noise bus, a DC blocker and a soft limit. The tuning is the period minus the loop filters' phase delays at f0, minus a measured correction table per preset (`voice_wind_tune.h`).

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath. Below the speaking pressure there is only a soft breath; then the tone blooms, louder and brighter.
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing: a fast strike gives a crisp attack (a pressure kick and a "t"); a slow press a breathy swell.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the embouchure, subtly: above 0 brighter and edgier, below 0 darker and breathier (the saxes' subtone, the flute's covered tone).
- **acceleration** (`motion.accel_g`, from a host's motion sensor): the saxophone presets growl (so do they when pushed past about 1.05); the other presets have no growl.
