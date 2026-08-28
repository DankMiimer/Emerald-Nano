#!/bin/sh

DATA_DIR="/mnt/FunKey/.pokemon-emerald-nano"
APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

# Drum78OS only exports these from /etc/profile, which a non-login /bin/sh
# launcher never sources; without SDL_NOMOUSE the fbcon driver aborts
# SDL_Init with "Unable to open mouse" (the device has no mouse node, only
# /dev/input/event0).
export SDL_NOMOUSE=1
export SDL_VIDEODRIVER=fbcon
export SDL_FBDEV=/dev/fb0

# Deliberately does NOT call "frontend set none" to take the screen. That
# writes /mnt/disable_frontend, which lives on the persistent vfat partition,
# so any launch that does not reach its matching "frontend set" -- a SIGKILL, a
# crash at the wrong moment, a flat battery -- leaves the console booting with
# no launcher and no working buttons until it is repaired over SSH. The
# launcher hands the framebuffer over on its own when it starts an OPK; if it
# turns out to still fight for the screen, fix that here in a way that cannot
# outlive this script.

mkdir -p "$DATA_DIR"
[ -f "$DATA_DIR/last.log" ] && mv -f "$DATA_DIR/last.log" "$DATA_DIR/prev.log"

cd "$APP_DIR"
exec ./pokemon-emerald-nano \
    --data-dir "$DATA_DIR" \
    --asset-manifest "$APP_DIR/asset_manifest.bin" \
    >"$DATA_DIR/last.log" 2>&1
