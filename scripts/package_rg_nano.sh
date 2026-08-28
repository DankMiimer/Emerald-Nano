#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build/rg-nano}"
DIST_DIR="${DIST_DIR:-$ROOT/dist}"
STAGE="$BUILD_DIR/opk-root"
UNSTRIPPED="$BUILD_DIR/pokemon-emerald-nano"
HOLED="$BUILD_DIR/pokemon-emerald-nano.holed"
MANIFEST="$BUILD_DIR/asset_manifest.bin"
ROM="${BASEROM:-$ROOT/baserom.gba}"
CROSS_COMPILE="${CROSS_COMPILE:-arm-funkey-linux-musleabihf-}"
HOST_PYTHON="${HOST_PYTHON:-/usr/bin/python3}"
DEVELOPMENT_BUILD="${DEVELOPMENT_BUILD:-0}"

if [[ ! -x "$UNSTRIPPED" ]]; then
    echo "Missing $UNSTRIPPED; run make -f Makefile_rg_nano first" >&2
    exit 1
fi
if [[ "$DEVELOPMENT_BUILD" != 1 && ! -f "$ROM" ]]; then
    echo "Missing user ROM: $ROM" >&2
    echo "Set BASEROM=/path/to/verified/baserom.gba" >&2
    exit 1
fi
if [[ "$DEVELOPMENT_BUILD" != 1 ]]; then
    "$HOST_PYTHON" -c 'import elftools' 2>/dev/null || {
        echo "pyelftools is required: python3 -m pip install --user pyelftools" >&2
        exit 1
    }
fi
command -v mksquashfs >/dev/null || {
    echo "mksquashfs is required (Ubuntu package: squashfs-tools)" >&2
    exit 1
}

rm -rf "$STAGE"
mkdir -p "$STAGE" "$DIST_DIR"

if [[ "$DEVELOPMENT_BUILD" == 1 ]]; then
    cp "$UNSTRIPPED" "$HOLED"
    rm -f "$MANIFEST"
else
    "$HOST_PYTHON" "$ROOT/tools/rg_nano/make_asset_holes.py" build \
        "$UNSTRIPPED" "$ROM" "$UNSTRIPPED" "$HOLED" "$MANIFEST"
fi
"${CROSS_COMPILE}strip" --strip-unneeded "$HOLED"

cp "$HOLED" "$STAGE/pokemon-emerald-nano"
if [[ "$DEVELOPMENT_BUILD" != 1 ]]; then
    cp "$MANIFEST" "$STAGE/asset_manifest.bin"
fi
cp "$ROOT/packaging/rg-nano/run.sh" "$STAGE/run.sh"
cp "$ROOT/packaging/rg-nano/pokemon-emerald-nano.funkey-s.desktop" "$STAGE/"
"$HOST_PYTHON" "$ROOT/tools/rg_nano/make_icon.py" "$STAGE/pokemon-emerald-nano.png"
chmod 0755 "$STAGE/pokemon-emerald-nano" "$STAGE/run.sh"

if find "$STAGE" -type f -name 'baserom.gba' | grep -q .; then
    echo "Refusing to package baserom.gba" >&2
    exit 1
fi
if [[ "$DEVELOPMENT_BUILD" != 1 ]] && ! cmp -s <(dd if="$MANIFEST" bs=1 count=20 status=none) \
              <(printf 'f3ae088181bf583e55daf962a92bb46f4f1d07b7' | xxd -r -p); then
    echo "Manifest ROM hash is not US Emerald v1.0" >&2
    exit 1
fi

OUTPUT="$DIST_DIR/PokemonEmeraldNano_funkey-s.opk"
rm -f "$OUTPUT"
mksquashfs "$STAGE" "$OUTPUT" -all-root -noappend -no-exports -no-xattrs -comp gzip
echo "Created $OUTPUT"
