#!/bin/bash

if ! ../resource/tools/enter_dfu.py; then
    echo "Wait for device to be off"
    echo "Press B and plug"
    echo "LEDS 4 & 5 should blink"
fi
while :; do
  lsusb|grep -q 1915:521f && break
  sleep 1
done
nrfutil device program --firmware objects/dfu-full.zip --traits nordicDfu
