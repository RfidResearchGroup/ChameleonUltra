#!/bin/bash
# Single-bootloader installer — no SWD, no two-stage.
#
# Flashes the composite bootloader (UF2 + CDC serial DFU) to 0xF3000 via
# the stock serial DFU. The BL fits the stock 44KB region, so stock
# accepts it directly. After this, app + future BL updates work via UF2
# drag-and-drop or nrfutil serial DFU.
set -euo pipefail

if [[ $BASH_SOURCE = */* ]]; then cd -- "${BASH_SOURCE%/*}/" || exit; fi

wait_for_dfu() {
    echo "Waiting for serial DFU device (1915:521f)..."
    local t=0
    while :; do
        lsusb | grep -q 1915:521f && return 0
        sleep 1; t=$((t+1))
        [ $t -gt 30 ] && echo "timeout waiting for DFU" && return 1
    done
}

device_type=ultra
lsusb | grep 1915:521f | grep -q ChameleonLite && device_type=lite

bl_zip="objects/${device_type}-dfu-bl.zip"
[ -f "$bl_zip" ] || { echo "missing $bl_zip — run ./build.sh"; exit 1; }

echo "=== Flashing composite bootloader (SD+BL) via serial DFU ==="
../resource/tools/enter_dfu.py || echo "Manually: cold-boot + hold B + plug"
wait_for_dfu
nrfutil device program --firmware "$bl_zip" --traits nordicDfu

echo
echo "Done. Composite bootloader (UF2 + CDC serial DFU) installed at 0xF3000."
echo "Flash the application with ./flash-dfu-app.sh or drag the app UF2."
