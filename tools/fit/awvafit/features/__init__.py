# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""What the toolkit measures (docs/fitting.md lists every feature and where it is used).

  frames     frame features of a render on the controls it got (10 ms hops): level, log-mel spectra
             (short and long windows), brightness, centroid, pitch, periodicity, period doubling;
             note events; the level-dependent spectral tilt. Model against model on the same controls.
  measures   the standard per-note measures, for any recording or render: harmonic amplitudes (the
             spectral envelope), harmonic-to-noise ratio, attack (key to first sound, rise), the
             sustained pitch track and its median in cents, settle time, brightness, spectral tilt,
             dynamics curves
  loudness   ITU-R BS.1770-4 loudness (LUFS, K-weighted, gated) and the gain that matches two signals
  notes      the three per-note analyses the instrument lanes fitted with, kept exactly (an
             instrument config names one): 'harmonic_sum' (the sax), 'windowed' (the flute),
             'lobes' (the clarinet)
"""
