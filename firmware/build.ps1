# build.ps1

$ErrorActionPreference = "Stop"

# Change to script directory
Set-Location -Path $PSScriptRoot

$softdevice = "s140"
$softdevice_version = "7.2.0"
$softdevice_id = "0x0100"

# TODO: find a way to manage this automatically, I don't want to rely on action build #.
$application_version = 1
$bootloader_version = 1

$device_type = $env:CURRENT_DEVICE_TYPE
if (-not $device_type) { $device_type = "ultra" }

switch ($device_type) {
  "ultra" { $hw_version = 0 }
  "lite"  { $hw_version = 1 }
  default {
    Write-Error "Unknown CURRENT_DEVICE_TYPE $device_type, aborting."
    exit 1
  }
}

Write-Host "Building firmware for $device_type (hw_version=$hw_version)"

Remove-Item -Recurse -Force "objects" -ErrorAction SilentlyContinue

Push-Location "bootloader"
Invoke-Expression "make -j"
Pop-Location

Push-Location "application"
Invoke-Expression "make -j"
Pop-Location

Push-Location "objects"

Copy-Item "../nrf52_sdk/components/softdevice/$softdevice/hex/${softdevice}_nrf52_${softdevice_version}_softdevice.hex" "softdevice.hex"

& nrfutil nrf5sdk-tools pkg generate `
  --hw-version $hw_version `
  --bootloader bootloader.hex --bootloader-version $bootloader_version --key-file ../../resource/dfu_key/chameleon.pem `
  --application application.hex --application-version $application_version `
  --softdevice softdevice.hex `
  --sd-req $softdevice_id --sd-id $softdevice_id `
  "${device_type}-dfu-full.zip"

& nrfutil nrf5sdk-tools settings generate `
  --family NRF52840 `
  --application application.hex --application-version $application_version `
  --softdevice softdevice.hex `
  --bootloader-version $bootloader_version --bl-settings-version 2 `
  settings.hex

& mergehex `
  --merge `
  settings.hex `
  application.hex `
  --output application_merged.hex

& mergehex `
  --merge `
  bootloader.hex `
  application_merged.hex `
  softdevice.hex `
  --output fullimage.hex

$tmp_dir = New-TemporaryFile | Split-Path
$tmp_dir = Join-Path (Split-Path $tmp_dir) ("cu_binaries_" + -join ((65..90) + (97..122) | Get-Random -Count 10 | % {[char]$_}))
New-Item -ItemType Directory -Path $tmp_dir | Out-Null

Copy-Item *.hex $tmp_dir
Remove-Item "$tmp_dir\application.hex"
Move-Item "$tmp_dir\application_merged.hex" "$tmp_dir\application.hex"
Remove-Item "$tmp_dir\settings.hex"

Compress-Archive -Path "$tmp_dir\*.hex" -DestinationPath "${device_type}-binaries.zip"

Remove-Item -Recurse -Force $tmp_dir

Pop-Location
