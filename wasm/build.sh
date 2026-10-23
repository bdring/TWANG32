#!/usr/bin/env bash
# Builds build/twang.wasm from the portable core with plain clang (no emscripten).
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p build
CORE=TWANG32/src/core
CLANG="${CLANG:-clang++}"
"$CLANG" --target=wasm32 -O2 -std=c++17 -nostdlib -ffreestanding -fno-exceptions -fno-rtti \
  -fno-threadsafe-statics -fvisibility=hidden -DTW_SYNTH_MAX_RATE=48000 \
  -Wl,--no-entry -Wl,--export-dynamic -Wl,--export=__wasm_call_ctors -Wl,--strip-all \
  -Wl,--initial-memory=4194304 -Wl,-z,stack-size=262144 \
  wasm/wasm_api.cpp wasm/wasm_shim.cpp \
  $CORE/tw_json.cpp $CORE/tw_level.cpp $CORE/tw_game.cpp $CORE/tw_gen.cpp \
  $CORE/tw_synth.cpp $CORE/tw_music.cpp $CORE/tw_sfx.cpp \
  -o build/twang.wasm
ls -l build/twang.wasm
