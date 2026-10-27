<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# flute

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**physical flutes: an air jet on the edge of an open-open bore (flute, alto flute, piccolo)**

Header `dsp/include/awva/voice_flute.h`, traits `dsp/include/awva/engines/flute.h`, component id `com.sorbet-ai.awva.flute`.

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
| 0 | flute | `p_loud`, `theta`, `y0`, `turb`, `ob_amt`, `cut_hz`, `tongue`, `release` |
| 1 | alto flute | `p_loud`, `theta`, `y0`, `turb`, `ob_amt`, `cut_hz`, `tongue`, `release` |
| 2 | piccolo | `p_loud`, `theta`, `y0`, `turb`, `ob_amt`, `cut_hz`, `tongue`, `release` |

After `init()` the engine plays preset 0 (flute).

## Hero parameters

`p_loud`, `theta`, `y0`, `turb`, `ob_amt`, `cut_hz`, `tongue`, `release`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/flute.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0 | 100 | 12.9 | continuous | The overall loudness of the sound. Range 0 to 100. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.35 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 0.65 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 2 | 200 | 36.5997 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 2 to 200 Hz. |
| `1036ae7e` | `release` | Release | `release` | level | 0.005 | 1 | 0.04 | continuous | How long the sound takes to die away after the key is released. Range 0.005 to 1 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | level | 0 | 3 | 0.4 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 3 s. |
| `f4ada648` | `p_soft` | Breath, soft | `breath+soft` | breath | 10 | 3000 | 233.295 | continuous | The blowing pressure at the lightest touch, relative to where the tone just speaks: above 1 it always speaks, below 1 a light touch is only air. Range 10 to 3000. |
| `ac9cdaa8` | `p_loud` | Breath, loud | `breath+loud` | breath | 20 | 6000 | 1920.16 | continuous | The blowing pressure at a full press: higher is louder and brighter, and pushes the tone harder. Range 20 to 6000. |
| `45481f5a` | `p_reg` | Breath by register | `breath+register` | breath | -0.5 | 2 | 0.773037 | continuous | How the blowing pressure changes going up the range: above 0 the high notes get more air. Range -0.5 to 2. |
| `b3a3844b` | `p_curve` | Breath curve | `dynamics` | breath | 0.3 | 3 | 1.21055 | continuous | The shape of the curve from key pressure to blowing pressure: below 1 soft playing is easier, above 1 the loud end comes later. Range 0.3 to 3. |
| `d0dcf09b` | `theta` | Jet timing | `jet+rate` | jet | 0.03 | 2 | 0.473292 | continuous | The jet's timing at the edge, relative to the note's period: 0.5 is the sweet spot. Range 0.03 to 2. |
| `fd1554fe` | `theta_reg` | Jet timing by register | `jet+register` | jet | -1 | 1 | 0.009298 | continuous | How the jet timing changes per octave up. Range -1 to 1 octaves. |
| `3554104a` | `w_track` | Lips follow breath | `jet+press` | jet | 0 | 1 | 0.998653 | continuous | How much the player shortens the lip distance with the breath: 1 keeps the jet timing. Range 0 to 1. |
| `bcb6b31b` | `jet_h` | Lip opening | `jet+open` | jet | 0.2 | 3 | 0.554691 | continuous | How thick the jet is: the lip opening. Range 0.2 to 3 mm. |
| `6933e4f0` | `h_loud` | Opening when loud | `jet+loud` | jet | -1 | 2 | -0.352882 | continuous | How much the lips open with the breath. Range -1 to 2 octaves. |
| `a9b69532` | `jet_w` | Jet width | `jet+size` | jet | 2 | 20 | 17.953 | continuous | How wide the jet is. Range 2 to 20 mm. |
| `ca3e3f27` | `g_soft` | Jet gain, soft | `jet+soft` | jet | 0.3 | 20 | 1.05304 | continuous | How much the jet feeds the tone at the lightest touch: 1 just speaks, more is surer. Range 0.3 to 20. |
| `2e3dc25e` | `amp_loud` | Jet gain, loud | `jet+loud` | jet | -3 | 3 | 0.613379 | continuous | How much more the jet feeds the tone at a full breath. Range -3 to 3 octaves. |
| `8b5ffe8c` | `y0` | Edge offset | `jet+amount` | jet | -5 | 5 | 0.207421 | continuous | Where the jet aims at the edge, relative to its thickness: off-centre adds even harmonics. Range -5 to 5. |
| `da682d3a` | `y_tilt` | Aim by timbre | `jet+timbre` | jet | -3 | 3 | 0.3 | continuous | How far timbre moves the jet's aim. Range -3 to 3. |
| `70626fd6` | `jet_st` | Jet bandwidth | `jet+bright` | jet | 0.01 | 1 | 0.496387 | continuous | How high the jet responds: lower is smoother. Range 0.01 to 1. |
| `d8760e19` | `vortex` | Edge losses | `jet+amount` | jet | 0.1 | 5 | 1.14688 | continuous | The losses at the edge: larger is less loss and a freer tone. Range 0.1 to 5. |
| `89e67032` | `bore_mm` | Bore radius | `bore+size` | bore | 2 | 20 | 9.5 | continuous | How wide the tube is. Range 2 to 20 mm. |
| `5b83d2e2` | `bore_decay` | Bore decay | `bore+decay` | bore | 0.002 | 0.5 | 0.023309 | continuous | How long the tube's resonance rings: the bloom and the tail. Range 0.002 to 0.5 s. |
| `d2924a86` | `bore_reg` | Bore by register | `bore+register` | bore | -1 | 2 | 0.848711 | continuous | How much shorter the tube's ring gets per octave up. Range -1 to 2 octaves. |
| `ba4d544a` | `cut_hz` | Tone-hole cutoff | `tone-holes` | bore | 200 | 12000 | 5730.56 | continuous | The frequency above which the open tone holes stop reflecting: the flute tone's brightness. Range 200 to 12000 Hz. |
| `a7262d15` | `wall` | Wall losses | `bore+amount` | bore | 0.5 | 50 | 7.21246 | continuous | How much the tube's walls soak up: higher is darker and softer. Range 0.5 to 50. |
| `506051f5` | `disp` | End correction | `bore+pitch` | bore | -0.9 | 0.9 | -0.024968 | continuous | A tuning correction of the tube's open end: 0 none. Range -0.9 to 0.9. |
| `bacd0480` | `cover` | Cover pitch | `embouchure+pitch` | bore | -200 | 200 | -30 | continuous | How far covering the embouchure hole (timbre up) flattens the pitch. Range -200 to 200 cents. |
| `0dabae5f` | `cover_hz` | Cover darkness | `embouchure+bright` | bore | -2 | 2 | -0.3 | continuous | How much covering the embouchure hole darkens the tone. Range -2 to 2 octaves. |
| `7e7d21e6` | `low_note` | Lowest note | `range+low` | fingering | 24 | 96 | 60 | continuous | The instrument's lowest note: the first register starts here. Range 24 to 96 MIDI note. |
| `5dedc587` | `overblow` | Overblow | `overtone+register` | fingering | 0 | 1 | 1 | toggle | Whether the second and third octaves are overblown (on) or every note plays on the fundamental (off). Range off or on. |
| `8c08f22a` | `range_lo` | Range, low | `range+low` | fingering | 24 | 96 | 59 | continuous | The bottom of the fitted range; notes below keep the lowest note's playing. Range 24 to 96 MIDI note. |
| `8e11efac` | `range_hi` | Range, high | `range+high` | fingering | 48 | 120 | 98 | continuous | The top of the fitted range; notes above play on a short pipe's fundamental. Range 48 to 120 MIDI note. |
| `a87d86ad` | `g_out` | Gain out of range | `range+soft` | fingering | 1 | 6 | 3 | continuous | How surely notes above the range speak at the lightest touch. Range 1 to 6. |
| `73670f0a` | `out_db` | Level out of range | `range+level` | fingering | -24 | 6 | -11 | continuous | The level of notes above the range, relative to the fitted ones. Range -24 to 6 dB. |
| `92595769` | `ob_amt` | Overblow drive | `jet+register` | jet | 0 | 1 | 0 | continuous | How much a hard breath pushes the note into the next register: 0 never. Range 0 to 1. |
| `e59e03a5` | `ob_thr` | Overblow from | `jet+press` | jet | 0 | 1.5 | 0.7 | continuous | The breath where overblowing starts. Range 0 to 1.5. |
| `742581dc` | `mouth` | Embouchure radiation | `radiate+open` | radiation | 0 | 4 | 1 | continuous | How much sound comes from the embouchure hole. Range 0 to 4. |
| `6a8e75aa` | `end` | End radiation | `radiate` | radiation | 0 | 4 | 0.013091 | continuous | How much sound comes from the open end and tone holes. Range 0 to 4. |
| `e24d7b34` | `dipole` | Edge tone | `radiate+noise` | radiation | 0 | 4 | 0.969396 | continuous | How much of the jet's own edge tone you hear directly. Range 0 to 4. |
| `53c95844` | `tone_hz` | Tone | `tone` | radiation | 500 | 16000 | 1462.35 | continuous | The brightness of the radiated tone: the frequency above which it rolls off. Range 500 to 16000 Hz. |
| `0cca7651` | `tone_q` | Tone sharpness | `tone+q` | radiation | 0.5 | 2 | 0.989089 | continuous | How sharp the tone roll-off is. Range 0.5 to 2. |
| `854bbf96` | `turb` | Turbulence | `air+noise` | air | 0 | 2 | 0.103732 | continuous | The turbulence in the air stream. Range 0 to 2. |
| `99df0ff9` | `turb_hz` | Turbulence bandwidth | `air+rate` | air | 100 | 12000 | 732.8 | continuous | How high the turbulence reaches. Range 100 to 12000 Hz. |
| `4aac31a8` | `hiss` | Hiss | `air` | air | 0 | 1 | 0.0116215 | continuous | The steady hiss of the air. Range 0 to 1. |
| `d6d799d2` | `hiss_st` | Hiss pitch | `air+rate` | air | 0.02 | 1 | 0.168445 | continuous | Where the hiss sits, as a Strouhal number of the jet. Range 0.02 to 1. |
| `09ceaf46` | `t_noise` | Tongue "t" | `tongue` | air | 0 | 3 | 1.26998 | continuous | The short noise burst of a tongued start. Range 0 to 3. |
| `25a7329d` | `tongue` | Strike | `strike` | attack | 0 | 2 | 0.516874 | continuous | How much a hard strike sharpens the start: the tongue's hold and release, the accent and the "t". Range 0 to 2. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | attack | 0 | 1.5 | 0.050248 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 1.5. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | attack | 0.005 | 0.3 | 0.079499 | continuous | How long the accent lasts before the note settles. Range 0.005 to 0.3 s. |
| `fd47c156` | `tongue_t` | Tongue release | `strike+speed` | attack | 0.0005 | 0.1 | 0.003666 | continuous | How long the tongue takes to let the note go on a soft start; a hard strike is many times faster. Range 0.0005 to 0.1 s. |
| `48e3b5fa` | `att_gain` | Attack jet | `strike+level` | attack | 0 | 40 | 20.8502 | continuous | How much faster and thinner the jet starts after the tongue's release. Range 0 to 40. |
| `fd2c475f` | `att_t` | Attack jet length | `strike+time` | attack | 0.005 | 0.15 | 0.0220593 | continuous | How long the fast, thin starting jet lasts. Range 0.005 to 0.15 s. |
| `835672b9` | `att_soft` | Attack without a strike | `strike+soft` | attack | 0 | 1 | 0.996383 | continuous | How much of the fast start a breath attack or a slur still gets. Range 0 to 1. |
| `72d5f1ed` | `slur` | Slur | `slur` | attack | 0 | 1 | 0.1 | continuous | How much articulation a legato note change gets: 0 seamless, 1 each new note is re-tongued. Range 0 to 1. |
| `af32797a` | `pitch_t` | Pitch settling | `settle` | attack | 0.002 | 1 | 0.03 | continuous | How fast the player's ear pulls the pitch to its centre after a start or a change. Range 0.002 to 1 s. |
| `6a122da6` | `vib` | Vibrato by shake | `vibrato+shake` | mod | 0 | 2 | 0.5 | continuous | How much breath vibrato a shake (the motion input) adds. Range 0 to 2. |
| `9e2c50e9` | `vib_hz` | Vibrato rate | `vibrato+rate` | mod | 2 | 9 | 5.2 | continuous | How fast the vibrato wavers. Range 2 to 9 Hz. |
| `dbdd06c6` | `tongue_nz` | Tongue noise by strike | `tongue` | attack | 0 | 2 | 0.516874 | continuous | How much of the strike goes into the "t" noise at the start. Range 0 to 2. |
| `e5f99898` | `tongue_rt` | Tongue speed by strike | `strike+speed` | attack | 0 | 2 | 0.516874 | continuous | How much a hard strike quickens the tongue's release. Range 0 to 2. |
| `69fb3ac7` | `tongue_sus` | Swell accent | `accent+hold` | attack | 0 | 2 | 0.516874 | continuous | How much accent and noise a swell within a held note gets: the texture of the sustain. Range 0 to 2. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 115.625 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 3.14844 KB (3224 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 120 KB |

<!-- awva_docs: end -->

## How it works

A physical model of the flute family: the concert flute, the alto flute and the piccolo are presets of one engine. Nothing sets the pitch but the jet and the bore together, in the style of the physical trumpet. There is no oscillator.

- **Breath:** key pressure gives the breath level, smoothed, and that gives the blowing pressure, from `p_soft` to `p_loud`, rising `p_reg` octaves per octave of pitch (high notes need more). The jet leaves the lips at the Bernoulli velocity.
- **Jet:** the jet-drive model (Coltman; Verge, Fabre, Hirschberg et al. 1997; Fletcher & Rossing ch. 16; the time-domain form of Auvray, Ernoult, Fabre & Lagree, JASA 2014).
  - The acoustic flow through the embouchure window deflects the jet at the lips. The deflection is convected to the edge at about 0.4 of the jet's speed and grows on the way: the jet's instability.
  - The edge splits the jet into the flow into the pipe and the flow outside, with `y0` the edge's offset from the jet's centre line (the even harmonics). The flow injected on both sides of the edge is a pressure dipole across the window.
- **Player:** the lip-to-edge distance is the player's. It is set per note so that the jet's travel time is a part `theta` of the period at the middle breath. A louder breath shortens the travel time unless the player follows it (`w_track`). With `theta` too small for the note, the jet favours the next mode (overblowing); too large, it drops.
- **Losses:** vortex shedding at the edge, solved implicitly with the pipe's flow (one square root per sample).
- **Noise:** the jet's turbulence rides the same instability, so it is heard through the resonances as breathiness, growing with the jet's speed. The jet's own edge noise is radiated directly, a band at a Strouhal frequency. The tongue adds its "t".
- **Bore:** an open-open cylinder, a waveguide of two delay lines.
  - At the open end (the first open tone hole) the low frequencies reflect inverted. Above the tone-hole lattice's cutoff they leave.
  - A first-order allpass gives the frequency-dependent end corrections: the upper resonances sit flat.
  - The mouth end is open too.
  - A slow "player's ear" trims the bore onto the note (within 150 cents).
- **Fingering:** as on the flute. The first octave from the instrument's lowest note plays on the fundamental, the second octave overblown (the bore an octave down, the jet set for mode 2), the third on modes 3 and 4. A soft breath on an overblown note may fall to the lower mode, and a hard one on a low note may jump up: the jet decides.
- **Range:** inside `range_lo` .. `range_hi` the player is the fitted one. Outside it the player keeps the pressure, the jet phase and the bore losses of the range's edge. Above it the note plays on the fundamental of a short pipe, its level `out_db` meeting the edge. Past the pipe's longest (G2) and the jet's highest (6 kHz) the tone fades into the breath over 3 semitones.
- **Radiation:** the flow out of the embouchure hole and out of the open end, differentiated (monopole radiation), with a low-pass for the room and the directivity.

Every element is passive or bounded. The jet's flow is bounded, the pipe's loop gain is below 0.98, and the window flow is solved implicitly. A per-voice guard resets a voice whose state goes non-finite or absurd. The master has a DC blocker and a soft ceiling at 4.2.

The presets were fitted (CMA-ES) to recordings. The flute and the alto flute were fitted to the University of Iowa MIS recordings (non-vibrato, pp/mf/ff, every note): the steady harmonics, the level across notes and dynamics, the noise, the attack and its rise, the timbre's change with the dynamic, the tuning and the register. The piccolo is the flute's player on the piccolo's geometry (an octave higher, a 5.4 mm bore). Its jet, bore losses, radiation, noise and attack were fitted to the VSCO-2 Community Edition piccolo (CC0). No recording is in this repository.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath. It sets the dynamics and the brightness (the edge saturates) and the breathiness. At the extremes the note overblows or falls back.
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing. The tongue releases the breath, faster for a harder strike, with the pressure built up behind it (an accent) and a "t" of turbulence: the chiff. The attack's jet is released fast and thin, so the tone speaks in tens of milliseconds. No strike is a breath attack.
- **release** (the Note Off): the breath stops and the pipe rings out. `sustain` lengthens the release.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the jet angle and the cover of the embouchure hole. Above 0 rolls in (darker, a little flatter); below 0 rolls out.
- **pitch bend** (`pitch`: MIDI 2.0's per-note pitch, MPE's member-channel bend): the host moves the note; the pipe follows.
- **acceleration** (`motion.accel_g`, from a host's motion sensor): breath vibrato.
- **`ob_amt`** (0 by default): overblowing. Above `ob_thr` of the breath, the player stops shortening the lip-to-edge distance as the jet speeds up. The jet's travel time falls, and the next mode of the pipe takes over, with the jet's hysteresis.
