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
| sustain | `sustain`, 0 .. 1 (3/16 at rest) | CC64 |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0.1 | 100 | 10.2 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.585375 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 0.416054 | continuous |
| `14a92b36` | `pres_hz` | level | 2 | 200 | 14.0604 | continuous |
| `1036ae7e` | `release` | level | 0.005 | 1 | 0.137643 | continuous |
| `c7d91eff` | `rel_pedal` | level | 0 | 3 | 0.4 | continuous |
| `f4ada648` | `p_soft` | breath | 0.8 | 2 | 1.09407 | continuous |
| `e754ff5f` | `g_loud` | breath | 0.3 | 1.4 | 0.770122 | continuous |
| `e68ca377` | `g_reg` | breath | -0.5 | 0.5 | -0.100848 | continuous |
| `b3a3844b` | `p_curve` | breath | 0.3 | 3 | 1.04757 | continuous |
| `657428bf` | `zeta` | reed | 0.05 | 1.2 | 0.260937 | continuous |
| `d63bf2aa` | `zeta_reg` | reed | -1 | 1 | 0.499791 | continuous |
| `e79a6f74` | `reed_hz` | reed | 500 | 6000 | 4749.94 | continuous |
| `47cc72a1` | `reed_q` | reed | 0.05 | 1.2 | 0.537844 | continuous |
| `4d6071e5` | `lay` | reed | 0 | 12 | 6.95634 | continuous |
| `7e8ac38f` | `emb` | reed | -1.5 | 1.5 | 0.4 | continuous |
| `02934c93` | `emb_bend` | reed | -100 | 100 | 25 | continuous |
| `79cb4aa2` | `bore_r` | bore | 3 | 20 | 7.3 | continuous |
| `a7262d15` | `wall` | bore | 0 | 5 | 0.794358 | continuous |
| `9875d581` | `lat_hz` | bore | 300 | 5000 | 3468.1 | continuous |
| `52aa6206` | `lat_q` | bore | 0.3 | 0.7 | 0.350535 | continuous |
| `59d3f5e4` | `reg_pos` | bore | 0.03 | 0.42 | 0.152096 | continuous |
| `9faaca68` | `reg_tau` | bore | 0.1 | 10 | 0.995833 | continuous |
| `fca07064` | `reg_r` | bore | 0 | 2 | 0.960028 | continuous |
| `81d60b50` | `alt_rel` | bore | 19 | 40 | 26.5117 | continuous |
| `53c95844` | `tone_hz` | radiation | 1000 | 20000 | 3077.53 | continuous |
| `0cca7651` | `tone_q` | radiation | 0.5 | 2 | 0.7 | continuous |
| `235d9fc7` | `breath` | air | 0 | 1 | 0.0588474 | continuous |
| `e23d0dfc` | `breath_mod` | air | 0 | 1 | 0.00233541 | continuous |
| `c5316b30` | `breath_hp` | air | 0 | 4000 | 860.735 | continuous |
| `f23b7114` | `wander` | air | 0 | 0.2 | 0.00386448 | continuous |
| `09ceaf46` | `t_noise` | air | 0 | 1 | 0.00055064 | continuous |
| `a5cd3dfb` | `t_len` | air | 0.001 | 0.05 | 0.00561325 | continuous |
| `00c632f3` | `t_gate` | air | 0 | 1 | 0.00988701 | continuous |
| `ecaee291` | `breath_pre` | air | 0 | 4 | 0.2262 | continuous |
| `25a7329d` | `tongue` | attack | 0 | 2 | 1.91798 | continuous |
| `9a6d88a0` | `tongue_noise` | attack | 0 | 2 | 1.14578 | continuous |
| `6f1a0e5f` | `acc_level` | attack | 0 | 1 | 0.566124 | continuous |
| `6f9d5d57` | `acc_decay` | attack | 0.01 | 0.3 | 0.0167341 | continuous |
| `fd47c156` | `tongue_t` | attack | 0.001 | 0.05 | 0.0496621 | continuous |
| `eb47a500` | `tongue_f` | attack | 0 | 4 | 0.998742 | continuous |
| `72d5f1ed` | `slur` | attack | 0 | 1 | 0.00100402 | continuous |
| `68410493` | `tune` | attack | -100 | 100 | 0 | continuous |
| `af32797a` | `pitch_t` | attack | 0.003 | 0.3 | 0.01 | continuous |
| `9a67fb30` | `instrument` | bore | 0 | 1 | 0 | steps |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 35000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 2 s of the last release |
| engine object | 3.07812 KB (3152 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | 100 KB |

<!-- awva_docs: end -->

## How it works

A physical model: nothing sets the pitch but the reed and the bore together. There is no oscillator.

- **Reed:** a single reed on a mouthpiece, one mechanical mode (its resonance and the lip's damping), pushed closed by the pressure difference across it and beating on the lay when blown hard. The flow through the slit is Bernoulli's, solved exactly with the bore's wave every sample (Guillemain, Kergomard & Voinier 2005). Pressures are in units of the reed's closing pressure: the mouth pressure speaks near 1/3, beats from 1/2 and chokes near 1 (Kergomard & Chaigne). Turbulence rides only on the flow's pulsating part, so there is no air before the reed speaks.
- **Bore:** a cylinder, two waveguide sections split at the register hole. Its length is the fingering's: a quarter wave of the note in the chalumeau, three quarters in the clarion with the register hole open (a resistive shunt inertance that bleeds the first resonance away, so the reed plays the twelfth), and a short tube again above the clarion (the altissimo's vented fingerings). Wall losses grow with the square root of the frequency and with the length.
- **Tone holes:** the open tone-hole lattice reflects the low frequencies and passes the high ones out, a fourth-order cutoff (flat below, steep above). The steep cutoff keeps the reed's two transitions per period short; the chalumeau's 2nd harmonic, radiated as the flow at an impedance minimum, depends on it.
- **Radiation:** the flow through the open end, differentiated (a monopole), then a tone low-pass.
- **Tuning:** the bore's length is solved per block (Newton's method) so that the input impedance, register hole and lattice included, peaks at the note together with the reed's own reactance; a slow "player's ear" trims the rest. The same impedance gives the threshold of oscillation, which places the lightest touch.

Every element is passive or bounded, and a per-voice guard resets a voice whose state goes non-finite. The presets' parameters were fitted (CMA-ES, `tools/clarinet/`) to recordings of a B-flat and a bass clarinet (University of Iowa Musical Instrument Samples, pp, mf and ff, note by note): the harmonic spectrum, the level, the noise between the harmonics, the attack, the pitch.

## Playing

- **pressure:** the breath, from just above the threshold (pp: round, nearly odd harmonics only) to a beating reed (ff: bright).
- **strike:** tonguing. The tongue holds the reed shut against the built-up breath and lets it go, faster for a harder strike, with an accent. No strike is a breath attack.
- **release:** the breath stops and the bore rings out; the pedal lengthens the release.
- **tilt forward/back:** the embouchure: forward firmer (a smaller opening: brighter, a little sharp), back looser (darker, flatter).
- **sideways tilt:** a pitch bend (the host moves the note; the bore follows).
