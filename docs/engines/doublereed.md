<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# doublereed

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**physical double reeds: reed, conical bore and tone holes, a living player (oboe, cor anglais, bassoon)**

Header `dsp/include/awva/voice_doublereed.h`, traits `dsp/include/awva/engines/doublereed.h`, component id `com.sorbet-ai.awva.doublereed`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | oboe | `reed_open`, `p_close`, `lattice_hz`, `p_loud`, `wander`, `tongue`, `emb_tilt`, `release` |
| 1 | cor anglais | `reed_open`, `p_close`, `lattice_hz`, `p_loud`, `wander`, `tongue`, `emb_tilt`, `release` |
| 2 | bassoon | `reed_open`, `p_close`, `lattice_hz`, `p_loud`, `wander`, `tongue`, `emb_tilt`, `release` |

After `init()` the engine plays preset 0 (oboe).

## Hero parameters

`reed_open`, `p_close`, `lattice_hz`, `p_loud`, `wander`, `tongue`, `emb_tilt`, `release`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0.05 | 100 | 1.722 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.721312 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 0.536147 | continuous |
| `14a92b36` | `pres_hz` | level | 2 | 200 | 63.634 | continuous |
| `1036ae7e` | `release` | level | 0.005 | 1 | 0.05 | continuous |
| `c7d91eff` | `rel_pedal` | level | 0 | 3 | 0.4 | continuous |
| `f4ada648` | `p_soft` | breath | 0.5 | 3 | 1.33259 | continuous |
| `ac9cdaa8` | `p_loud` | breath | 0.1 | 3 | 0.163817 | continuous |
| `45481f5a` | `p_reg` | breath | -1 | 1 | 0.333613 | continuous |
| `b3a3844b` | `p_curve` | breath | 0.3 | 3 | 2.53337 | continuous |
| `e79a6f74` | `reed_hz` | reed | 200 | 8000 | 2731.34 | continuous |
| `06277eb0` | `reed_reg` | reed | -1 | 1.5 | 1.24277 | continuous |
| `47cc72a1` | `reed_q` | reed | 0.3 | 30 | 0.452105 | continuous |
| `afd734f2` | `reed_open` | reed | 0.05 | 2.5 | 0.172683 | continuous |
| `29073838` | `open_reg` | reed | -1 | 1.5 | 0.947729 | continuous |
| `49cc75c7` | `reed_w` | reed | 2 | 25 | 5.4026 | continuous |
| `396ad694` | `p_close` | reed | 1 | 40 | 14.0655 | continuous |
| `4303b923` | `contact` | reed | 0 | 50 | 1.3225 | continuous |
| `ca324b15` | `duct` | reed | 0.05 | 5 | 4.00981 | continuous |
| `4113cde7` | `emb_tilt` | reed | -1 | 1 | 0.25 | continuous |
| `71143aa8` | `emb_soft` | reed | 0.1 | 1 | 0.343511 | continuous |
| `b4624bcf` | `bite` | reed | -100 | 100 | 15 | continuous |
| `02049853` | `jitter` | reed | 0 | 40 | 9.56209 | continuous |
| `f23b7114` | `wander` | player | 0 | 0.3 | 0.048645 | continuous |
| `f1f80bc2` | `lip_wander` | player | 0 | 0.3 | 0.023202 | continuous |
| `72049c93` | `wander_hz` | player | 0.3 | 30 | 7.37563 | continuous |
| `f9f50cf7` | `ref_note` | bore | 24 | 96 | 69 | continuous |
| `c6cd0dc1` | `apex` | bore | 0.5 | 80 | 16.6843 | continuous |
| `99d79043` | `cavity` | bore | 0 | 4 | 0.021383 | continuous |
| `63cb2800` | `bore_d` | bore | 1.5 | 15 | 2.00927 | continuous |
| `5b83d2e2` | `bore_decay` | bore | 0.003 | 0.3 | 0.095786 | continuous |
| `d2924a86` | `bore_reg` | bore | -1 | 2 | 1.38611 | continuous |
| `b17df278` | `lattice_hz` | holes | 100 | 6000 | 4891.7 | continuous |
| `9dc9d62d` | `lattice_q` | holes | 0.4 | 4 | 0.422051 | continuous |
| `b38aa92c` | `lattice_reg` | holes | -1 | 1.5 | -0.498894 | continuous |
| `17979d07` | `rad_hz` | radiation | 10 | 3000 | 62.3155 | continuous |
| `53c95844` | `tone_hz` | radiation | 300 | 16000 | 4015.93 | continuous |
| `0cca7651` | `tone_q` | radiation | 0.5 | 3 | 0.665699 | continuous |
| `235d9fc7` | `breath` | air | 0 | 1 | 0.054957 | continuous |
| `4aac31a8` | `hiss` | air | 0 | 0.5 | 0 | continuous |
| `09ceaf46` | `t_noise` | air | 0 | 1 | 0.000821 | continuous |
| `25a7329d` | `tongue` | attack | 0 | 2 | 1.21443 | continuous |
| `96f13303` | `att_rate` | attack | 0 | 2 | 1.21443 | continuous |
| `972d507d` | `att_noise` | attack | 0 | 2 | 1.21443 | continuous |
| `6f1a0e5f` | `acc_level` | attack | 0 | 1.5 | 1.45496 | continuous |
| `6f9d5d57` | `acc_decay` | attack | 0.005 | 0.3 | 0.037843 | continuous |
| `fd47c156` | `tongue_t` | attack | 0.0005 | 0.05 | 0.0008 | continuous |
| `6db50acc` | `tongue_shut` | attack | 0 | 1.2 | 0.49849 | continuous |
| `72d5f1ed` | `slur` | attack | 0 | 1 | 0.15 | continuous |
| `af32797a` | `pitch_t` | attack | 0.002 | 0.5 | 0.013862 | continuous |
| `175c54d9` | `flutter` | mod | 0 | 1 | 0.6 | continuous |
| `e6f3c1b1` | `flut_hz` | mod | 8 | 40 | 24 | continuous |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 90.63 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2.5 s of the last release |
| hot (the engine object) | 3.22656 KB (3304 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 40 KB |

<!-- awva_docs: end -->

## How it works

(to be written)
