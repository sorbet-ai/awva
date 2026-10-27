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
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |
| acceleration | `motion.accel_g`, the acceleration's magnitude in g (1 at rest) | a host's motion sensor (not from MIDI yet) |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | trumpet | `brassiness`, `bell_hz`, `tone_hz`, `tongue`, `p_loud`, `breath`, `lip_tilt`, `release` |

After `init()` the engine plays preset 0 (trumpet).

## Hero parameters

`brassiness`, `bell_hz`, `tone_hz`, `tongue`, `p_loud`, `breath`, `lip_tilt`, `release`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/trumpet.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0.05 | 20 | 19.3075 | continuous | The overall loudness of the sound. Range 0.05 to 20. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.351743 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 1.19262 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 2 | 200 | 16.0102 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 2 to 200 Hz. |
| `1036ae7e` | `release` | Release | `release` | level | 0.005 | 1 | 0.050774 | continuous | How long the sound takes to die away after the key is released. Range 0.005 to 1 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | level | 0 | 3 | 0.4 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 3 s. |
| `f4ada648` | `p_soft` | Breath, soft | `breath+soft` | breath | 0.5 | 3 | 1.05585 | continuous | The blowing pressure at the lightest touch, relative to where the tone just speaks: above 1 it always speaks, below 1 a light touch is only air. Range 0.5 to 3. |
| `ac9cdaa8` | `p_loud` | Breath, loud | `breath+loud` | breath | 1.1 | 20 | 14.9845 | continuous | The blowing pressure at a full press: higher is louder and brighter, and pushes the tone harder. Range 1.1 to 20. |
| `45481f5a` | `p_reg` | Breath by register | `breath+register` | breath | -0.5 | 1.5 | -0.130238 | continuous | How the blowing pressure changes going up the range: above 0 the high notes get more air. Range -0.5 to 1.5. |
| `b3a3844b` | `p_curve` | Breath curve | `dynamics` | breath | 0.3 | 3 | 2.41112 | continuous | The shape of the curve from key pressure to blowing pressure: below 1 soft playing is easier, above 1 the loud end comes later. Range 0.3 to 3. |
| `ce695ef1` | `lip_tune` | Lip tuning | `embouchure+pitch` | lips | -600 | 300 | -265.508 | continuous | Where the lips' resonance sits relative to the note: below 0 the lips strike outward. Range -600 to 300 cents. |
| `5f7a1151` | `lip_treg` | Lip tuning by register | `embouchure+register` | lips | -300 | 300 | 84.7883 | continuous | How the lip tuning changes per octave up. Range -300 to 300 cents. |
| `da4434a2` | `lip_q` | Lip sharpness | `embouchure+q` | lips | 0.5 | 60 | 9.98719 | continuous | How sharp the lips' resonance is. Range 0.5 to 60. |
| `1816948b` | `lip_mass` | Lip mass | `embouchure+size` | lips | 0.2 | 30 | 5.04216 | continuous | How heavy the lips are, on B flat 4. Range 0.2 to 30 kg/m². |
| `637c9830` | `mass_reg` | Lip mass by register | `embouchure+register` | lips | -0.5 | 2 | 1.24726 | continuous | How much lighter the lips get per octave up. Range -0.5 to 2 octaves. |
| `8e5c632b` | `lip_open` | Lip opening | `embouchure+open` | lips | 0 | 1.5 | 0.020385 | continuous | How open the lips rest, on B flat 4. Range 0 to 1.5 mm. |
| `29073838` | `open_reg` | Opening by register | `embouchure+register` | lips | -1 | 2 | 1.02704 | continuous | How much smaller the opening gets per octave up. Range -1 to 2 octaves. |
| `13cb67c9` | `lip_width` | Lip width | `embouchure+size` | lips | 3 | 15 | 12.906 | continuous | How wide the lip opening is. Range 3 to 15 mm. |
| `4303b923` | `contact` | Lip contact | `embouchure+q` | lips | 0 | 20 | 1.01252 | continuous | How stiff the lips are when they close against each other. Range 0 to 20. |
| `693ecfc5` | `lip_nl` | Lip losses | `embouchure+amount` | lips | 0 | 100 | 4.18258 | continuous | How much the lips lose energy as they move faster: tames the loud end. Range 0 to 100. |
| `f3575e44` | `embouch` | Embouchure firms | `embouchure+press` | lips | 0 | 1.5 | 0.93927 | continuous | How much the embouchure firms with the breath: 1 the aperture holds. Range 0 to 1.5. |
| `e03cb8d0` | `lip_tilt` | Lips by timbre | `embouchure+timbre` | lips | -1.5 | 1.5 | 0.061837 | continuous | How much timbre firms the lips: up brightens. Range -1.5 to 1.5. |
| `02049853` | `jitter` | Jitter | `jitter` | lips | 0 | 40 | 1.37483 | continuous | A fine, fast roughness of the pitch, like a living lip. Range 0 to 40 cents. |
| `fa56340f` | `cup` | Cup volume | `mouthpiece+size` | mouthpiece | 0.2 | 2.5 | 1.48285 | continuous | The size of the mouthpiece cup. Range 0.2 to 2.5 cm³. |
| `69324bcf` | `pop_hz` | Mouthpiece pop | `mouthpiece+rate` | mouthpiece | 300 | 3000 | 1427.48 | continuous | Where the mouthpiece's own resonance sits. Range 300 to 3000 Hz. |
| `11bad404` | `pop_q` | Mouthpiece pop sharpness | `mouthpiece+q` | mouthpiece | 0.5 | 10 | 0.976347 | continuous | How sharp the mouthpiece's resonance is. Range 0.5 to 10. |
| `5b83d2e2` | `bore_decay` | Bore decay | `bore+decay` | bore | 0.005 | 0.2 | 0.044498 | continuous | How long the tube's resonance rings: the bloom and the tail. Range 0.005 to 0.2 s. |
| `d2924a86` | `bore_reg` | Bore by register | `bore+register` | bore | -1 | 2 | 0.784376 | continuous | How much shorter the tube's ring gets per octave up. Range -1 to 2 octaves. |
| `48d7d837` | `bell_hz` | Bell cutoff | `flare` | bore | 150 | 4000 | 1821.65 | continuous | The frequency above which the bell stops reflecting: the resonances above it fade. Range 150 to 4000 Hz. |
| `239b9a3e` | `brassiness` | Brassiness | `brassiness` | bore | 0 | 1500 | 448.664 | continuous | How much the tone blares and steepens as it gets loud. Range 0 to 1500. |
| `17979d07` | `rad_hz` | Radiation low cut | `radiate+low` | radiation | 20 | 2000 | 109.229 | continuous | The frequency below which the instrument radiates less. Range 20 to 2000 Hz. |
| `53c95844` | `tone_hz` | Tone | `tone` | radiation | 500 | 16000 | 8298.62 | continuous | The brightness of the radiated tone: the frequency above which it rolls off. Range 500 to 16000 Hz. |
| `0cca7651` | `tone_q` | Tone sharpness | `tone+q` | radiation | 0.5 | 2 | 0.542062 | continuous | How sharp the tone roll-off is. Range 0.5 to 2. |
| `42267f29` | `low_comp` | Low compensation | `radiate+register` | radiation | 0 | 2 | 1.05614 | continuous | How much the low notes are brought up in level. Range 0 to 2. |
| `235d9fc7` | `breath` | Breath noise | `air` | air | 0 | 0.8 | 0.108717 | continuous | The audible breath in the tone. Range 0 to 0.8. |
| `09ceaf46` | `t_noise` | Tongue "t" | `tongue` | air | 0 | 1 | 0.211963 | continuous | The short noise burst of a tongued start. Range 0 to 1. |
| `25a7329d` | `tongue` | Strike | `strike` | attack | 0 | 2 | 1.39721 | continuous | How much a hard strike sharpens the start: the tongue's hold and release, the accent and the "t". Range 0 to 2. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | attack | 0 | 1.5 | 1.25615 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 1.5. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | attack | 0.01 | 0.3 | 0.031235 | continuous | How long the accent lasts before the note settles. Range 0.01 to 0.3 s. |
| `fd47c156` | `tongue_t` | Tongue release | `strike+speed` | attack | 0.001 | 0.05 | 0.001128 | continuous | How long the tongue takes to let the note go on a soft start; a hard strike is many times faster. Range 0.001 to 0.05 s. |
| `aa44f1df` | `tongue_lip` | Tongue on the lips | `strike+open` | attack | 0 | 2 | 0.053056 | continuous | How far the tongue holds the lips closed on a tongued start. Range 0 to 2 mm. |
| `72d5f1ed` | `slur` | Slur | `slur` | attack | 0 | 1 | 0.148917 | continuous | How much articulation a legato note change gets: 0 seamless, 1 each new note is re-tongued. Range 0 to 1. |
| `af32797a` | `pitch_t` | Pitch settling | `settle` | attack | 0.003 | 0.3 | 0.00626 | continuous | How fast the player's ear pulls the pitch to its centre after a start or a change. Range 0.003 to 0.3 s. |
| `175c54d9` | `flutter` | Flutter by shake | `flutter` | mod | 0 | 1 | 0.6 | continuous | How much flutter tongue a shake (the motion input) adds. Range 0 to 1. |
| `e6f3c1b1` | `flut_hz` | Flutter rate | `flutter+rate` | mod | 8 | 40 | 24 | continuous | How fast the flutter tongue rolls. Range 8 to 40 Hz. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 137.5 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 8.35156 KB (8552 bytes) (on a 64-bit host) |
| stream (StreamMemory) | 64 KB |
| bulk (BulkMemory) | 16 KB |

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

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath, dynamics and brightness together (pp round, ff blaring).
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing. A hard strike is a fast release with an accent: the lips are let go from closed, a blat that scoops up onto the note, plus a "t". A soft one is a "d"; no strike is a breath attack.
- **release** (the Note Off): the breath stops and the bore rings out; `sustain` lengthens the release.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the embouchure, firmer (above 0: brighter) or looser (below 0: darker).
- **pitch bend** (`pitch`: MIDI 2.0's per-note pitch, MPE's member-channel bend): the host moves the note; the horn follows.
- **acceleration** (`motion.accel_g`, from a host's motion sensor): flutter tongue.
