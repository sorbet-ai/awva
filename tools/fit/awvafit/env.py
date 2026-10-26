# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""Where the toolkit works and what it runs: from the environment only, never a path in the code.

  AWVA_FIT_WORK  the work directory: inputs, renders, features, fits, packs (required to write)
  AWVA_RENDER    the renderer, awva_render (sdk/tools/awva_render.cpp); default: awva_render on the PATH
  AWVA_REFS      the recordings' cache (sources.py fills it, one folder per source)
  AWVA_CAPTURES  optional: engine controls captured from an embedded host's key path (<input>.ctl):
                 the exact path of numbers measured on that host
  AWVA_REC       the recorded key streams (default: tools/harness/rec of this repository)
  FFMPEG         ffmpeg, for the decoders that use it (default: ffmpeg on the PATH)
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))  # the repository


class EnvError(RuntimeError):
    pass


def _need(name, what):
    v = os.environ.get(name)
    if not v:
        raise EnvError(f'set {name}: {what} (tools/fit/README.md)')
    return v


def work_root():
    return _need('AWVA_FIT_WORK', 'the work directory')


def work(*parts):
    """a file in the work directory (its folder is made)"""
    p = os.path.join(work_root(), *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


def workdir(*parts):
    """a folder in the work directory (made)"""
    p = os.path.join(work_root(), *parts)
    os.makedirs(p, exist_ok=True)
    return p


def renderer():
    v = os.environ.get('AWVA_RENDER')
    if v:
        return v
    w = shutil.which('awva_render')
    if not w:
        raise EnvError('set AWVA_RENDER: the renderer, awva_render (cmake --build build/sdk --target awva_render)')
    return w


def refs_root():
    return _need('AWVA_REFS', 'the recordings cache')


def captures():
    return os.environ.get('AWVA_CAPTURES') or None


def rec_dir():
    return os.environ.get('AWVA_REC') or os.path.join(ROOT, 'tools', 'harness', 'rec')


def ffmpeg():
    return os.environ.get('FFMPEG', 'ffmpeg')


def die(msg):
    sys.exit(f'awvafit: {msg}')
