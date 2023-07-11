#!/bin/env bash

# Partially based on informations from https://github.com/RfidResearchGroup/ChameleonUltra/issues/16

set -xe

(
  cd bootloader
  make -j
)

(
  cd application
  make -j
)

(
  cd objects
  nrfutil settings generate --family NRF52840 --application application.hex --application-version 1 --bootloader-version 1 --bl-settings-version 2 settings.hex
  mergehex --merge bootloader.hex settings.hex application.hex ../nrf52_sdk/components/softdevice/s140/hex/s140_nrf52_7.2.0_softdevice.hex --output project.hex
)
