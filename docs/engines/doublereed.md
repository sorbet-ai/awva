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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/doublereed.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0.05 | 100 | 1.722 | continuous | The overall loudness of the sound. Range 0.05 to 100. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.721312 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 0.536147 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 2 | 200 | 63.634 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 2 to 200 Hz. |
| `1036ae7e` | `release` | Release | `release` | level | 0.005 | 1 | 0.05 | continuous | How long the sound takes to die away after the key is released. Range 0.005 to 1 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | level | 0 | 3 | 0.4 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 3 s. |
| `f4ada648` | `p_soft` | Breath, soft | `breath+soft` | breath | 0.5 | 3 | 1.33259 | continuous | The blowing pressure at the lightest touch, relative to where the tone just speaks: above 1 it always speaks, below 1 a light touch is only air. Range 0.5 to 3. |
| `ac9cdaa8` | `p_loud` | Breath, loud | `breath+loud` | breath | 0.1 | 3 | 0.163817 | continuous | The blowing pressure at a full press: higher is louder and brighter, and pushes the tone harder. Range 0.1 to 3. |
| `45481f5a` | `p_reg` | Breath by register | `breath+register` | breath | -1 | 1 | 0.333613 | continuous | How the blowing pressure changes going up the range: above 0 the high notes get more air. Range -1 to 1. |
| `b3a3844b` | `p_curve` | Breath curve | `dynamics` | breath | 0.3 | 3 | 2.53337 | continuous | The shape of the curve from key pressure to blowing pressure: below 1 soft playing is easier, above 1 the loud end comes later. Range 0.3 to 3. |
| `e79a6f74` | `reed_hz` | Reed resonance | `reed+rate` | reed | 200 | 8000 | 2731.34 | continuous | Where the reed's own resonance sits. Range 200 to 8000 Hz. |
| `06277eb0` | `reed_reg` | Reed resonance by register | `reed+register` | reed | -1 | 1.5 | 1.24277 | continuous | How the reed resonance follows the register, in octaves per octave. Range -1 to 1.5 octaves. |
| `47cc72a1` | `reed_q` | Reed damping | `reed+q` | reed | 0.3 | 30 | 0.452105 | continuous | How much the lips damp the reed: low is damped and smooth, high is free and edgy. Range 0.3 to 30. |
| `afd734f2` | `reed_open` | Reed opening | `reed+open` | reed | 0.05 | 2.5 | 0.172683 | continuous | How open the reed tip rests, at the reference note. Range 0.05 to 2.5 mm. |
| `29073838` | `open_reg` | Opening by register | `reed+register` | reed | -1 | 1.5 | 0.947729 | continuous | How much smaller the reed's opening gets per octave up. Range -1 to 1.5 octaves. |
| `49cc75c7` | `reed_w` | Reed width | `reed+size` | reed | 2 | 25 | 5.4026 | continuous | How wide the reed tip is. Range 2 to 25 mm. |
| `396ad694` | `p_close` | Closing pressure | `reed+press` | reed | 1 | 40 | 14.0655 | continuous | The blowing pressure that closes the reed: the blades' stiffness. Range 1 to 40 kPa. |
| `4303b923` | `contact` | Blade contact | `reed+q` | reed | 0 | 50 | 1.3225 | continuous | How stiff the blades are when they beat against each other. Range 0 to 50. |
| `ca324b15` | `duct` | Duct | `reed+size` | reed | 0.05 | 5 | 4.00981 | continuous | The reed's narrow duct: the opening where it starts to choke the air. Range 0.05 to 5 mm. |
| `4113cde7` | `emb_tilt` | Bite by timbre | `embouchure+timbre` | reed | -1 | 1 | 0.25 | continuous | How much timbre bites the reed: up pinches it. Range -1 to 1 octaves. |
| `71143aa8` | `emb_soft` | Pinch when soft | `embouchure+soft` | reed | 0.1 | 1 | 0.343511 | continuous | How far the lips pinch the reed at the lightest breath, relaxing to the full opening when loud. Range 0.1 to 1. |
| `b4624bcf` | `bite` | Bite pitch | `embouchure+pitch` | reed | -100 | 100 | 15 | continuous | How far biting pulls the pitch as timbre moves. Range -100 to 100 cents. |
| `02049853` | `jitter` | Jitter | `jitter` | reed | 0 | 40 | 9.56209 | continuous | A fine, fast roughness of the pitch, like a living lip. Range 0 to 40 cents. |
| `f23b7114` | `wander` | Breath wander | `drift` | player | 0 | 0.3 | 0.048645 | continuous | How unsteady the breath is: the slow and fast fluctuation of a sustained tone. Range 0 to 0.3. |
| `f1f80bc2` | `lip_wander` | Lip wander | `drift+open` | player | 0 | 0.3 | 0.023202 | continuous | How unsteady the lips are on a sustained tone. Range 0 to 0.3. |
| `72049c93` | `wander_hz` | Wander speed | `drift+rate` | player | 0.3 | 30 | 7.37563 | continuous | How fast the breath and lips wander. Range 0.3 to 30 Hz. |
| `f9f50cf7` | `ref_note` | Reference note | `range` | bore | 24 | 96 | 69 | continuous | The note the instrument's parameters are measured at. Range 24 to 96 MIDI note. |
| `c6cd0dc1` | `apex` | Cone apex | `bore+size` | bore | 0.5 | 80 | 16.6843 | continuous | How far the reed sits from the cone's apex. Range 0.5 to 80 cm. |
| `99d79043` | `cavity` | Reed cavity | `bore+amount` | bore | 0 | 4 | 0.021383 | continuous | The air volume of the reed and staple, relative to the missing tip of the cone. Range 0 to 4. |
| `63cb2800` | `bore_d` | Bore diameter | `bore+size` | bore | 1.5 | 15 | 2.00927 | continuous | How wide the tube is at the reed. Range 1.5 to 15 mm. |
| `5b83d2e2` | `bore_decay` | Bore decay | `bore+decay` | bore | 0.003 | 0.3 | 0.095786 | continuous | How long the tube's resonance rings: the bloom and the tail. Range 0.003 to 0.3 s. |
| `d2924a86` | `bore_reg` | Bore by register | `bore+register` | bore | -1 | 2 | 1.38611 | continuous | How much shorter the tube's ring gets per octave up. Range -1 to 2 octaves. |
| `b17df278` | `lattice_hz` | Tone-hole cutoff | `tone-holes` | holes | 100 | 6000 | 4891.7 | continuous | The frequency above which the open tone holes stop reflecting: the wind tone's brightness. Range 100 to 6000 Hz. |
| `9dc9d62d` | `lattice_q` | Cutoff sharpness | `tone-holes+q` | holes | 0.4 | 4 | 0.422051 | continuous | How sharp the tone-hole cutoff is. Range 0.4 to 4. |
| `b38aa92c` | `lattice_reg` | Cutoff by register | `tone-holes+register` | holes | -1 | 1.5 | -0.498894 | continuous | How the tone-hole cutoff follows the register, in octaves per octave. Range -1 to 1.5 octaves. |
| `17979d07` | `rad_hz` | Radiation low cut | `radiate+low` | radiation | 10 | 3000 | 62.3155 | continuous | The frequency below which the instrument radiates less. Range 10 to 3000 Hz. |
| `53c95844` | `tone_hz` | Tone | `tone` | radiation | 300 | 16000 | 4015.93 | continuous | The brightness of the radiated tone: the frequency above which it rolls off. Range 300 to 16000 Hz. |
| `0cca7651` | `tone_q` | Tone sharpness | `tone+q` | radiation | 0.5 | 3 | 0.665699 | continuous | How sharp the tone roll-off is. Range 0.5 to 3. |
| `235d9fc7` | `breath` | Breath noise | `air` | air | 0 | 1 | 0.054957 | continuous | The audible breath in the tone. Range 0 to 1. |
| `4aac31a8` | `hiss` | Hiss | `air` | air | 0 | 0.5 | 0 | continuous | The steady hiss of the air. Range 0 to 0.5. |
| `09ceaf46` | `t_noise` | Tongue "t" | `tongue` | air | 0 | 1 | 0.000821 | continuous | The short noise burst of a tongued start. Range 0 to 1. |
| `25a7329d` | `tongue` | Strike | `strike` | attack | 0 | 2 | 1.21443 | continuous | How much a hard strike sharpens the start: the tongue's hold and release, the accent and the "t". Range 0 to 2. |
| `96f13303` | `att_rate` | Attack speed by strike | `strike+speed` | attack | 0 | 2 | 1.21443 | continuous | How much a hard strike quickens the attack: how fast the tongue lets the reed go and how far it had held it shut. Range 0 to 2. |
| `972d507d` | `att_noise` | Attack noise by strike | `tongue` | attack | 0 | 2 | 1.21443 | continuous | How much a hard strike adds to the "t" at the start. Range 0 to 2. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | attack | 0 | 1.5 | 1.45496 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 1.5. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | attack | 0.005 | 0.3 | 0.037843 | continuous | How long the accent lasts before the note settles. Range 0.005 to 0.3 s. |
| `fd47c156` | `tongue_t` | Tongue release | `strike+speed` | attack | 0.0005 | 0.05 | 0.0008 | continuous | How long the tongue takes to let the note go on a soft start; a hard strike is many times faster. Range 0.0005 to 0.05 s. |
| `6db50acc` | `tongue_shut` | Tongue on the reed | `strike+open` | attack | 0 | 1.2 | 0.49849 | continuous | How far the tongue holds the reed shut on a tongued start. Range 0 to 1.2. |
| `72d5f1ed` | `slur` | Slur | `slur` | attack | 0 | 1 | 0.15 | continuous | How much articulation a legato note change gets: 0 seamless, 1 each new note is re-tongued. Range 0 to 1. |
| `af32797a` | `pitch_t` | Pitch settling | `settle` | attack | 0.002 | 0.5 | 0.013862 | continuous | How fast the player's ear pulls the pitch to its centre after a start or a change. Range 0.002 to 0.5 s. |
| `175c54d9` | `flutter` | Flutter by shake | `flutter` | mod | 0 | 1 | 0.6 | continuous | How much flutter tongue a shake (the motion input) adds. Range 0 to 1. |
| `e6f3c1b1` | `flut_hz` | Flutter rate | `flutter+rate` | mod | 8 | 40 | 24 | continuous | How fast the flutter tongue rolls. Range 8 to 40 Hz. |

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

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath: dynamics and brightness together (pp sweet and covered, ff reedy, the blades beating).
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing. A hard strike is a fast release from a closed reed, an accent and a "t"; a soft one is a "d"; no strike is a breath attack.
- **release** (the Note Off): the breath stops and the bore rings out; `sustain` lengthens the release.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the embouchure: above 0 it bites (a smaller tip opening, more lip damping: thinner, brighter, a little sharp), below 0 it relaxes (darker, fuller, a little flat).
- **pitch bend** (`pitch`: MIDI 2.0's per-note pitch, MPE's member-channel bend): the host moves the note; the bore follows.
- **acceleration** (`motion.accel_g`, from a host's motion sensor): flutter tongue.
