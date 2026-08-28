#!/usr/bin/env bash
set -euo pipefail

SDK_VERSION="2.3.0"
SDK_URL="https://github.com/FunKey-Project/FunKey-OS/releases/download/FunKey-OS-${SDK_VERSION}/FunKey-sdk-${SDK_VERSION}.tar.gz"
DESTINATION="${1:-$HOME/funkey-sdk-${SDK_VERSION}}"
ARCHIVE="${TMPDIR:-/tmp}/FunKey-sdk-${SDK_VERSION}.tar.gz"

if [[ -x "$DESTINATION/bin/arm-funkey-linux-musleabihf-gcc" ]]; then
    echo "FunKey SDK already installed at $DESTINATION"
else
    mkdir -p "$DESTINATION"
    curl -fL "$SDK_URL" -o "$ARCHIVE"
    tar -xzf "$ARCHIVE" -C "$DESTINATION" --strip-components=1
fi

if [[ -x "$DESTINATION/relocate-sdk.sh" ]]; then
    "$DESTINATION/relocate-sdk.sh"
fi

cat <<EOF
SDK ready at $DESTINATION
Add it to PATH before building:
  export PATH="$DESTINATION/bin:\$PATH"
  make -f Makefile_rg_nano
EOF
