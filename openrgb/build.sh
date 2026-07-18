#!/usr/bin/env bash
#
# Build ZMK firmware for the Keychron V6 Ultra 8K (and its Ultra siblings),
# including the OpenRGB direct-control additions on this branch.
#
# Produces an OTA/DFU-flashable image (app/build/zephyr/zmk.bin) whose header
# format matches Keychron's official CDN firmware, i.e. the file you flash via
# launcher.keychron.com > Firmware Update (or over the chip's DFU bootloader).
#
# The toolchain is the Docker image ZMK's CI uses; nothing is installed on the
# host. The west workspace (zephyr/ + modules/) is created inside the repo on
# the first run.
#
# Prerequisites: docker, and this repo checked out as the west manifest repo.
#
# Usage:
#   ./openrgb/build.sh                          # builds keychron_v6_ultra_ansi
#   ./openrgb/build.sh keychron_q6_ultra_ansi   # build a different Ultra shield
#   ZMK_WORKSPACE=/path/to/zmk ./openrgb/build.sh   # override workspace location
#
set -euo pipefail

# ---- config ---------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="${ZMK_BUILD_IMAGE:-zmkfirmware/zmk-build-arm:3.5}"
# west topdir = this repo's root (holds app/; zephyr/ + modules/ land here on init).
WORKSPACE="${ZMK_WORKSPACE:-$(cd "$SCRIPT_DIR/.." && pwd)}"
BOARD="keychron"                                     # RTL8762GTU board; defines RTK_DFU that the Ultra shields require
SHIELD="${1:-keychron_v6_ultra_ansi}"                # keyboard shield; override via first arg
OUTDIR="$SCRIPT_DIR/output"
# ---------------------------------------------------------------------------

command -v docker >/dev/null || { echo "ERROR: docker not found on host." >&2; exit 1; }
[ -d "$WORKSPACE/app" ] || { echo "ERROR: workspace not found at $WORKSPACE (set ZMK_WORKSPACE)" >&2; exit 1; }

# Pull the toolchain image if it isn't present yet.
docker image inspect "$IMAGE" >/dev/null 2>&1 || { echo ">> pulling $IMAGE ..."; docker pull "$IMAGE"; }

mkdir -p "$OUTDIR"
echo ">> board=$BOARD shield=$SHIELD workspace=$WORKSPACE"

# Run the build inside the container.
#  -u  : match host uid/gid so build artifacts aren't root-owned
#  IMPORTANT: build dir MUST be app/build. Keychron's post-build OTA step
#  (app/CMakeLists.txt -> prepend_header) runs from app/ and hardcodes the
#  paths "mp.ini" and "build/zephyr/zmk.bin" relative to app/. Any other -d
#  breaks the header step ("mp.ini not found" / "zmk.bin not exist").
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -e BOARD="$BOARD" -e SHIELD="$SHIELD" \
  -v "$WORKSPACE":/workspaces/zmk -w /workspaces/zmk "$IMAGE" bash -lc '
    set -e
    # One-time workspace init (no-op once zephyr/ and modules/ exist).
    if [ ! -d zephyr ] || [ ! -d modules ]; then
      echo ">> initializing west workspace (first run) ..."
      west init -l app
      west update
      west zephyr-export
    fi
    west build -s app -p -b "$BOARD" -d app/build -- -DSHIELD="$SHIELD"
  '

BIN="$WORKSPACE/app/build/zephyr/zmk.bin"
HEX="$WORKSPACE/app/build/zephyr/zmk.hex"
[ -f "$BIN" ] || { echo "ERROR: build did not produce zmk.bin" >&2; exit 1; }

cp "$BIN" "$OUTDIR/${SHIELD}.bin"
[ -f "$HEX" ] && cp "$HEX" "$OUTDIR/${SHIELD}.hex"

echo
echo "=== BUILD OK ==="
echo "OTA-flashable image : $OUTDIR/${SHIELD}.bin"
echo "size                : $(stat -c%s "$OUTDIR/${SHIELD}.bin") bytes"
echo "sha256              : $(sha256sum "$OUTDIR/${SHIELD}.bin" | cut -d' ' -f1)"
echo
echo "Flash it with ./openrgb/flash.py, or via launcher.keychron.com > Firmware"
echo "Update. Image is plaintext (unsigned/unencrypted)."
