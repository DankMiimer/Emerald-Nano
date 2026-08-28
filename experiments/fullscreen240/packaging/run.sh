#!/bin/sh
# experiments/fullscreen240 launcher. Deliberately NOT the stock run.sh:
#
#  * its own data dir, so the experiment cannot corrupt the real save. The
#    first launch seeds it from the real save so there is something to walk
#    around in.
#  * a development build, so no asset manifest and no ROM gate.
#
# The "frontend set none" warning in packaging/rg-nano/run.sh applies here too:
# do not add it. See RG_NANO_PORT_HANDOFF.md.

DATA_DIR="/mnt/FunKey/.pokemon-emerald-fs240"
MAIN_DATA_DIR="/mnt/FunKey/.pokemon-emerald-nano"
APP_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

export SDL_NOMOUSE=1
export SDL_VIDEODRIVER=fbcon
export SDL_FBDEV=/dev/fb0

mkdir -p "$DATA_DIR"
if [ ! -f "$DATA_DIR/pokeemerald.sav" ] && [ -f "$MAIN_DATA_DIR/pokeemerald.sav" ]; then
    cp "$MAIN_DATA_DIR/pokeemerald.sav" "$DATA_DIR/pokeemerald.sav"
fi
[ -f "$DATA_DIR/last.log" ] && mv -f "$DATA_DIR/last.log" "$DATA_DIR/prev.log"

cd "$APP_DIR"
exec ./pokemon-emerald-nano \
    --data-dir "$DATA_DIR" \
    >"$DATA_DIR/last.log" 2>&1
