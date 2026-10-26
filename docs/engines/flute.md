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
| pressure | `pres`, 0 released .. 1 a firm press (up to ~1.5) | MPE: channel pressure of the note's channel; MIDI 1.0: poly or channel pressure; MIDI 2.0: Poly Pressure (full scale = 1) |
| strike | `vpres`, the pressure's velocity: > 0 while the note is struck, < 0 while it is released | note-on velocity (a strike that fades), note-off velocity (the release) |
| tilt y | `but_y`, forward/back tilt -1 .. 1 | MPE: CC74 of the note's channel; MIDI 1.0: CC74 (64 = 0) or CC1; MIDI 2.0: CC74 (bipolar) |
| shake | `acc_abs`, the accelerometer's magnitude in g (1 at rest) | the instrument's motion sensor (not from MIDI) |
| pedal | `pedal`, 0 .. 1 (3/16 at rest) | CC64 |

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

Default: the value after `init()` (preset 0). The id is the component ABI's (FNV-1a 32 of the name).

| id | name | group | min | max | default | kind |
|---|---|---|---|---|---|---|
| `1b5426fe` | `gain` | level | 0 | 100 | 12.9 | continuous |
| `75245e0b` | `pres_lin` | level | 0 | 1 | 0.35 | continuous |
| `1b755ad8` | `pres_sq` | level | 0 | 1.5 | 0.65 | continuous |
| `14a92b36` | `pres_hz` | level | 2 | 200 | 36.5997 | continuous |
| `1036ae7e` | `release` | level | 0.005 | 1 | 0.04 | continuous |
| `c7d91eff` | `rel_pedal` | level | 0 | 3 | 0.4 | continuous |
| `f4ada648` | `p_soft` | breath | 10 | 3000 | 233.295 | continuous |
| `ac9cdaa8` | `p_loud` | breath | 20 | 6000 | 1920.16 | continuous |
| `45481f5a` | `p_reg` | breath | -0.5 | 2 | 0.773037 | continuous |
| `b3a3844b` | `p_curve` | breath | 0.3 | 3 | 1.21055 | continuous |
| `d0dcf09b` | `theta` | jet | 0.03 | 2 | 0.473292 | continuous |
| `fd1554fe` | `theta_reg` | jet | -1 | 1 | 0.009298 | continuous |
| `3554104a` | `w_track` | jet | 0 | 1 | 0.998653 | continuous |
| `bcb6b31b` | `jet_h` | jet | 0.2 | 3 | 0.554691 | continuous |
| `6933e4f0` | `h_loud` | jet | -1 | 2 | -0.227037 | continuous |
| `a9b69532` | `jet_w` | jet | 2 | 20 | 17.953 | continuous |
| `ca3e3f27` | `g_soft` | jet | 0.3 | 20 | 1.05304 | continuous |
| `2e3dc25e` | `amp_loud` | jet | -3 | 3 | 0.05793 | continuous |
| `8b5ffe8c` | `y0` | jet | -5 | 5 | 0.207421 | continuous |
| `da682d3a` | `y_tilt` | jet | -3 | 3 | 0.3 | continuous |
| `70626fd6` | `jet_st` | jet | 0.01 | 1 | 0.496387 | continuous |
| `d8760e19` | `vortex` | jet | 0.1 | 5 | 1.14688 | continuous |
| `89e67032` | `bore_mm` | bore | 2 | 20 | 9.5 | continuous |
| `5b83d2e2` | `bore_decay` | bore | 0.002 | 0.5 | 0.023309 | continuous |
| `d2924a86` | `bore_reg` | bore | -1 | 2 | 0.848711 | continuous |
| `ba4d544a` | `cut_hz` | bore | 200 | 12000 | 5730.56 | continuous |
| `a7262d15` | `wall` | bore | 0.5 | 50 | 7.21246 | continuous |
| `506051f5` | `disp` | bore | -0.9 | 0.9 | -0.024968 | continuous |
| `bacd0480` | `cover` | bore | -200 | 200 | -30 | continuous |
| `0dabae5f` | `cover_hz` | bore | -2 | 2 | -0.3 | continuous |
| `7e7d21e6` | `low_note` | fingering | 24 | 96 | 60 | continuous |
| `5dedc587` | `overblow` | fingering | 0 | 1 | 1 | toggle |
| `8c08f22a` | `range_lo` | fingering | 24 | 96 | 59 | continuous |
| `8e11efac` | `range_hi` | fingering | 48 | 120 | 98 | continuous |
| `a87d86ad` | `g_out` | fingering | 1 | 6 | 3 | continuous |
| `73670f0a` | `out_db` | fingering | -24 | 6 | -11 | continuous |
| `92595769` | `ob_amt` | jet | 0 | 1 | 0 | continuous |
| `e59e03a5` | `ob_thr` | jet | 0 | 1.5 | 0.7 | continuous |
| `742581dc` | `mouth` | radiation | 0 | 4 | 1 | continuous |
| `6a8e75aa` | `end` | radiation | 0 | 4 | 0.013091 | continuous |
| `e24d7b34` | `dipole` | radiation | 0 | 4 | 1.4672 | continuous |
| `53c95844` | `tone_hz` | radiation | 500 | 16000 | 1951.39 | continuous |
| `0cca7651` | `tone_q` | radiation | 0.5 | 2 | 0.500948 | continuous |
| `854bbf96` | `turb` | air | 0 | 2 | 0.296501 | continuous |
| `99df0ff9` | `turb_hz` | air | 100 | 12000 | 206.416 | continuous |
| `4aac31a8` | `hiss` | air | 0 | 1 | 0.005222 | continuous |
| `d6d799d2` | `hiss_st` | air | 0.02 | 1 | 0.172695 | continuous |
| `09ceaf46` | `t_noise` | air | 0 | 3 | 1.22976 | continuous |
| `25a7329d` | `tongue` | attack | 0 | 2 | 0.516874 | continuous |
| `6f1a0e5f` | `acc_level` | attack | 0 | 1.5 | 0.050248 | continuous |
| `6f9d5d57` | `acc_decay` | attack | 0.005 | 0.3 | 0.079499 | continuous |
| `fd47c156` | `tongue_t` | attack | 0.0005 | 0.1 | 0.003666 | continuous |
| `48e3b5fa` | `att_gain` | attack | 0 | 40 | 32.1447 | continuous |
| `fd2c475f` | `att_t` | attack | 0.005 | 0.15 | 0.032721 | continuous |
| `835672b9` | `att_soft` | attack | 0 | 1 | 0.622556 | continuous |
| `72d5f1ed` | `slur` | attack | 0 | 1 | 0.1 | continuous |
| `af32797a` | `pitch_t` | attack | 0.002 | 1 | 0.03 | continuous |
| `6a122da6` | `vib` | mod | 0 | 2 | 0.5 | continuous |
| `9e2c50e9` | `vib_hz` | mod | 2 | 9 | 5.2 | continuous |
| `dbdd06c6` | `tongue_nz` | attack | 0 | 2 | 0.516874 | continuous |
| `e5f99898` | `tongue_rt` | attack | 0 | 2 | 0.516874 | continuous |
| `69fb3ac7` | `tongue_sus` | attack | 0 | 2 | 0.516874 | continuous |

## Budget and memory

| | |
|---|---|
| voices | up to 10 |
| channels | 1 (mono) |
| CPU | 37000 QEMU instructions per 32-sample block, worst preset, 10 voices, the default performance |
| tail | silent and idle within 2 s of the last release |
| engine object | 3.07812 KB (3152 bytes) (on a 64-bit host) |
| FastMemory | none |
| LargeMemory | 120 KB |

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

The presets were fitted (CMA-ES) to recordings. The flute and the alto flute were fitted to the University of Iowa MIS recordings (non-vibrato, pp/mf/ff, every note): the steady harmonics, the level across notes and dynamics, the noise, the attack and its rise, the timbre's change with the dynamic, the tuning and the register. The piccolo is the flute's player on the piccolo's geometry (an octave higher, a 5.4 mm bore). Its jet, bore losses, radiation, noise and attack were fitted to the VSCO-2 Community Edition piccolo (CC0). No recording is in this repository; `tools/flute/` and `tools/fit/` (`awvafit flute`) fetch the recordings and rerun the fits.

## Playing

- **pressure:** the breath. It sets the dynamics and the brightness (the edge saturates) and the breathiness. At the extremes the note overblows or falls back.
- **strike:** tonguing. The tongue releases the breath, faster for a harder strike, with the pressure built up behind it (an accent) and a "t" of turbulence: the chiff. The attack's jet is released fast and thin, so the tone speaks in tens of milliseconds. No strike is a breath attack.
- **release:** the breath stops and the pipe rings out. The pedal lengthens the release.
- **tilt forward/back:** the jet angle and the cover of the embouchure hole. Forward rolls in (darker, a little flatter); back rolls out.
- **sideways tilt:** a pitch bend (the host moves the note; the pipe follows).
- **shake:** breath vibrato.
- **`ob_amt`** (0 by default): overblowing. Above `ob_thr` of the breath, the player stops shortening the lip-to-edge distance as the jet speeds up. The jet's travel time falls, and the next mode of the pipe takes over, with the jet's hysteresis.
