#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build/rg-nano-host-tests"
mkdir -p "$BUILD"

cc -std=gnu99 -O2 -Wall -Wextra -Werror \
  -DPLATFORM_RG_NANO \
    -iquote "$ROOT/include" \
  "$ROOT/tests/rg_nano_host_test.c" \
  "$ROOT/src/platform/secondary_panel_render.c" \
  "$ROOT/src/platform/sha1.c" \
  "$ROOT/src/platform/rg_nano_asset_gate.c" \
  -ldl -o "$BUILD/rg_nano_host_test"

"$BUILD/rg_nano_host_test"
python3 -m py_compile \
  "$ROOT/tools/rg_nano/make_asset_holes.py" \
  "$ROOT/tools/rg_nano/make_icon.py"
python3 "$ROOT/tools/rg_nano/make_icon.py" "$BUILD/pokemon-emerald-nano.png"
file "$BUILD/pokemon-emerald-nano.png"
