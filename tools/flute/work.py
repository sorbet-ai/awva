# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
"""The working directory of the flute's fitting tools: FLUTE_WORK, else the current directory.
It holds ref/ (the recordings' notes and features), ctl/ (generated control files), takes/ (key
recordings as engine controls), bin/<tag>/replay (replay.cpp built), tmp/ and fits/."""
import os

ROOT = os.path.abspath(os.environ.get('FLUTE_WORK', os.getcwd()))


def replay(tag):
    p = os.path.join(ROOT, 'bin', tag, 'replay')
    return p + '.exe' if os.name == 'nt' else p
