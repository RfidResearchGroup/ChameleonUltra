#!/bin/env bash

# Partially based on informations from https://github.com/RfidResearchGroup/ChameleonUltra/issues/16

if [[ $BASH_SOURCE = */* ]]; then
  cd -- "${BASH_SOURCE%/*}/" || exit
fi

softdevice=s140
softdevice_version=7.2.0
softdevice_id=0x0100


# TODO: find a way to manage this automatically, I don't want to rely on action build #.
application_version=1
bootloader_version=1

declare -A device_type_to_hw_version=( ["ultra"]="0" ["lite"]="1" )

device_type=${CURRENT_DEVICE_TYPE:-ultra}
hw_version=${device_type_to_hw_version[$device_type]}
echo "Building firmware for $device_type (hw_version=$hw_version)"

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
  nrfutil settings generate --family NRF52840 \
    --application application.hex --application-version $application_version \
    --bootloader-version $bootloader_version --bl-settings-version 2 settings.hex
  mergehex --merge bootloader.hex settings.hex --output bootloader_merged.hex
  mergehex --merge bootloader_merged.hex application.hex \
    ../nrf52_sdk/components/softdevice/${softdevice}/hex/${softdevice}_nrf52_${softdevice_version}_softdevice.hex --output fullimage.hex
  nrfutil pkg generate \
    --application application.hex --application-version $application_version \
    --bootloader bootloader_merged.hex --bootloader-version $bootloader_version \
    --softdevice ../nrf52_sdk/components/softdevice/${softdevice}/hex/${softdevice}_nrf52_${softdevice_version}_softdevice.hex --sd-id ${softdevice_id} \
    --sd-req ${softdevice_id} --hw-version $hw_version \
    --key-file ../../resource/dfu_key/chameleon.pem \
    dfu-full.zip
  nrfutil pkg generate \
    --application application.hex --application-version $application_version \
    --sd-req ${softdevice_id} --hw-version $hw_version \
    --key-file ../../resource/dfu_key/chameleon.pem \
    dfu-app.zip
)
