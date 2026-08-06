#!/bin/env bash

if [[ $BASH_SOURCE = */* ]]; then
  cd -- "${BASH_SOURCE%/*}/" || exit
fi

softdevice=s140
softdevice_version=7.2.0
softdevice_id=0x0100

application_version=1
bootloader_version=1

device_type=${CURRENT_DEVICE_TYPE:-ultra}
case $device_type in
  "ultra") hw_version=0 ;;
  "lite")  hw_version=1 ;;
  *)       echo "Unknown CURRENT_DEVICE_TYPE $CURRENT_DEVICE_TYPE, aborting."; exit 1 ;;
esac

# ---------------------------------------------------------------------------
# Recovery-mode build
# ---------------------------------------------------------------------------
if [[ -n "$RECOVERY_ZIP" ]]; then
  if [[ ! -f "$RECOVERY_ZIP" ]]; then
    echo "error: RECOVERY_ZIP=$RECOVERY_ZIP does not exist" >&2
    exit 1
  fi
  RECOVERY_MODE=1
  echo "Recovery-mode build — embedding stock BL from $RECOVERY_ZIP"
fi

echo "Building firmware for $device_type (hw_version=$hw_version)"

set -xe

rm -rf "objects"

if [[ -z "$RECOVERY_MODE" ]]; then
  # ---- Single composite bootloader at 0xF3000 (44KB) ---------------------
  # UF2 (MSC) + CDC serial DFU, no debug CDC, no logging. Fits the stock
  # 44KB region so it's directly flashable from stock — no SWD, no
  # two-stage. The MBR boots 0xF3000, which is where this BL lives.
  # -----------------------------------------------------------------------
  (
    cd bootloader
    make -j
    # Ensure hex is generated (parallel build can miss it)
    make ../objects/bootloader.hex 2>/dev/null || \
    arm-none-eabi-objcopy -O ihex ../objects/bootloader.out ../objects/bootloader.hex
  )

  ./tools/gen_embedded_bl.py \
    objects/bootloader.hex \
    application/src/embedded_bootloader.h

else
  ./tools/make_recovery_header.py \
    "$RECOVERY_ZIP" \
    application/src/embedded_bootloader.h
fi

(
  cd application
  if [[ -n "$RECOVERY_MODE" ]]; then
    make -j RECOVERY_MODE=1
  else
    make -j
  fi
)

(
  cd objects

  if [[ -n "$RECOVERY_MODE" ]]; then
    ../tools/uf2conv.py application.hex -o ${device_type}-revert-to-stock.uf2

    set +x
    echo
    echo "=========================================================="
    echo "Built recovery UF2: objects/${device_type}-revert-to-stock.uf2"
    echo "=========================================================="
  else
    cp ../nrf52_sdk/components/softdevice/${softdevice}/hex/${softdevice}_nrf52_${softdevice_version}_softdevice.hex softdevice.hex

    # SD+BL DFU zip — composite BL at 0xF3000, flashable from stock
    nrfutil nrf5sdk-tools pkg generate \
      --hw-version $hw_version \
      --bootloader  bootloader.hex --bootloader-version $bootloader_version \
      --softdevice  softdevice.hex \
      --sd-req ${softdevice_id} --sd-id ${softdevice_id} \
      --key-file ../../resource/dfu_key/chameleon.pem \
      ${device_type}-dfu-sdbl.zip

    nrfutil nrf5sdk-tools pkg generate \
      --hw-version $hw_version \
      --bootloader  bootloader.hex   --bootloader-version $bootloader_version  --key-file ../../resource/dfu_key/chameleon.pem \
      --application application.hex  --application-version $application_version \
      --softdevice  softdevice.hex \
      --sd-req ${softdevice_id} --sd-id ${softdevice_id} \
      ${device_type}-dfu-full.zip

    nrfutil nrf5sdk-tools pkg generate \
      --hw-version $hw_version --key-file ../../resource/dfu_key/chameleon.pem \
      --application application.hex  --application-version $application_version \
      --sd-req ${softdevice_id} \
      ${device_type}-dfu-app.zip

    nrfutil nrf5sdk-tools settings generate \
      --family NRF52840 \
      --application application.hex --application-version $application_version \
      --softdevice softdevice.hex \
      --bootloader-version $bootloader_version --bl-settings-version 2 \
      settings.hex

    mergehex \
      --merge \
      settings.hex \
      application.hex \
      --output application_merged.hex

    mergehex \
      --merge \
        bootloader.hex \
        application_merged.hex \
        softdevice.hex \
      --output fullimage.hex

    ../tools/uf2conv.py application.hex --family 0x1B57745F -o ${device_type}-application.uf2
    ../tools/uf2conv.py fullimage.hex --family 0x1B57745F -o ${device_type}-fullimage.uf2

    # Bootloader-only UF2 for the stage-1 -> stage-2 handoff. Contains ONLY
    # the 0xEB000-0xFE000 BL region, so stage 1 can stage it at 0x80000
    # without app blocks (from fullimage) clobbering the staging area.
    ../tools/uf2conv.py bootloader.hex --family 0x1B57745F -o ${device_type}-bootloader.uf2

    tmp_dir=$(mktemp -d -t cu_binaries_XXXXXXXXXX)
    cp *.hex "$tmp_dir"
    mv $tmp_dir/application_merged.hex $tmp_dir/application.hex
    rm $tmp_dir/settings.hex
    zip -j ${device_type}-binaries.zip $tmp_dir/*.hex
    rm -rf $tmp_dir

    set +x
    echo
    echo "=========================================================="
    echo "Build complete."
    echo "  SD+BL      : objects/${device_type}-dfu-sdbl.zip"
    echo "  App        : objects/${device_type}-dfu-app.zip"
    echo "  Full image : objects/${device_type}-fullimage.uf2"
    echo "Use flash-dfu-sdbl.sh to install both stages."
    echo "=========================================================="
  fi
)
