<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# clarinet

<!-- awva_docs: generated from the engine's EngineInfo (sdk/tools/awva_docs.cpp); run it to update, don't edit -->

**physical clarinets: a single reed on a cylindrical bore with register hole and tone-hole lattice, fitted to recordings (2 presets)**

Header `dsp/include/awva/voice_clarinet.h`, traits `dsp/include/awva/engines/clarinet.h`, component id `com.sorbet-ai.awva.clarinet`.

## Inputs

| input | control | from MIDI |
|---|---|---|
| note | `note`, the pitch in semitones (69 = A4), bends included | the note number, plus the channel's pitch bend (MPE) or the note's own pitch (MIDI 2.0) |
| pressure | `pressure`, 0 released .. 1 MIDI 2.0's full scale (engines take up to 1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| pressure rate | `pressure_rate`, how fast the pressure moves: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| timbre | `timbre`, -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: Registered Per-Note Controller 74, CC74 (bipolar) |
| sustain | `sustain`, 0 .. 1 (0 at rest) | CC64 |

An input not listed makes no difference to the sound; each listed one changes it in at least one preset (the contract suite checks both). What each does in this engine: "Playing" below. MIDI in detail: [Playing with MIDI 2.0 and MPE](../midi.md).

## Presets

| # | name | hero parameters |
|---|---|---|
| 0 | clarinet | `zeta`, `reed_hz`, `g_loud`, `p_soft`, `lat_hz`, `tongue`, `emb`, `release` |
| 1 | bass clarinet | `zeta`, `reed_hz`, `g_loud`, `p_soft`, `lat_hz`, `tongue`, `emb`, `release` |

After `init()` the engine plays preset 0 (clarinet).

## Hero parameters

`zeta`, `reed_hz`, `g_loud`, `p_soft`, `lat_hz`, `tongue`, `emb`, `release`

## Parameters

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name). Label, icon and what it does: the parameter's text for people (`param_text/clarinet.json`; the icons: [Parameter icons](../icons.md)).

| id | name | label | icon | group | min | max | default | kind | what it does |
|---|---|---|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | Level | `level` | level | 0.1 | 100 | 10.2 | continuous | The overall loudness of the sound. Range 0.1 to 100. |
| `75245e0b` | `pres_lin` | Dynamics (even) | `dynamics+soft` | level | 0 | 1 | 0.585375 | continuous | The even part of how pressure becomes loudness: more here and a light touch already speaks. Range 0 to 1. |
| `1b755ad8` | `pres_sq` | Dynamics (curved) | `dynamics+loud` | level | 0 | 1.5 | 0.416054 | continuous | The curved part of how pressure becomes loudness: more here and the loud end opens up faster than the soft end. Range 0 to 1.5. |
| `14a92b36` | `pres_hz` | Response | `response` | level | 2 | 200 | 14.0604 | continuous | How quickly the sound follows a change of pressure: low is a slow swell, high is immediate. Range 2 to 200 Hz. |
| `1036ae7e` | `release` | Release | `release` | level | 0.005 | 1 | 0.137643 | continuous | How long the sound takes to die away after the key is released. Range 0.005 to 1 s. |
| `c7d91eff` | `rel_pedal` | Release with pedal | `release+pedal` | level | 0 | 3 | 0.4 | continuous | How much longer the release gets with the sustain pedal down. Range 0 to 3 s. |
| `f4ada648` | `p_soft` | Breath, soft | `breath+soft` | breath | 0.8 | 2 | 1.09407 | continuous | The blowing pressure at the lightest touch, relative to where the tone just speaks: above 1 it always speaks, below 1 a light touch is only air. Range 0.8 to 2. |
| `e754ff5f` | `g_loud` | Breath, loud | `breath+loud` | breath | 0.3 | 1.4 | 0.770122 | continuous | The blowing pressure at a full press, relative to the reed's closing pressure: higher is louder and more driven. Range 0.3 to 1.4. |
| `e68ca377` | `g_reg` | Breath by register | `breath+register` | breath | -0.5 | 0.5 | -0.100848 | continuous | How the full-press blowing pressure changes per octave up: above 0 the high notes get more air. Range -0.5 to 0.5. |
| `b3a3844b` | `p_curve` | Breath curve | `dynamics` | breath | 0.3 | 3 | 1.04757 | continuous | The shape of the curve from key pressure to blowing pressure: below 1 soft playing is easier, above 1 the loud end comes later. Range 0.3 to 3. |
| `657428bf` | `zeta` | Reed opening | `reed+open` | reed | 0.05 | 1.2 | 0.260937 | continuous | How open the reed is: higher is louder and freer, lower is tighter. Range 0.05 to 1.2. |
| `d63bf2aa` | `zeta_reg` | Opening by register | `reed+register` | reed | -1 | 1 | 0.499791 | continuous | How the reed opening changes per octave up. Range -1 to 1 octaves. |
| `e79a6f74` | `reed_hz` | Reed resonance | `reed+rate` | reed | 500 | 6000 | 4749.94 | continuous | Where the reed's own resonance sits. Range 500 to 6000 Hz. |
| `47cc72a1` | `reed_q` | Reed damping | `reed+q` | reed | 0.05 | 1.2 | 0.537844 | continuous | How much the lips damp the reed: low is damped and smooth, high is free and edgy. Range 0.05 to 1.2. |
| `4d6071e5` | `lay` | Lay | `reed+q` | reed | 0 | 12 | 6.95634 | continuous | How stiff the reed is against the mouthpiece when shut. Range 0 to 12. |
| `7e8ac38f` | `emb` | Embouchure by timbre | `embouchure+timbre` | reed | -1.5 | 1.5 | 0.4 | continuous | How much timbre firms the embouchure: up tightens the reed. Range -1.5 to 1.5 octaves. |
| `02934c93` | `emb_bend` | Pitch by timbre | `embouchure+pitch` | reed | -100 | 100 | 25 | continuous | How far the embouchure pulls the pitch as timbre moves. Range -100 to 100 cents. |
| `79cb4aa2` | `bore_r` | Bore radius | `bore+size` | bore | 3 | 20 | 7.3 | continuous | How wide the tube is: the wall losses. Range 3 to 20 mm. |
| `a7262d15` | `wall` | Wall losses | `bore+amount` | bore | 0 | 5 | 0.794358 | continuous | How much the tube's walls soak up: higher is darker and softer. Range 0 to 5. |
| `9875d581` | `lat_hz` | Tone-hole cutoff | `tone-holes` | bore | 300 | 5000 | 3468.1 | continuous | The frequency above which the open tone holes stop reflecting: the wind tone's brightness. Range 300 to 5000 Hz. |
| `52aa6206` | `lat_q` | Cutoff sharpness | `tone-holes+q` | bore | 0.3 | 0.7 | 0.350535 | continuous | How sharp the tone-hole cutoff is. Range 0.3 to 0.7. |
| `59d3f5e4` | `reg_pos` | Register hole position | `tone-holes+register` | bore | 0.03 | 0.42 | 0.152096 | continuous | How far the register hole sits from the reed. Range 0.03 to 0.42 m. |
| `9faaca68` | `reg_tau` | Register hole inertance | `tone-holes+register` | bore | 0.1 | 10 | 0.995833 | continuous | How sluggish the register hole is: its effect on the upper register's tone. Range 0.1 to 10 ms. |
| `fca07064` | `reg_r` | Register hole resistance | `tone-holes+register` | bore | 0 | 2 | 0.960028 | continuous | How much the register hole resists the air. Range 0 to 2. |
| `81d60b50` | `alt_rel` | Altissimo from | `tone-holes+high` | bore | 19 | 40 | 26.5117 | continuous | How many semitones above the lowest note the altissimo register starts. Range 19 to 40 semitones. |
| `53c95844` | `tone_hz` | Tone | `tone` | radiation | 1000 | 20000 | 3077.53 | continuous | The brightness of the radiated tone: the frequency above which it rolls off. Range 1000 to 20000 Hz. |
| `0cca7651` | `tone_q` | Tone sharpness | `tone+q` | radiation | 0.5 | 2 | 0.7 | continuous | How sharp the tone roll-off is. Range 0.5 to 2. |
| `235d9fc7` | `breath` | Breath noise | `air` | air | 0 | 1 | 0.0588474 | continuous | The audible breath in the tone. Range 0 to 1. |
| `e23d0dfc` | `breath_mod` | Breath pulsing | `air+rate` | air | 0 | 1 | 0.00233541 | continuous | Whether the breath noise follows the pulses of the tone (1) or the steady flow (0). Range 0 to 1. |
| `c5316b30` | `breath_hp` | Breath colour | `air+bright` | air | 0 | 4000 | 860.735 | continuous | The frequency below which the breath noise is cut: 0 none. Range 0 to 4000 Hz. |
| `f23b7114` | `wander` | Breath wander | `drift` | air | 0 | 0.2 | 0.00386448 | continuous | How unsteady the breath is: the slow and fast fluctuation of a sustained tone. Range 0 to 0.2. |
| `09ceaf46` | `t_noise` | Tongue "t" | `tongue` | air | 0 | 1 | 0.00055064 | continuous | The short noise burst of a tongued start. Range 0 to 1. |
| `a5cd3dfb` | `t_len` | Tongue "t" length | `tongue+time` | air | 0.001 | 0.05 | 0.00561325 | continuous | How long the "t" burst lasts. Range 0.001 to 0.05 s. |
| `00c632f3` | `t_gate` | Tongue "t" gate | `tongue+gate` | air | 0 | 1 | 0.00988701 | continuous | How much the reed speaking silences the "t": 0 not at all, 1 the "t" sounds only before the tone. Range 0 to 1. |
| `ecaee291` | `breath_pre` | Air before the tone | `air+early` | air | 0 | 4 | 0.2262 | continuous | The hiss before the reed speaks, relative to the breath noise: an air attack. Range 0 to 4. |
| `25a7329d` | `tongue` | Strike | `strike` | attack | 0 | 2 | 1.91798 | continuous | How much a hard strike sharpens the start: the tongue's hold and release, the accent and the "t". Range 0 to 2. |
| `9a6d88a0` | `tongue_noise` | Tongue noise | `tongue` | attack | 0 | 2 | 1.14578 | continuous | How much "t" a hard strike adds at the start of the note. Range 0 to 2. |
| `6f1a0e5f` | `acc_level` | Accent | `accent` | attack | 0 | 1 | 0.566124 | continuous | The extra push at the start of a struck note: 0 none, higher is a sharper attack. Range 0 to 1. |
| `6f9d5d57` | `acc_decay` | Accent length | `accent+time` | attack | 0.01 | 0.3 | 0.0167341 | continuous | How long the accent lasts before the note settles. Range 0.01 to 0.3 s. |
| `fd47c156` | `tongue_t` | Tongue release | `strike+speed` | attack | 0.001 | 0.05 | 0.0496621 | continuous | How long the tongue takes to let the note go on a soft start; a hard strike is many times faster. Range 0.001 to 0.05 s. |
| `eb47a500` | `tongue_f` | Tongue on the reed | `strike+open` | attack | 0 | 4 | 0.998742 | continuous | How hard the tongue pushes the reed shut on a tongued start. Range 0 to 4. |
| `72d5f1ed` | `slur` | Slur | `slur` | attack | 0 | 1 | 0.00100402 | continuous | How much articulation a legato note change gets: 0 seamless, 1 each new note is re-tongued. Range 0 to 1. |
| `68410493` | `tune` | Tune | `tuning` | attack | -100 | 100 | 0 | continuous | A fine tuning offset. Range -100 to 100 cents. |
| `af32797a` | `pitch_t` | Pitch settling | `settle` | attack | 0.003 | 0.3 | 0.01 | continuous | How fast the player's ear pulls the pitch to its centre after a start or a change. Range 0.003 to 0.3 s. |
| `9a67fb30` | `instrument` | Instrument | `instrument` | bore | 0 | 1 | 0 | steps | Which instrument the engine plays; its lowest note sets where the registers lie. Range steps 0 to 1. |

## Cost and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 131.25 instructions a voice and frame (Cortex-M7 (FPv5-D16), gcc -O3, instructions counted under QEMU: 10 voices at 48 kHz, the engine's worst preset) |
| tail | silent and idle within 2 s of the last release |
| hot (the engine object) | 4.25781 KB (4360 bytes) (on a 64-bit host) |
| stream (StreamMemory) | none |
| bulk (BulkMemory) | 100 KB |

<!-- awva_docs: end -->

## How it works

A physical model: nothing sets the pitch but the reed and the bore together. There is no oscillator.

- **Reed:** a single reed on a mouthpiece, one mechanical mode (its resonance and the lip's damping), pushed closed by the pressure difference across it and beating on the lay when blown hard. The flow through the slit is Bernoulli's, solved exactly with the bore's wave every sample (Guillemain, Kergomard & Voinier 2005). Pressures are in units of the reed's closing pressure: the mouth pressure speaks near 1/3, beats from 1/2 and chokes near 1 (Kergomard & Chaigne). Turbulence rides only on the flow's pulsating part, so there is no air before the reed speaks.
- **Bore:** a cylinder, two waveguide sections split at the register hole. Its length is the fingering's: a quarter wave of the note in the chalumeau, three quarters in the clarion with the register hole open (a resistive shunt inertance that bleeds the first resonance away, so the reed plays the twelfth), and a short tube again above the clarion (the altissimo's vented fingerings). Wall losses grow with the square root of the frequency and with the length.
- **Tone holes:** the open tone-hole lattice reflects the low frequencies and passes the high ones out, a fourth-order cutoff (flat below, steep above). The steep cutoff keeps the reed's two transitions per period short; the chalumeau's 2nd harmonic, radiated as the flow at an impedance minimum, depends on it.
- **Radiation:** the flow through the open end, differentiated (a monopole), then a tone low-pass.
- **Tuning:** the bore's length is solved per block (Newton's method) so that the input impedance, register hole and lattice included, peaks at the note together with the reed's own reactance; a slow "player's ear" trims the rest. The same impedance gives the threshold of oscillation, which places the lightest touch.

Every element is passive or bounded, and a per-voice guard resets a voice whose state goes non-finite. The presets' parameters were fitted (CMA-ES) to recordings of a B-flat and a bass clarinet (University of Iowa Musical Instrument Samples, pp, mf and ff, note by note): the harmonic spectrum, the level, the noise between the harmonics, the attack, the pitch.

## Playing

The controls are the engine's inputs above ([MIDI 2.0 and MPE](../midi.md)).

- **pressure** (`pressure`: MIDI 2.0's Poly Pressure, MPE's channel pressure): the breath, from just above the threshold (pp: round, nearly odd harmonics only) to a beating reed (ff: bright).
- **strike** (`pressure_rate` above 0: the Note On's velocity, or a fast rise of the pressure): tonguing. The tongue holds the reed shut against the built-up breath and lets it go, faster for a harder strike, with an accent. No strike is a breath attack.
- **release** (the Note Off): the breath stops and the bore rings out; `sustain` lengthens the release.
- **timbre** (`timbre`: MIDI 2.0's Registered Per-Note Controller 74, MPE's CC74): the embouchure: above 0 firmer (a smaller opening: brighter, a little sharp), below 0 looser (darker, flatter).
- **pitch bend** (`pitch`: MIDI 2.0's per-note pitch, MPE's member-channel bend): the host moves the note; the bore follows.
