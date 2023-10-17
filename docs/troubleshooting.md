# Hardware

## Difficulties to get emulation working properly

Try with waking up the Chameleon by pressing a button before presenting it to the reader. Try with keeping some 2-3 cm distance to the reader.

## Difficulties to get the LF working properly

The LF antenna is on a second PCB attached to the main PCB via little screws which also serve as electric connection.
It has reported that on some devices the electric connection is not good, some glue or resin residues may interfere.
You can try to dismantle very gently the screws and PCB, clean them and put them back in place.
Be very careful the screws have been reported to be quite fragile so be gentle with them!

# BLE

## Difficulties connecting using BLE

On Android make sure your location is turned, as that allows for scanning of bluetooth devices.

## Difficulties to use BLE

After BLE pairing, both the phone and ChameleonUltra will save a secret key for encrypted communication. If either party deletes the pairing record, it will result in communication failure. If Bluetooth cannot be connected, clearing the pairing information on the other side can solve the problem:

* Find the Bluetooth settings in the phone's system settings and cancel pairing with the ChameleonUltra.
* In the CLI of ChameleonUltra, execute the `hw settings bleclearbonds` command to clear all pairing records.

Default BLE connect key(passkey) is `123456`

# DFU

## Error when attempting DFU upgrade with `nrfutil`


```
[00:00:00] ------   0% [id:9] Failed, [sdfu] [json.exception.type_error.302] type must be string, but is null
```
or
```
[00:00:00] ------   0% [2/2 ...] Failed, [sdfu]
```

### Check permissions and ModemManager

Check the serial port permissions and if under Linux, make sure ModemManager is not interfering with your Chameleon.
The proposed [udev/rules.d file](../resource/driver/79-chameleon-usb-device-blacklist-dialout.rules) may help you (and add your user to the dialout group).

### Check `hw_version` of your DFU package

Another cause of this error is a mismatch between `hw_version` of the DFU package you want to use and your hardware. You can check it with the following command.

```
nrfutil nrf5sdk-tools pkg display my-dfu-file.zip |grep hw_version
```
`hw_version` must be equal to `0` for the Ultra and `1` for the Lite.

# CLI tools compilation

## ProxSpace: cmake error

The following error has been reported on some ProxSpace installations (why on some and not all is still unclear)

```
pm3 ~/ChameleonUltra/software/src$ cmake .
-- Building for: Ninja
-- The C compiler identification is GNU 10.3.0
-- Detecting C compiler ABI info
-- Detecting C compiler ABI info - failed
-- Check for working C compiler: C:/Users/moeya/Desktop/Chamelion/ProxSpace/msys2/mingw64/bin/cc.exe
-- Check for working C compiler: C:/Users/moeya/Desktop/Chamelion/ProxSpace/msys2/mingw64/bin/cc.exe - broken
CMake Error at C:/Users/moeya/Desktop/Chamelion/ProxSpace/msys2/mingw64/share/cmake-3.21/Modules/CMakeTestCCompiler.cmake:69 (message):
  The C compiler

    "C:/Users/xxx/ProxSpace/msys2/mingw64/bin/cc.exe"

  is not able to compile a simple test program.
```

This is due to a version of ninja not aware of the Windows paths. Fix:

```
pacman -R ninja --noconfirm
pacman -S mingw-w64-x86_64-ninja --noconfirm
```

# CLI usage

## InvalidException: Device unsupported cmd

You need to update the firmware of your Chameleon.

