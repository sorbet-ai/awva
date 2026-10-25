<!-- SPDX-FileCopyrightText: 2026 Sorbet AI -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
# The flute's fitting tools

How the flute engine's (`dsp/include/awva/voice_flute.h`) parameters were fitted: CMA-ES on the
engine's physics, against recordings of real flutes, note by note. Python 3 with numpy, scipy and
`cma`; run with `python -I`. They work in `FLUTE_WORK` (else the current directory), never here.

| | |
|---|---|
| `ref_segment.py` | the University of Iowa MIS chromatic scales (AIFF, pp/mf/ff) cut into notes, each note's pitch checked, resampled to 48 kHz: `ref/<inst>_<dyn>_<note>.f32`, `ref/index.json` |
| `ref_feats.py` | every recorded note's features: `ref/feats.pkl` |
| `feat.py` | the features of one note, the same code for a recording and the model: harmonics, level, the noise between the harmonics, the attack windows, the rise, the tone's rise, pitch, register |
| `objective.py` | the distance of the model from the recordings, per component |
| `ctl.py` | engine-level control files for scripted notes, the key built as the scanner reports it |
| `evalm.py` | render the model on an instrument's fit set and compare (`--notes` per note) |
| `fit.py` | CMA-ES of the physics (the sustain and the expression) |
| `fit_atk.py` | CMA-ES of the attack alone: the tone's rise from the key on recorded key takes (`takes.py`) and against the recordings' attacks |
| `probe.py`, `sweep.py` | a few notes' features; every note at four pressures, flagging those that don't speak |
| `bake.py` | fitted parameters into `voice_flute.h`'s presets |
| `replay.cpp` | the renderer they use, `bin/<tag>/replay` (build line in its header) |

The recordings are not part of this repository: the University of Iowa Musical Instrument Samples
(theremin.music.uiowa.edu/MIS), flute (non-vibrato) and alto flute, are "freely available ... and
may be used for any projects, without restrictions". The key takes are the harness's recordings
(`tools/harness/rec/`) played through the virtual board and captured as engine controls.
