#!/usr/bin/env bash
# Builds the portable core natively and runs the test suite.
# Usage: tests/run.sh [output-dir-for-wav-files]
set -euo pipefail
cd "$(dirname "$0")/.."
OUT="${1:-build/test-audio}"
mkdir -p build "$OUT"
CORE=TWANG32/src/core
g++ -std=c++17 -O1 -g -Wall -Wextra -Wno-unused-parameter \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  tests/test_main.cpp $CORE/tw_json.cpp $CORE/tw_level.cpp $CORE/tw_game.cpp $CORE/tw_gen.cpp \
  $CORE/tw_synth.cpp $CORE/tw_music.cpp $CORE/tw_sfx.cpp \
  -o build/twang_tests
./build/twang_tests "$OUT"
