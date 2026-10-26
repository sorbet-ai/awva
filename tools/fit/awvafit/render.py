# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""awva_render from Python: the one renderer of every engine, preset and input.

  r = Renderer()                                   # AWVA_RENDER, or awva_render on the PATH
  r.params('trumpet', 2)                           # {name: Param(group, lo, hi, value)} of the preset
  res = r.render('trumpet', 2, ctl='in.ctl', out='o.f32', sets={'gain': 12.0}, manifest='o.json')
  res.x, res.manifest['hash']

An input is one of rec= (a key stream CSV, through the key path), ctl= (engine controls), midi= (a
Standard MIDI File, MPE or plain: up-converted to MIDI 2.0) or perf= (a scripted performance).
The settings: rate, voices (nv), seconds or tail, settle (silence before, not written: the glide of
the overrides arrives), transpose, notes (each new press takes the next), fx. The block is always 32.

Every render writes a JSON manifest (engine, preset, the parameters as set, the input's identity,
the settings, the outputs and the FNV-1a hash of the samples). Renders are deterministic: the engines
seed their noise in init(), so the hash is the render's identity.
"""
import json
import os
import subprocess
from dataclasses import dataclass, field

import numpy as np

from . import env

BLOCK = 32
CTL_VOICES = 10
CTL_WIDTH = CTL_VOICES * 5 + 8


class RenderError(RuntimeError):
    pass


@dataclass
class Param:
    group: str
    lo: float
    hi: float
    value: float


@dataclass
class Result:
    out: str
    ctl_out: str = None
    manifest: dict = field(default_factory=dict)
    stderr: str = ''

    @property
    def x(self):
        return np.fromfile(self.out, np.float32)

    @property
    def finite(self):
        return self.manifest.get('nonfinite', 0) == 0


def fmt_value(v):
    """a parameter value on the command line: Python's shortest repr (parsed back to the same double)"""
    return repr(float(v)) if not isinstance(v, str) else v


class Renderer:
    def __init__(self, exe=None):
        self.exe = exe or env.renderer()

    def run(self, args, check=True):
        r = subprocess.run([self.exe] + [str(a) for a in args], capture_output=True, text=True)
        if check and r.returncode != 0:
            raise RenderError(f'awva_render {" ".join(str(a) for a in args)}: exit {r.returncode}\n{r.stderr.strip()}')
        return r

    def list(self):
        """{engine: [preset names]}"""
        out, eng = {}, None
        for ln in self.run(['--list']).stdout.splitlines():
            if ln.startswith('engine '):
                eng = ln.split()[1]
                out[eng] = []
            elif ln.startswith('  preset ') and eng:
                out[eng].append(ln.split(None, 2)[2])
        return out

    def params(self, engine, preset=None, sets=None):
        """the live parameters of an engine's preset (after sets): {name: Param}"""
        a = ['--engine', engine] + (['--preset', str(preset)] if preset is not None else [])
        for k, v in (sets or {}).items():
            a += ['--set', f'{k}={fmt_value(v)}']
        t = {}
        for ln in self.run(a + ['--params']).stdout.splitlines():
            f = ln.split()
            t[f[0]] = Param(f[1], float(f[2]), float(f[3]), float(f[4]))
        return t

    def render(self, engine, preset=None, *, out, rec=None, ctl=None, midi=None, perf=None, sets=None, transpose=0.0,
               notes=None, settle=0.0, tail=0.0, seconds=None, nv=None, rate=None, fx=False, ctl_out=None, wav=None,
               manifest=None, check=False, max_peak=None):
        """one render; raises RenderError on a failure (exit 2: bad arguments or input)"""
        a = ['--engine', engine]
        if preset is not None:
            a += ['--preset', str(preset)]
        inputs = [(k, v) for k, v in (('--rec', rec), ('--ctl', ctl), ('--midi', midi), ('--perf', perf)) if v is not None]
        if len(inputs) > 1:
            raise RenderError('one input only')
        for k, v in inputs:
            a += [k, v]
        for k, v in (sets or {}).items():
            a += ['--set', f'{k}={fmt_value(v)}']
        if transpose:
            a += ['--transpose', repr(float(transpose))]
        if notes is not None:
            a += ['--notes', ','.join(str(n) for n in notes)]
        if settle:
            a += ['--settle', repr(float(settle))]
        if tail:
            a += ['--tail', repr(float(tail))]
        if seconds is not None:
            a += ['--seconds', repr(float(seconds))]
        if nv is not None:
            a += ['--nv', str(nv)]
        if rate is not None:
            a += ['--rate', str(rate)]
        if fx:
            a += ['--fx']
        if check:
            a += ['--check'] + (['--max-peak', str(max_peak)] if max_peak is not None else [])
        a += ['--out', out]
        if ctl_out:
            a += ['--ctl-out', ctl_out]
        if wav:
            a += ['--wav', wav]
        manifest = manifest or out + '.json'  # (kept next to the output: the record of what it is)
        if os.path.exists(manifest):
            os.remove(manifest)
        a += ['--manifest', manifest]
        r = self.run(a, check=False)
        if r.returncode == 2 or not os.path.exists(manifest):
            raise RenderError(f'awva_render {" ".join(a)}: exit {r.returncode}\n{r.stderr.strip()}')
        with open(manifest) as f:
            m = json.load(f)
        m['exit'] = r.returncode
        return Result(out=out, ctl_out=ctl_out, manifest=m, stderr=r.stderr)


def read_ctl(path):
    """engine controls (.ctl): (voices [blocks, 10, 5] = note, pres, vpres, x, y; globals [blocks, 8])"""
    c = np.fromfile(path, np.float32).reshape(-1, CTL_WIDTH)
    return c[:, :50].reshape(-1, CTL_VOICES, 5), c[:, 50:]


def voice_controls(path):
    """the voices part of a .ctl, [blocks, 10, 5] (the layout every feature reads)"""
    return read_ctl(path)[0]
