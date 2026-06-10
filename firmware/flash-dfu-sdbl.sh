#!/bin/bash
# Two-stage UF2 bootloader installer.
#
# Stage 1: minimal UF2-only BL at 0xF3000 via serial DFU (fits stock 44KB).
# Stage 2: full BL at 0xEB000 via UF2 drag-and-drop.
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

wait_for_chameleon_drive() {
    echo "Waiting for CHAMELEON drive..." >&2
    local t=0
    while :; do
        drive=$(lsblk -o LABEL,PATH 2>/dev/null | awk '/CHAMELEON/{print $2}')
        if [ -n "$drive" ]; then
            mp=$(findmnt -n -o TARGET "$drive" 2>/dev/null || true)
            if [ -z "$mp" ]; then
                mp=$(udisksctl mount -b "$drive" 2>/dev/null | sed 's/.* at //' || true)
            fi
            [ -n "$mp" ] && echo "$mp" && return 0
        fi
        for d in /run/media/$USER/CHAMELEON /media/$USER/CHAMELEON /media/CHAMELEON; do
            [ -d "$d" ] && echo "$d" && return 0
        done
        sleep 1; t=$((t+1))
        [ $t -gt 60 ] && echo "timeout waiting for drive" >&2 && return 1
    done
}

drop_uf2() {
    local uf2="$1" mp="$2"
    cp "$uf2" "$mp/"
    sync                      # ensure flush before device resets
    echo "Wrote $(basename "$uf2") to $mp"
}

device_type=ultra
lsusb | grep 1915:521f | grep -q ChameleonLite && device_type=lite

stage1_zip="objects/${device_type}-dfu-sdbl-stage1.zip"
stage2_uf2="objects/${device_type}-fullimage.uf2"

[ -f "$stage1_zip" ] || { echo "missing $stage1_zip — run ./build.sh"; exit 1; }
[ -f "$stage2_uf2" ] || { echo "missing $stage2_uf2 — run ./build.sh"; exit 1; }

echo "=== Stage 1: minimal UF2 bootloader via serial DFU ==="
../resource/tools/enter_dfu.py || echo "Manually: cold-boot + hold B + plug"
wait_for_dfu
nrfutil device program --firmware "$stage1_zip" --traits nordicDfu
echo "Stage 1 done. Device will reboot into UF2 mode."
sleep 3

echo "=== Stage 2: full UF2 bootloader via drag-and-drop ==="
echo "If the drive doesn't appear: cold-boot + hold B + plug USB"
mp=$(wait_for_chameleon_drive)
drop_uf2 "$stage2_uf2" "$mp"

echo
echo "Done. Full UF2 bootloader installed at 0xEB000."
echo "Run ./flash-uf2-app.sh to flash the application."
