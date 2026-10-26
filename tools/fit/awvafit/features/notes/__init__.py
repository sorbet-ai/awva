# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The per-note analyses the instrument lanes fitted with, kept exactly (their numbers reproduce):

  harmonic_sum   note_feats(x, note): finds the note's own sustain plateau; harmonics 1..10, log
                 bands, HNR per broad band, brightness, rise, overshoot, attack brightness,
                 half-harmonics (the sax)
  windowed       note_feats(x, note, onset, off): the sustain between a known onset and release;
                 harmonics, the noise floor between them, register, ACF pitch, rise, attack windows,
                 the tone's rise from the key (the flute)
  lobes          note_features(x, sr, f0, t_on, t_off): harmonics 1..24 as lobe energies, noise
                 between harmonics, rise 10-90 %, attack centroid and shape, release (the clarinet)

A new instrument picks the one whose sustain model fits its recordings, or uses features.measures.
"""
from . import harmonic_sum, lobes, windowed  # noqa: F401

ANALYSES = {'harmonic_sum': harmonic_sum, 'windowed': windowed, 'lobes': lobes}
