#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# clang-format 15 (.clang-format) on the engines, the SDK, the component and the harness header.
# Usage: tools/format.sh [--check]   (--check: list what would change, exit 1)
#   CLANG_FORMAT   the clang-format to run (default clang-format-15, else clang-format; it must be 15:
#                  other versions format differently)
set -euo pipefail
cd "$(dirname "$0")/.."
CF="${CLANG_FORMAT:-$(command -v clang-format-15 || command -v clang-format)}"
"$CF" --version | grep -q "version 15\." || { echo "format.sh: $CF is not clang-format 15" >&2; exit 2; }
FILES=$(ls dsp/include/awva/fx.h dsp/include/awva/voice_*.h dsp/include/awva/controls.h dsp/include/awva/dspmath.h \
  dsp/include/awva/params.h dsp/include/awva/svf.h dsp/include/awva/engine.h dsp/include/awva/adapter.h \
  dsp/include/awva/engine_set.h dsp/include/awva/midi_in.h dsp/include/awva/ump.h dsp/include/awva/legato.h dsp/include/awva/preset.h \
  dsp/include/awva/engines/*.h dsp/include/awva/effects/*.h \
  sdk/include/awva_sdk/*.h sdk/src/*.h sdk/src/*.cpp sdk/tools/*.cpp sdk/tests/*.cpp tools/harness/harness_util.h \
  $(ls component/include/nsc/*.h component/tools/*.cpp component/tests/*.cpp examples/*/*.c examples/*/*.cpp 2>/dev/null))
if [ "${1:-}" = --check ]; then
  "$CF" --dry-run -Werror $FILES
else
  "$CF" -i $FILES
fi
