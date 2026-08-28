#!/usr/bin/env bash
# Push the experiment OPK to the device. Run from WSL Ubuntu.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REMOTE="${REMOTE:-/mnt/Native games/PokemonEmeraldFS240_funkey-s.opk}"
NANO="${NANO:-/mnt/c/Programmering/SBC/RG_Nano/NanoWiFi/nano_remote.sh}"
cd "$(dirname "$NANO")"
./nano_remote.sh push "$HERE/dist/PokemonEmeraldFS240_funkey-s.opk" "$REMOTE" 0755
echo "deployed to $REMOTE"
