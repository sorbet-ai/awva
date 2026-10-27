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
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/brass.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0.1 | 6 | 0.328 | continuous | The overall loudness of the sound. Range 0.1 to 6. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.3 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 0.7 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 2 | 200 | 40 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 2 to 200 Hz. |
| `1036ae7e` | `release` | Release | `release` | level | 0.005 | 1 | 0.03 | continuous | How long the sound takes to die away after the key is released. Range 0.005 to 1 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | level | 0 | 3 | 0.4 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 3 s. |
| `aa7e184b` | `lip_soft` | Lips, soft | `embouchure+soft` | lips | -2 | 0.8 | -1.5 | continuous | How closed the lips play when soft: -2 pure and round, towards 0.8 buzzy. Range -2 to 0.8. |
| `c6217e1b` | `lip_loud` | Lips, loud | `embouchure+loud` | lips | -2 | 0.85 | 0.65 | continuous | How closed the lips play when loud: -2 pure and round, 0.85 buzzy. Range -2 to 0.85. |
| `e03cb8d0` | `lip_tilt` | Lips by timbre | `embouchure+timbre` | lips | -0.5 | 0.5 | 0.15 | continuous | How much timbre firms the lips: up brightens. Range -0.5 to 0.5. |
| `02049853` | `jitter` | Jitter | `jitter` | lips | 0 | 20 | 3 | continuous | A fine, fast roughness of the pitch, like a living lip. Range 0 to 20 cents. |
| `235d9fc7` | `breath` | Breath noise | `air` | lips | 0 | 0.8 | 0.08 | continuous | The audible breath in the tone. Range 0 to 0.8. |
| `239b9a3e` | `brassiness` | Brassiness | `brassiness` | bore | 0 | 60 | 16 | continuous | How much the tone blares and steepens as it gets loud. Range 0 to 60. |
| `5b83d2e2` | `bore_decay` | Bore decay | `bore+decay` | bore | 0.005 | 0.2 | 0.025 | continuous | How long the tube's resonance rings: the bloom and the tail. Range 0.005 to 0.2 s. |
| `48d7d837` | `bell_hz` | Bell cutoff | `flare` | bore | 150 | 4000 | 1400 | continuous | The frequency above which the bell stops reflecting: the resonances above it fade. Range 150 to 4000 Hz. |
| `17979d07` | `rad_hz` | Radiation low cut | `radiate+low` | body | 20 | 2000 | 350 | continuous | The frequency below which the instrument radiates less. Range 20 to 2000 Hz. |
| `d6db0cb6` | `cup_hz` | Cup formant | `mouthpiece+rate` | body | 150 | 3000 | 1200 | continuous | Where the mouthpiece cup's formant sits. Range 150 to 3000 Hz. |
| `9be8e613` | `cup_q` | Cup sharpness | `mouthpiece+q` | body | 0.5 | 5 | 1.4 | continuous | How narrow the cup formant is. Range 0.5 to 5. |
| `df277df3` | `cup_gain` | Cup boost | `mouthpiece+level` | body | 0 | 4 | 1.5 | continuous | How much the cup formant is boosted. Range 0 to 4. |
| `53c95844` | `tone_hz` | Tone | `tone` | body | 500 | 12000 | 5500 | continuous | The brightness of the radiated tone: the frequency above which it rolls off. Range 500 to 12000 Hz. |
| `0cca7651` | `tone_q` | Tone sharpness | `tone+q` | body | 0.5 | 2 | 0.8 | continuous | How sharp the tone roll-off is. Range 0.5 to 2. |
| `4d442335` | `tone_dyn` | Tone by dynamics | `tone+soft` | body | 0 | 4 | 1.5 | continuous | How many octaves darker the tone gets when soft. Range 0 to 4 octaves. |
| `a5324c77` | `tilt_tone` | Tone by timbre | `tone+timbre` | body | -2 | 2 | 0.6 | continuous | How far timbre moves the tone: down is darker and more closed. Range -2 to 2 octaves. |
| `d6069f54` | `mute` | Mute | `mute` | mute | 0 | 1 | 0 | continuous | How deep the mute sits in the bell: 0 open. Range 0 to 1. |
| `58cbd8d3` | `mute_hz` | Mute resonance | `mute+rate` | mute | 300 | 5000 | 1700 | continuous | Where the mute's resonance sits. Range 300 to 5000 Hz. |
| `9f982c60` | `mute_q` | Mute sharpness | `mute+q` | mute | 0.5 | 10 | 4 | continuous | How sharp the mute's resonance is. Range 0.5 to 10. |
| `62cbe891` | `mute_hp` | Straight mute | `mute+high` | mute | 0 | 3 | 0.3 | continuous | The straight-mute character: how much of the highs pass. Range 0 to 3. |
| `5ebcd7bb` | `mute_bp` | Harmon mute | `mute+q` | mute | 0 | 4 | 2.5 | continuous | The harmon-mute character: how much of the resonant middle passes. Range 0 to 4. |
| `5ad66955` | `mute_lp` | Mute lows | `mute+low` | mute | 0 | 2 | 0 | continuous | How much of the lows pass the mute. Range 0 to 2. |
| `2de2e627` | `wah` | Wah | `mute+timbre` | mute | -2 | 2 | 0 | continuous | How far timbre moves the mute's resonance: up opens it. Range -2 to 2 octaves. |
| `25a7329d` | `tongue` | Strike | `strike` | attack | 0 | 2 | 1 | continuous | How much a hard strike sharpens the start: the tongue's hold and release, the accent and the "t". Range 0 to 2. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | attack | 0 | 1.5 | 0.35 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 1.5. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | attack | 0.01 | 0.3 | 0.05 | continuous | How long the accent lasts before the note settles. Range 0.01 to 0.3 s. |
| `0ffdea03` | `acc_bright` | Accent brightness | `accent+bright` | attack | 0 | 1.5 | 0.5 | continuous | How much brighter the start of a struck note is than the sustain. Range 0 to 1.5. |
| `bb6347c3` | `scoop` | Scoop | `scoop` | attack | 0 | 1.5 | 0.25 | continuous | How far the pitch scoops up from below at a strike. Range 0 to 1.5 semitones. |
| `71ca3e3e` | `blat` | Blat | `jitter+strike` | attack | 0 | 1.5 | 0.25 | continuous | The buzzy pitch flutter during a hard attack. Range 0 to 1.5 semitones. |
| `09ceaf46` | `t_noise` | Tongue "t" | `tongue` | attack | 0 | 1 | 0.15 | continuous | The short noise burst of a tongued start. Range 0 to 1. |
| `72d5f1ed` | `slur` | Slur | `slur` | attack | 0 | 1 | 0.25 | continuous | How much articulation a legato note change gets: 0 seamless, 1 each new note is re-tongued. Range 0 to 1. |
| `e4d88d72` | `vibrato` | Vibrato | `vibrato` | mod | 0 | 1 | 0 | continuous | The depth of the automatic vibrato that sets in after the note starts. Range 0 to 1. |
| `9e2c50e9` | `vib_hz` | Vibrato rate | `vibrato+rate` | mod | 2 | 9 | 5.5 | continuous | How fast the vibrato wavers. Range 2 to 9 Hz. |
| `175c54d9` | `flutter` | Flutter by shake | `flutter` | mod | 0 | 1 | 0.6 | continuous | How much flutter tongue a shake (the motion input) adds. Range 0 to 1. |
| `e6f3c1b1` | `flut_hz` | Flutter rate | `flutter+rate` | mod | 8 | 40 | 24 | continuous | How fast the flutter tongue rolls. Range 8 to 40 Hz. |
| `e7c742df` | `life` | Life | `life` | mod | 0 | 1 | 0 | continuous | Micro-randomness: 0 every note identical, 1 each one slightly different. Range 0 to 1. |
| `77bdc182` | `growl` | Growl | `growl` | growl | 0 | 1 | 0 | continuous | The depth of the throaty growl when the note is pushed hard: 0 off. Range 0 to 1. |
| `ef88e6bb` | `growl_from` | Growl from | `growl+press` | growl | 0 | 1.5 | 0.7 | continuous | The pressure where the growl sets in. Range 0 to 1.5. |
| `aed9b325` | `growl_hz` | Growl rate | `growl+rate` | growl | 10 | 80 | 15 | continuous | How fast the throat flutters in the growl. Range 10 to 80 Hz. |
| `7c445ab1` | `pair` | Second player | `ensemble` | section | 0 | 1 | 0 | continuous | The level of a second, slightly detuned player: 0 solo. Range 0 to 1. |
| `bb5fdd2c` | `detune` | Detune | `detune` | section | 0 | 30 | 8 | continuous | How far the second player is detuned. Range 0 to 30 cents. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 93.75 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 8.94531 KB (9160 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 80 KB |

<!-- awva_docs: end -->

## How it works

A hybrid: a lip source sets the pitch, and a physical bore shapes it.

- **Lips:** an outward-striking valve that opens once per period. The flow pulse is max(cos(2πφ) − c, 0)², and the closure c follows the breath: soft playing is almost sinusoidal, loud playing closes the lips for most of the cycle (a narrow, harmonic-rich pulse). The oscillator is the pitch master, so the voice stays in tune and can't jump registers. Lip life comes from a few cents of pitch jitter and breath turbulence modulated by the lip opening. A tongued attack adds a short blat: a scoop from below, irregular buzz, a brighter pulse, an overshoot and a "t".
- **Bore:** a waveguide loop one period long (mouthpiece → bell → mouthpiece). At the bell the low frequencies reflect and the rest radiates; the loop delay is the period minus the bell filter's phase delay, so its resonances sit on the harmonics. In steady state the radiated wave equals the source, but in time it behaves like a bore: the attack starts as a bright buzz while the low resonances build up, a release rings briefly, and legato changes smear.
- **Brassiness:** the wave steepens on its way to the bell like a finite-amplitude wave: the radiated wave is read at a delay that moves with the pressure, in two stages in series, with the read speed limited (no fold-back, no aliasing). High harmonics grow like powers of the amplitude, so soft is round and loud blares.
- **Body:** a radiation high-pass, the mouthpiece cup formant, and a bell/tone low-pass whose cutoff rises with the level. The mutes are one state-variable filter (straight, harmon, plunger); `timbre` sweeps the mute resonance or the tone.
- **Section:** an optional second player a few cents apart.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): breath, dynamics and brightness together (pp mellow, ff blaring).
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing. A hard strike gives a bright blat, an accent, a slight scoop and a "t"; re-striking a held note re-tongues.
- **release** (the Note Off): the lips stop and the bore rings out briefly; `sustain` lengthens the tail a little.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): open presets are brighter (above 0) or darker (below 0) (lips and bell; the horn's hand). In the mute presets it is a wah (harmon: above 0 opens; plunger: below 0 closes).
- **acceleration** (`motion.accel_g`, from a host's motion sensor): flutter tongue.
