#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# The clarinet's fit tools (voice_clarinet.h, its presets fitted to the University of Iowa MIS clarinet
# and bass clarinet scales). Builds the renderers into $CLARINET_WORK/bin/<tag>:
#   clar_render  single notes on synthetic key controls (model.py, fit.py)
#   imp          the bore's input impedance (CLARINET_TEST_HOOK): imp.py
#   probe        the mouthpiece pressure, flow and reed on a held note: probe.py
# Usage: [CXX=clang++] [CLARINET_WORK=<dir>] tools/clarinet/build.sh [tag]   (Windows: from a Visual
# Studio developer shell). Then, with the recordings under <samples>/{bb_clarinet,bass_clarinet}/:
#   python -I tools/clarinet/refs.py <samples> $CLARINET_WORK/refs.pkl
#   python -I tools/clarinet/fit.py <tag> <preset 0|1> <run> --start best.json --workers 3
#   python -I tools/clarinet/bake.py dsp/include/awva/voice_clarinet.h <bb best.json> <bass best.json> [gains]
# (numpy, scipy, cma)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WORK="${CLARINET_WORK:-$PWD/clarinet_work}"
TAG="${1:-cur}"
BIN="$WORK/bin/$TAG"
mkdir -p "$BIN"
EXE=""
case "$(uname -s)" in MINGW* | MSYS* | CYGWIN*) EXE=".exe"; FLAGS_OS=(-D_CRT_SECURE_NO_WARNINGS) ;; *) FLAGS_OS=() ;; esac
CXX="${CXX:-clang++}"
FLAGS=(-O2 -ffp-contract=off -fno-strict-aliasing -std=c++20 -Wall -Wno-unused-function "${FLAGS_OS[@]}" -I "$REPO/dsp/include/awva")
for t in clar_render imp probe; do
  "$CXX" "${FLAGS[@]}" "$HERE/$t.cpp" -o "$BIN/$t$EXE"
done
echo "built $BIN"
