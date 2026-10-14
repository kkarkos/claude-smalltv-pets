#!/bin/sh
# Builds the firmware and uploads it to the pet over Wi-Fi.
#   tools/flash.sh                 -> http://claude-pet.local
#   tools/flash.sh 192.168.1.50   -> that address (also works on the stock
#                                     GeekMagic firmware's /update page)
set -e
cd "$(dirname "$0")/../firmware"
HOST=${1:-claude-pet.local}
PIO=${PIO:-$(command -v pio || echo "$HOME/.local/bin/pio")}

# pioarduino re-checks its Python deps on every build. On a Mac whose uv is an
# Intel build, the newest `cryptography` has no wheel and tries to compile
# Rust; keep it on a release that ships prebuilt wheels.
CONSTRAINTS="$HOME/.platformio/uv-constraints.txt"
[ -f "$CONSTRAINTS" ] || echo 'cryptography<47' > "$CONSTRAINTS"
UV_CONSTRAINT="$CONSTRAINTS" "$PIO" run

BIN=.pio/build/smalltv_pro/firmware.bin
echo "Uploading $(wc -c < "$BIN" | tr -d ' ') bytes to http://$HOST/update ..."
curl --fail --show-error -m 180 -F "firmware=@$BIN" "http://$HOST/update"
echo
echo "Done. The pet restarts in a few seconds."
