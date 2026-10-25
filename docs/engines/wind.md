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
| pressure | `pres`, 0 released .. 1 a firm press (up to ~1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| strike | `vpres`, the pressure's velocity: > 0 while the key goes down, < 0 coming up | note-on velocity (a strike that fades), note-off velocity (the release) |
| tilt y | `but_y`, forward/back tilt -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `9b99e7dd` | `level` | breath | 0.1 | 4 | 2.05 | continuous |
| `a2780a89` | `speak` | breath | 0.03 | 0.5 | 0.12 | continuous |
| `ed3ba870` | `dyn` | breath | 0.4 | 1.5 | 0.75 | continuous |
| `4595b8fd` | `attack` | breath | 0.5 | 60 | 3 | continuous |
| `b8de5976` | `dyn_gain` | breath | 0 | 1 | 0.6 | continuous |
| `25a7329d` | `tongue` | artic | 0 | 1 | 0.35 | continuous |
| `9a6d88a0` | `tongue_noise` | artic | 0 | 2 | 0.5 | continuous |
| `afd734f2` | `reed_open` | reed | 0.08 | 0.7 | 0.3 | continuous |
| `f492e4fc` | `reed_freq` | reed | 1500 | 8000 | 4000 | continuous |
| `f439540c` | `reed_damp` | reed | 0.2 | 1.5 | 0.5 | continuous |
| `e7347b1a` | `loss` | bore | 0.9 | 0.995 | 0.985 | continuous |
| `5aae6265` | `lattice` | bore | 300 | 6000 | 1100 | continuous |
| `41a065fd` | `lat_breath` | bore | 0 | 1.5 | 0.4 | continuous |
| `c6cd0dc1` | `apex` | bore | 0.15 | 0.5 | 0.5 | continuous |
| `f6d15cd6` | `apex_lp` | bore | 1.5 | 20 | 6 | continuous |
| `d7e330f4` | `jet` | bore | 0.4 | 0.6 | 0.5 | continuous |
| `841975f6` | `jet_off` | bore | 0 | 0.8 | 0.3 | continuous |
| `dbaa7975` | `body` | tone | 0 | 3 | 0.4 | continuous |
| `11979395` | `rad_hp` | tone | 20 | 2000 | 250 | continuous |
| `25094f85` | `bright` | tone | 500 | 16000 | 5000 | continuous |
| `23672d90` | `br_breath` | tone | 0 | 8 | 1.5 | continuous |
| `00978477` | `f1_hz` | tone | 150 | 5000 | 800 | continuous |
| `007b0263` | `f1_db` | tone | -12 | 15 | 0 | continuous |
| `96297594` | `f2_hz` | tone | 300 | 8000 | 2500 | continuous |
| `86477178` | `f2_db` | tone | -12 | 15 | 0 | continuous |
| `af7cc6f9` | `f_q` | tone | 0.5 | 6 | 1.5 | continuous |
| `235d9fc7` | `breath` | noise | 0 | 3 | 0.3 | continuous |
| `854bbf96` | `turb` | noise | 0 | 0.15 | 0.02 | continuous |
| `8e25e9f4` | `noise_hz` | noise | 300 | 8000 | 2000 | continuous |
| `492d7c4f` | `tilt_bright` | expr | 0 | 1 | 0.5 | continuous |
| `e98585d5` | `tilt_breath` | expr | 0 | 1 | 0.5 | continuous |
| `77bdc182` | `growl` | expr | 0 | 0.8 | 0 | continuous |
| `ef88e6bb` | `growl_from` | expr | 0.5 | 1.5 | 1.05 | continuous |
| `6a122da6` | `vib` | mod | 0 | 0.6 | 0 | continuous |
| `9e2c50e9` | `vib_hz` | mod | 2 | 8 | 5 | continuous |
| `2819a4b5` | `vib_breath` | mod | 0 | 0.3 | 0 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 20000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 2 s of the last release |
| engine object | 1.88281 KB (1928 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | 120 KB |

<!-- awva_docs: end -->

## How it works

Physically modelled woodwinds, one excitation and bore per preset.

- **Cylinder (the clarinets):** a pressure-controlled single reed on one waveguide (closed at the reed, open at the bell): odd harmonics below the tone-hole lattice cutoff, weak above it.
- **Cone (the saxophones, oboe, bassoon):** the same reed on a truncated cone, modelled as the main tube in parallel with a short open tube for the missing apex (the "cylindrical saxophone"). The apex has a fixed length per instrument, so its share of the period grows towards high notes as on the real instrument; its reflection is low-passed (3 f0), which evens out the cone's impedance peaks and keeps low notes in their register even when blown hard.
- **Jet (the flute):** an air jet deflected by the acoustic flow at the mouth hole, travelling half a period to the edge, which splits it (saturating, with an offset for the even harmonics).

The reed is a mass-spring reed with lip damping and Bernoulli flow, solved exactly per sample (Guillemain, Kergomard & Voinier 2005). The bell and tone-hole lattice are a 2-pole low-pass in the loop: above its cutoff the bore has no resonances, so the internal spectrum falls and the reed can't squeak. The output is the wave radiated through the lattice plus some internal wave, then (shared by all voices) a radiation high-pass, a brightness low-pass, two formant bands for the instrument's body, a breath-noise bus, a DC blocker and a soft limit. The tuning is the period minus the loop filters' phase delays at f0, minus a measured correction table per preset (`voice_wind_tune.h`).

## Playing

- **pressure:** the breath. Below the speaking pressure there is only a soft breath; then the tone blooms, louder and brighter.
- **strike:** tonguing: a fast strike gives a crisp attack (a pressure kick and a "t"); a slow press a breathy swell.
- **tilt forward/back:** the embouchure, subtly: forward brighter and edgier, back darker and breathier (the saxes' subtone, the flute's covered tone).
- **shake:** the saxophone presets growl (so do they when pushed past about 1.05); the other presets have no growl.
