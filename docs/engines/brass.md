<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# brass

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**brass: a lip source and a physical bore, bell, mutes (9 presets)**

Header `dsp/include/awva/voice_brass.h`, traits `dsp/include/awva/engines/brass.h`, component id `com.sorbet-ai.awva.brass`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pres`, 0 released .. 1 a firm press (up to ~1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| strike | `vpres`, the pressure's velocity: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| tilt y | `but_y`, forward/back tilt -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |
| pedal | `pedal`, 0 .. 1 (3/16 at rest) | CC64 |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | trumpet | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 1 | flugelhorn | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 2 | trombone | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 3 | horn | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 4 | euphonium | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 5 | tuba | `brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair` |
| 6 | harmon | `mute`, `mute_bp`, `mute_hz`, `wah`, `brassiness`, `tone_hz`, `vibrato`, `release` |
| 7 | plunger | `mute`, `mute_lp`, `mute_hz`, `wah`, `brassiness`, `tone_hz`, `vibrato`, `release` |
| 8 | section | `pair`, `detune`, `brassiness`, `lip_loud`, `tone_hz`, `breath`, `vibrato`, `release` |

After `init()` the engine plays preset 0 (trumpet).

## Hero parameters

`brassiness`, `lip_loud`, `tone_hz`, `breath`, `mute`, `release`, `vibrato`, `pair`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0.1 | 6 | 0.328 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.3 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 0.7 | continuous |
| `14a92b36` | `pres_hz` | level | 2 | 200 | 40 | continuous |
| `1036ae7e` | `release` | level | 0.005 | 1 | 0.03 | continuous |
| `c7d91eff` | `rel_pedal` | level | 0 | 3 | 0.4 | continuous |
| `aa7e184b` | `lip_soft` | lips | -2 | 0.8 | -1.5 | continuous |
| `c6217e1b` | `lip_loud` | lips | -2 | 0.85 | 0.65 | continuous |
| `e03cb8d0` | `lip_tilt` | lips | -0.5 | 0.5 | 0.15 | continuous |
| `02049853` | `jitter` | lips | 0 | 20 | 3 | continuous |
| `235d9fc7` | `breath` | lips | 0 | 0.8 | 0.08 | continuous |
| `239b9a3e` | `brassiness` | bore | 0 | 60 | 16 | continuous |
| `5b83d2e2` | `bore_decay` | bore | 0.005 | 0.2 | 0.025 | continuous |
| `48d7d837` | `bell_hz` | bore | 150 | 4000 | 1400 | continuous |
| `17979d07` | `rad_hz` | body | 20 | 2000 | 350 | continuous |
| `d6db0cb6` | `cup_hz` | body | 150 | 3000 | 1200 | continuous |
| `9be8e613` | `cup_q` | body | 0.5 | 5 | 1.4 | continuous |
| `df277df3` | `cup_gain` | body | 0 | 4 | 1.5 | continuous |
| `53c95844` | `tone_hz` | body | 500 | 12000 | 5500 | continuous |
| `0cca7651` | `tone_q` | body | 0.5 | 2 | 0.8 | continuous |
| `4d442335` | `tone_dyn` | body | 0 | 4 | 1.5 | continuous |
| `a5324c77` | `tilt_tone` | body | -2 | 2 | 0.6 | continuous |
| `d6069f54` | `mute` | mute | 0 | 1 | 0 | continuous |
| `58cbd8d3` | `mute_hz` | mute | 300 | 5000 | 1700 | continuous |
| `9f982c60` | `mute_q` | mute | 0.5 | 10 | 4 | continuous |
| `62cbe891` | `mute_hp` | mute | 0 | 3 | 0.3 | continuous |
| `5ebcd7bb` | `mute_bp` | mute | 0 | 4 | 2.5 | continuous |
| `5ad66955` | `mute_lp` | mute | 0 | 2 | 0 | continuous |
| `2de2e627` | `wah` | mute | -2 | 2 | 0 | continuous |
| `25a7329d` | `tongue` | attack | 0 | 2 | 1 | continuous |
| `6f1a0e5f` | `acc_level` | attack | 0 | 1.5 | 0.35 | continuous |
| `6f9d5d57` | `acc_decay` | attack | 0.01 | 0.3 | 0.05 | continuous |
| `0ffdea03` | `acc_bright` | attack | 0 | 1.5 | 0.5 | continuous |
| `bb6347c3` | `scoop` | attack | 0 | 1.5 | 0.25 | continuous |
| `71ca3e3e` | `blat` | attack | 0 | 1.5 | 0.25 | continuous |
| `09ceaf46` | `t_noise` | attack | 0 | 1 | 0.15 | continuous |
| `72d5f1ed` | `slur` | attack | 0 | 1 | 0.25 | continuous |
| `e4d88d72` | `vibrato` | mod | 0 | 1 | 0 | continuous |
| `9e2c50e9` | `vib_hz` | mod | 2 | 9 | 5.5 | continuous |
| `175c54d9` | `flutter` | mod | 0 | 1 | 0.6 | continuous |
| `e6f3c1b1` | `flut_hz` | mod | 8 | 40 | 24 | continuous |
| `7c445ab1` | `pair` | section | 0 | 1 | 0 | continuous |
| `bb5fdd2c` | `detune` | section | 0 | 30 | 8 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 30000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 2 s of the last release |
| engine object | 8.02344 KB (8216 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | 80 KB |

<!-- awva_docs: end -->

## How it works

A hybrid: a lip source sets the pitch, and a physical bore shapes it.

- **Lips:** an outward-striking valve that opens once per period. The flow pulse is max(cos(2πφ) − c, 0)², and the closure c follows the breath: soft playing is almost sinusoidal, loud playing closes the lips for most of the cycle (a narrow, harmonic-rich pulse). The oscillator is the pitch master, so the voice stays in tune and can't jump registers. Lip life comes from a few cents of pitch jitter and breath turbulence modulated by the lip opening. A tongued attack adds a short blat: a scoop from below, irregular buzz, a brighter pulse, an overshoot and a "t".
- **Bore:** a waveguide loop one period long (mouthpiece → bell → mouthpiece). At the bell the low frequencies reflect and the rest radiates; the loop delay is the period minus the bell filter's phase delay, so its resonances sit on the harmonics. In steady state the radiated wave equals the source, but in time it behaves like a bore: the attack starts as a bright buzz while the low resonances build up, a release rings briefly, and legato changes smear.
- **Brassiness:** the wave steepens on its way to the bell like a finite-amplitude wave: the radiated wave is read at a delay that moves with the pressure, in two stages in series, with the read speed limited (no fold-back, no aliasing). High harmonics grow like powers of the amplitude, so soft is round and loud blares.
- **Body:** a radiation high-pass, the mouthpiece cup formant, and a bell/tone low-pass whose cutoff rises with the level. The mutes are one state-variable filter (straight, harmon, plunger); forward/back tilt sweeps the mute resonance or the tone.
- **Section:** an optional second player a few cents apart.

## Playing

- **pressure:** breath, dynamics and brightness together (pp mellow, ff blaring).
- **strike:** tonguing. A hard strike gives a bright blat, an accent, a slight scoop and a "t"; re-striking a held note re-tongues.
- **release:** the lips stop and the bore rings out briefly; the pedal lengthens the tail a little.
- **tilt forward/back:** open presets are brighter or darker (lips and bell; the horn's hand). In the mute presets it is a wah (harmon: forward opens; plunger: back closes).
- **shake:** flutter tongue.
