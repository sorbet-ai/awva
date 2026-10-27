<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# reed

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**single reed: clarinet to saxophone, one model, timbre morphs the bore**

Header `dsp/include/awva/voice_reed.h`, traits `dsp/include/awva/engines/reed.h`, component id `com.sorbet-ai.awva.reed`.

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

None: the parameters are the sound.

## Hero parameters

`growl_from`, `life`

## Parameters

Default: the value after `init()`. The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `ef88e6bb` | `growl_from` | expr | 0.5 | 1.5 | 0.7 | continuous |
| `e7c742df` | `life` | expr | 0 | 1 | 0 | continuous |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 62.5 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 1.90625 KB (1952 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 160 KB |

<!-- awva_docs: end -->

## How it works

A physically modelled single-reed woodwind that morphs from clarinet to saxophone.

- **Reed:** a mass-spring reed (resonance at or above 3.4 kHz and 6 f0, with lip damping) and the Bernoulli flow through it, solved exactly per sample for the mouthpiece pressure (Guillemain, Kergomard & Voinier 2005: the explicit quadratic solution). The reed beats against the lay for part of each period when blown harder.
- **Bore:** two waveguides meeting at the mouthpiece. The main tube ends at the bell and tone holes in a 2-pole Butterworth low-pass: above its cutoff the bore has no resonances, so the reed can't squeak. A short tube stands for the missing apex of a cone, with a one-pole low-pass. With both round trips equal the pair is exactly a cylinder (clarinet, odd harmonics); with a short apex tube it behaves like a truncated cone (saxophone, all harmonics). The apex low-pass evens out the cone's impedance peaks, so the fundamental register is preferred.
- **Output:** the wave transmitted at the bell, some internal wave, a presence band whose level grows with breath, and breath noise.
- **Tuning:** the loop delay is the period minus the loop filters' phase delays at f0, minus a measured correction table (within about 5 cents up to C7 at normal pressures). The delays ramp per sample inside the block, so vibrato and timbre changes are click-free.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath. Below about 0.12 (clarinet) to 0.2 (sax) the reed doesn't speak; above it the tone blooms, louder and brighter. Pushed past about 1.05 it growls.
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing: a pressure kick and a short tongue noise.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): below 0 is the clarinet (cylindrical, hollow), above 0 the saxophone (conical, buzzier). High notes are pulled towards the middle of the morph.
- **acceleration** (`motion.accel_g`, from a host's motion sensor): growl.
