<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# sine

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**the minimal example engine: one sine per voice, level from pressure**

Header `dsp/include/awva/voice_sine.h`, traits `dsp/include/awva/engines/sine.h`, component id `com.sorbet-ai.awva.sine`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

None: the parameters are the sound.

## Hero parameters

(none)

## Parameters

Default: the value after `init()`. The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| | (none) | | | | | |

## Cost and memory

| | |
|---|---|
| voices | up to 16 |
| channels | 1 (mono) |
| CPU | 5 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 1 s of the last release |
| hot (the engine object) | 676 bytes (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | none |

<!-- awva_docs: end -->

## How it works

The minimal example engine: one sine per voice whose amplitude follows pressure. It shows the engine contract and the patterns that matter on a Cortex-M7: block-rate coefficients with per-sample multiply-adds, no libm at all (polynomial cos/sin), per-voice state in locals during the sample loop, and a plain-data engine object.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the amplitude (smoothed over about 5 ms). Nothing else is read.
