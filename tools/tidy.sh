#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Sorbet AI
# SPDX-License-Identifier: Apache-2.0
#
# clang-tidy (.clang-tidy) on the SDK's and the component's sources, from a build's
# compile_commands.json. Usage: tools/tidy.sh [build dir, default build/sdk]
set -euo pipefail
cd "$(dirname "$0")/.."
B="${1:-build/sdk}"
cmake -S sdk -B "$B" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
cmake --build "$B" > /dev/null
RT="${RUN_CLANG_TIDY:-$(command -v run-clang-tidy-15 || command -v run-clang-tidy)}"
"$RT" -quiet -p "$B" $(ls sdk/src/*.cpp sdk/tools/*.cpp sdk/tests/*.cpp "$B"/generated/engine_*.cpp $(ls component/tools/*.cpp component/tests/*.cpp component/conformance/*.cpp 2>/dev/null))
