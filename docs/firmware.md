# Firmware

The Chameleons firmware consists of 3 parts, the bootloader, the application and the settings

NOTE: If your developer searching for the building instructions, look into [How_to_use_Firmware.md](../How_to_use_Firmware.md)

## The Bootloader

The bootloader is the lowest level programm running on your Chameleon. It is read only and provides the DFU (**D**evice **F**irmware **U**pgrade) mode. The bootloader being read only also makes it really hard to brick your Chameleon. You enter DFU mode by:

- Holding down the `B` or `right` button for ~5s, plugging it into a PC while still holding the button, hold for another ~10s and release.

- Issuing `hw dfu` from the CLI

- Clicking on `Enter DFU mode` in a GUI

The device stays in dfu mode for ~30s. While in DFU mode, the LEDs 4 and 5 blink alternating green. While in DFU mode you can perform firmware upgrades either via a GUI or the command line:

1. Download NRF Util from the [NRF website](https://www.nordicsemi.com/Products/Development-tools/nrf-util)

2. Open a Command Line / Terminal on your PC

3. Install the "device" toolkit by running `nrfutil install device`

4. Download the firmware from [the github](https://github.com/RfidResearchGroup/ChameleonUltra/releases), you want the ultra-dfu-app.zip or lite-dfu-app.zip depending on your device (the Devkit in this case is a Ultra)

5. Put your Chameleon into dfu mode and install the firmware with the following command: `nrfutil device program --firmware ultra-dfu-app.zip --traits nordicDfu`(Keep in mind to change the filename to whatever device your using)

While flashing firmware the LEDs 4 and 5 should blink blue. Using DFU and performing a firmware update is also how you recover from most device related issues.

## The Application

The application is the piece of software being loaded by the bootloader. It communicates with the client, emulates, reads and writes cards, drives the LEDs, handles buttons and much more. The application is also writable, its the piece of software being updated via DFU.

The communication with the application is either done via the CLI or a GUI. Communication can be done over USB or BLE (**B**luetooth **L**ow **E**nergy), altough, at time of writing, only GUIs support BLE.

The communication with the application isnt the easiest but is structured as follows:

`MAGIC BYTE(0x11) LRC(Magic Byte) COMMAND STATUS(0x00) DATA LRC(COMMAND + STATUS + DATA)`

You build the Packet by first adding 0x11, this is the "Magic Byte" to say that theres something coming. This is followed by the LRC ([**L**ongitudinal **R**edundancy **C**heck](https://en.wikipedia.org/wiki/Longitudinal_redundancy_check)) of the "Magic Byte". Then you put in the command in [Big Endian](https://en.wikipedia.org/wiki/Endianness). Each command gets assigned a unique number (eg: `factoryReset(1020)`), this is what your sending to the device. Append the status, also in Big Endian. The status is always 0x00. Then you add your Data, this could be anything, for example sending the card keys when reading a block.

For recieving its the exact same in reverse.

## The Settings

The Chameleon has a reserved space of memory and flash where it stores settings. This will not be overwritten by DFU updates and the settings will only be reset by either issuing `hw factory_reset --i-know-what-im-doing` in the CLI or clicking Factory reset in a GUI.