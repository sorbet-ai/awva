# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The objective framework: one distance, lower is nearer, the same arithmetic for every instrument.

A comparison (awvafit.terms) measures named components, each in its own unit (dB, cents, a fraction
of notes, ...). The instrument's config gives every component it uses a scale (how much is "clearly
audible") and a weight (how much it matters), and the objective is

    total = sum over the components, in the config's order, of  weight * component / scale

A component a config doesn't list is measured but not counted. Per-file (or per-input) components are
averaged first (combine), then totalled: the same as the lanes' fits.

  obj = Objective([('spec', 3.0, 2.0), ('harm', 4.0, 1.0), ...])     (name, scale, weight)
  obj.total(comps); obj.combine({input: comps}); obj.fmt(comps)
  obj.failed()     every component at 10 x its scale: a render that blew up

Comparisons and their components (docs/fitting.md has the full list):
  terms.framewise   a model render against a reference render on the same controls, frame by frame
  terms.notewise    model notes against recorded notes, note x dynamic, with one level offset
"""
import numpy as np


class Objective:
    def __init__(self, terms):
        """terms: [(name, scale, weight)] in the order they are summed"""
        self.terms = [(str(n), float(s), float(w)) for n, s, w in terms]
        self.scale = {n: s for n, s, _ in self.terms}
        self.weight = {n: w for n, _, w in self.terms}

    @property
    def names(self):
        return [n for n, _, _ in self.terms]

    def with_terms(self, extra):
        """a copy with more terms after these (e.g. the trombone's tilt)"""
        return Objective(self.terms + list(extra))

    def total(self, comps):
        return float(sum(self.weight[k] * comps[k] / self.scale[k] for k in self.scale))

    def combine(self, per_file):
        """the mean of every component over the files, and their total"""
        avg = {k: float(np.mean([c[k] for c in per_file.values()])) for k in self.scale}
        avg['total'] = self.total(avg)
        return avg

    def failed(self):
        return {k: 10 * s for k, s in self.scale.items()}

    def contributions(self, comps):
        """each component's share of the total, largest first: [(name, weight * value / scale)]"""
        return sorted(((k, self.weight[k] * comps[k] / self.scale[k]) for k in self.scale), key=lambda kv: -kv[1])

    def fmt(self, comps):
        return ' '.join(f"{k}={comps[k]:.3g}" for k in self.names + (['total'] if 'total' in comps else []) if k in comps)

    def to_dict(self):
        return [dict(name=n, scale=s, weight=w) for n, s, w in self.terms]
