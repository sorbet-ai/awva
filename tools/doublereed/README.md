<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The double reed's fitting and diagnosis tools

These tools fitted the double reed engine's (`dsp/include/awva/voice_doublereed.h`) presets, and they measure what a listener might hear as "janky". They need Python 3 with numpy, scipy and `cma`; run them with `python -I`. They work in `DR_WORK` (else the current directory), never in this directory.

| | |
|---|---|
| `fetch_samples.py` | downloads the recordings into `DR_WORK/samples`: the Iowa MIS oboe and bassoon, and the VSCO-2 CE oboe and bassoon (for validation) |
| `ref_segment.py`, `ref_feats.py` | cut the Iowa scales into notes (`ref/`, with each pitch checked); every note's features (`ref/feats.pkl`) |
| `feat.py` | one note's features, the same code for a recording and the model: harmonics, level, noise, the attack, the tone's rise, pitch, register, and the tone's life (pitch and level wobble, the attack's pitch excursion) |
| `objective.py`, `evalm.py` | the distance from the recordings, and the model rendered on a fit set |
| `fit_dr.py` | CMA-ES of a preset (below) |
| `diag.py`, `ramp.py`, `takediag.py`, `refdiag.py` | the jank diagnosis: notes at pressures and strikes, pressure ramps, the recorded key takes, the recordings' own life |
| `robust.py`, `takes.py`, `probe.py`, `ctl.py` | every note speaking in its register; the takes' rise from the key; scripted notes |
| `bake_dr.py` | fitted parameters into the presets |
| `replay.cpp` | the renderer they use, `bin/<tag>/replay` (the build line is in its header) |

`fit_dr.py` fits in the Track B way, not on the static spectrum alone. Its objective has these parts:
- the sustain: harmonics, noise, the level across notes and dynamics, and the timbre's response to the dynamic;
- the attack: its spectra, the tone's rise from the key, and its pitch excursion compared with the recording's;
- the tone's life: its pitch and level wobble compared with the recording's;
- pitch and register;
- the recorded takes' rise from the key: hard strikes within 50 ms, soft within 120 ms;
- every in-range note at four pressures speaking in its register;
- a dynamic that keeps growing to the firmest press.

The steady breath noise (`hiss`) is fixed at 0.

The recordings are not part of this repository. The University of Iowa Musical Instrument Samples (theremin.music.uiowa.edu/MIS) are "freely available ... and may be used for any projects, without restrictions". VSCO-2 Community Edition is CC0 1.0. The key takes are the harness's recordings (`tools/harness/rec/`), played through the virtual board and captured as engine controls.
