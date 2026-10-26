# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""awvafit: render awva engines and measure them against recordings (tools/fit/README.md).

  env          where things are: the work directory, the renderer, the recordings' cache (environment)
  render       awva_render, the one renderer: any engine and preset, any input, overrides, a manifest
  keys         key streams as a keyboard's scanner reports them (CSV), the default key layout
  controls     engine-level controls (.ctl): read, write, synthesise; the note events in them
  phrases      the standard expression phrases (sweeps, crescendo, staccato, slurs, ...)
  sources      the recordings: one registry with source, licence, citation and hash per file; fetching
  labels       note names, dynamics, and the file-name conventions of each source
  segment      recordings of note sequences cut into notes (onset, sustain, release)
  audio        reading and writing audio (AIFF, WAV, FLAC, float32), resampling
  features     what is measured: spectra, harmonics, noise, pitch, attack, loudness, frame features
  objective    the objective framework: terms, scales and weights from an instrument's config
  fit          the CMA-ES driver: normalised spaces, checkpoints, hold-out
  robust       every in-range note at three pressures speaks
  bake         fitted parameters into an engine's preset tables
  pack         listening packs: the same phrases per engine version, references loudness-matched
  instruments  the instruments' configurations (brass family, sax, flute, clarinet)
"""
__version__ = '1.0'
