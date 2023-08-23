#!/bin/bash

if [[ $BASH_SOURCE = */* ]]; then
  cd -- "${BASH_SOURCE%/*}/" || exit
fi

if ! ../resource/tools/enter_dfu.py; then
    echo "Wait for device to be off"
    echo "Press B and plug"
    echo "LEDS 4 & 5 should blink"
fi
while :; do
  lsusb|grep -q 1915:521f && break
  sleep 1
done

device_type=ultra
lsusb | grep 1915:521f | grep -q ChameleonLite && device_type=lite

echo "Flashing $device_type"

dfu_package=objects/${device_type}-dfu-full.zip

if [ ! -f $dfu_package ]; then
    echo "DFU package for $device_type not found, aborting."
    echo "Build firmware using CURRENT_DEVICE_TYPE=$device_type firmware/build.sh"
    exit 1
fi

nrfutil device program --firmware $dfu_package --traits nordicDfu

