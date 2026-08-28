#!/usr/bin/env bash
# Build the fullscreen240 experiment and package it as its own OPK.
#
# Run from WSL Ubuntu with a clean PATH:
#   wsl.exe -d Ubuntu bash -c 'cd /mnt/c/Programmering/SBC/RG_Nano/Pokemon_Emerald_RG_Nano && experiments/fullscreen240/build.sh'
#
# This shares build/rg-nano with the stock build. That is deliberate -- a
# separate build dir means recompiling the whole game plus the 420 generated
# songs -- but it does mean the ELF in build/rg-nano is the experiment's after
# this runs. The stock OPKs are archived in dist/dev and dist/release, and the
# baseline unstripped ELF (for symbolicating crashes from the deployed release
# build) is kept next to this script as baseline-unstripped.elf.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
BUILD_DIR="$ROOT/build/rg-nano"
STAGE="$BUILD_DIR/opk-root-fs240"
CROSS_COMPILE="${CROSS_COMPILE:-arm-funkey-linux-musleabihf-}"
export PATH="$HOME/funkey-sdk-2.3.0/bin:$PATH"

cd "$ROOT"

# make cannot see that -DRG_NANO_FULLSCREEN changed, so force every object that
# reads the flag (directly or through platform.h's geometry macros) to rebuild.
touch src/platform/gba_easy_draw.c \
      src/platform/rg_nano.c \
      src/event_object_movement.c \
      src/menu.c \
      src/overworld.c \
      experiments/fullscreen240/fullscreen240.c

make -f Makefile_rg_nano RG_NANO_FULLSCREEN=1 "$@"

rm -rf "$STAGE"
mkdir -p "$STAGE" "$HERE/dist"
cp "$BUILD_DIR/pokemon-emerald-nano" "$STAGE/pokemon-emerald-nano"
"${CROSS_COMPILE}strip" --strip-unneeded "$STAGE/pokemon-emerald-nano"
cp "$HERE/packaging/run.sh" "$STAGE/run.sh"
cp "$HERE/packaging/pokemon-emerald-fs240.funkey-s.desktop" "$STAGE/"
python3 "$ROOT/tools/rg_nano/make_icon.py" "$STAGE/pokemon-emerald-fs240.png"
chmod 0755 "$STAGE/pokemon-emerald-nano" "$STAGE/run.sh"

OUTPUT="$HERE/dist/PokemonEmeraldFS240_funkey-s.opk"
rm -f "$OUTPUT"
mksquashfs "$STAGE" "$OUTPUT" -all-root -noappend -no-exports -no-xattrs -comp gzip >/dev/null
echo "Created $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
