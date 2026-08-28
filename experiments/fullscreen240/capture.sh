#!/usr/bin/env bash
# Grab the live framebuffer from the device and write one PNG per page.
# Capture WHILE the app is running: after it exits the launcher repaints and
# you get its wallpaper instead of the game (see RG_NANO_PORT_HANDOFF.md).
#
#   experiments/fullscreen240/capture.sh experiments/fullscreen240/shots/walking
#
# The device has no base64 (busybox), so this dd's to tmpfs and pulls the raw
# bytes over ssh instead of encoding them. /dev/fb0 is 240x720 = 345600 bytes:
# three 240x240 pages, and SDL cycles two of them, so all three come back and
# fb2png.py writes one PNG each.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="${1:-$HERE/shots/capture}"
NANO="${NANO:-/mnt/c/Programmering/SBC/RG_Nano/NanoWiFi/nano_remote.sh}"
REMOTE_TMP=/tmp/fbgrab.raw

mkdir -p "$(dirname "$PREFIX")"
cd "$(dirname "$NANO")"
./nano_remote.sh run "dd if=/dev/fb0 of=$REMOTE_TMP bs=345600 count=1 2>/dev/null; ls -l $REMOTE_TMP"
./nano_remote.sh pull "$REMOTE_TMP" "$PREFIX.raw"
./nano_remote.sh run "rm -f $REMOTE_TMP"
ls -l "$PREFIX.raw"
python3 "$HERE/fb2png.py" "$PREFIX.raw" "$PREFIX"
