<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# trumpet

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**physical trumpet: lips, mouthpiece and bore, played like the brass trumpet (1 preset)**

Header `dsp/include/awva/voice_trumpet.h`, traits `dsp/include/awva/engines/trumpet.h`, component id `com.sorbet-ai.awva.trumpet`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |
| pedal | `pedal`, 0 .. 1 (3/16 at rest) | CC64 |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | trumpet | `brassiness`, `bell_hz`, `tone_hz`, `tongue`, `p_loud`, `breath`, `lip_tilt`, `release` |

After `init()` the engine plays preset 0 (trumpet).

## Hero parameters

`brassiness`, `bell_hz`, `tone_hz`, `tongue`, `p_loud`, `breath`, `lip_tilt`, `release`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0.05 | 20 | 19.3075 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.351743 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 1.19262 | continuous |
| `14a92b36` | `pres_hz` | level | 2 | 200 | 16.0102 | continuous |
| `1036ae7e` | `release` | level | 0.005 | 1 | 0.050774 | continuous |
| `c7d91eff` | `rel_pedal` | level | 0 | 3 | 0.4 | continuous |
| `f4ada648` | `p_soft` | breath | 0.5 | 3 | 1.05585 | continuous |
| `ac9cdaa8` | `p_loud` | breath | 1.1 | 20 | 14.9845 | continuous |
| `45481f5a` | `p_reg` | breath | -0.5 | 1.5 | -0.130238 | continuous |
| `b3a3844b` | `p_curve` | breath | 0.3 | 3 | 2.41112 | continuous |
| `ce695ef1` | `lip_tune` | lips | -600 | 300 | -265.508 | continuous |
| `5f7a1151` | `lip_treg` | lips | -300 | 300 | 84.7883 | continuous |
| `da4434a2` | `lip_q` | lips | 0.5 | 60 | 9.98719 | continuous |
| `1816948b` | `lip_mass` | lips | 0.2 | 30 | 5.04216 | continuous |
| `637c9830` | `mass_reg` | lips | -0.5 | 2 | 1.24726 | continuous |
| `8e5c632b` | `lip_open` | lips | 0 | 1.5 | 0.020385 | continuous |
| `29073838` | `open_reg` | lips | -1 | 2 | 1.02704 | continuous |
| `13cb67c9` | `lip_width` | lips | 3 | 15 | 12.906 | continuous |
| `4303b923` | `contact` | lips | 0 | 20 | 1.01252 | continuous |
| `693ecfc5` | `lip_nl` | lips | 0 | 100 | 4.18258 | continuous |
| `f3575e44` | `embouch` | lips | 0 | 1.5 | 0.93927 | continuous |
| `e03cb8d0` | `lip_tilt` | lips | -1.5 | 1.5 | 0.061837 | continuous |
| `02049853` | `jitter` | lips | 0 | 40 | 1.37483 | continuous |
| `fa56340f` | `cup` | mouthpiece | 0.2 | 2.5 | 1.48285 | continuous |
| `69324bcf` | `pop_hz` | mouthpiece | 300 | 3000 | 1427.48 | continuous |
| `11bad404` | `pop_q` | mouthpiece | 0.5 | 10 | 0.976347 | continuous |
| `5b83d2e2` | `bore_decay` | bore | 0.005 | 0.2 | 0.044498 | continuous |
| `d2924a86` | `bore_reg` | bore | -1 | 2 | 0.784376 | continuous |
| `48d7d837` | `bell_hz` | bore | 150 | 4000 | 1821.65 | continuous |
| `239b9a3e` | `brassiness` | bore | 0 | 1500 | 448.664 | continuous |
| `17979d07` | `rad_hz` | radiation | 20 | 2000 | 109.229 | continuous |
| `53c95844` | `tone_hz` | radiation | 500 | 16000 | 8298.62 | continuous |
| `0cca7651` | `tone_q` | radiation | 0.5 | 2 | 0.542062 | continuous |
| `42267f29` | `low_comp` | radiation | 0 | 2 | 1.05614 | continuous |
| `235d9fc7` | `breath` | air | 0 | 0.8 | 0.108717 | continuous |
| `09ceaf46` | `t_noise` | air | 0 | 1 | 0.211963 | continuous |
| `25a7329d` | `tongue` | attack | 0 | 2 | 1.39721 | continuous |
| `6f1a0e5f` | `acc_level` | attack | 0 | 1.5 | 1.25615 | continuous |
| `6f9d5d57` | `acc_decay` | attack | 0.01 | 0.3 | 0.031235 | continuous |
| `fd47c156` | `tongue_t` | attack | 0.001 | 0.05 | 0.001128 | continuous |
| `aa44f1df` | `tongue_lip` | attack | 0 | 2 | 0.053056 | continuous |
| `72d5f1ed` | `slur` | attack | 0 | 1 | 0.148917 | continuous |
| `af32797a` | `pitch_t` | attack | 0.003 | 0.3 | 0.00626 | continuous |
| `175c54d9` | `flutter` | mod | 0 | 1 | 0.6 | continuous |
| `e6f3c1b1` | `flut_hz` | mod | 8 | 40 | 24 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 44000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 2 s of the last release |
| engine object | 8.16406 KB (8360 bytes) (on a 64-bit host) |
| FastMemory | 64 KB |
| LargeMemory | 16 KB |

<!-- awva_docs: end -->

## How it works

A physical model: nothing sets the pitch but the lips and the bore together. There is no oscillator.

- **Lips:** one mechanical mode, outward striking (the pressure difference pushes them open; Elliott & Bowsher 1982), tuned below the note. It has a mass, a Q, a rest opening per register, a width, contact stiffness and damping when closed, and tissue losses that grow with the lip speed. The embouchure firms with the breath. The flow through them is a Bernoulli jet, regularised at small pressure differences, with turbulence riding on its pulsating part.
- **Mouthpiece:** the cup's compliance and the backbore's inertance, with a loss in parallel: the Helmholtz ("popping") resonance between lips and bore. It is solved implicitly (trapezoidal) together with the jet's conductance.
- **Bore:** a waveguide of two delay lines whose resonances sit on the note's harmonics. At the bell the low frequencies reflect (a one-pole low-pass times a round-trip loss from a decay time), with exactly -1 at DC (the open end). The loop delay is solved per block by Newton's method, so that the small-signal loop gain of the lips' admittance and the load they see is real at the note: the note sounds in tune at the oscillation threshold, and the same loop gain gives that threshold. A slow "player's ear" trims the bore for the pull of louder lips.
- **Brassiness:** the wave steepens on its way to the bell (finite-amplitude propagation; Hirschberg et al. 1996). The radiated wave is read at a delay that moves with the pressure, in two rate-limited stages. The loop itself stays linear.
- **Radiation:** the bell's transmission (1 - reflection), a radiation high-pass and a bell/directivity low-pass.

Every element is passive or bounded, and a per-voice guard resets a voice whose state goes non-finite. The parameters were fitted (CMA-ES) so that it plays like the brass engine's trumpet preset on the same performances: the attack, the response to strike and pressure, the noise.

## Playing

- **pressure:** the breath, dynamics and brightness together (pp round, ff blaring).
- **strike:** tonguing. A hard strike is a fast release with an accent: the lips are let go from closed, a blat that scoops up onto the note, plus a "t". A soft one is a "d"; no strike is a breath attack.
- **release:** the breath stops and the bore rings out; the pedal lengthens the release.
- **tilt forward/back:** the embouchure, firmer (brighter) or looser (darker).
- **sideways tilt:** a pitch bend (the host moves the note; the horn follows).
- **shake:** flutter tongue.
