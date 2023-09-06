# Firmware

The Chameleon flash contains several parts: the bootloader and its settings, the application, the user data and the SoftDevice.

NOTE: If you are a developer searching for the building instructions, look into [development](./development.md)

## The Bootloader

The bootloader is the lowest-level program running on your Chameleon. It is read-only and provides the DFU (**D**evice **F**irmware **U**pgrade) mode. The bootloader being read-only, it makes it really hard to brick your Chameleon. The flash also contains a special section to store bootloader settings required by the nRF to deal with upgrades. This is only a concern for developers.


You enter DFU mode by of the following methods:

1. Physical button

- Disconnect the Chameleon and wait for it to enter sleep mode
- Hold down the 🅑 button. If you are using Windows you have to wait about ~5s before next step.
- Plug USB into a PC while still holding the button. If you are using Windows you have to wait about ~10s before next step.
- Then release the 🅑 button

2. From CLI

- Execute the command `hw dfu`

3. From GUI

- Click on `Enter DFU mode`

4. From Shell

- Execute the script `resource/tools/enter_dfu.py`

The device stays in DFU mode for ~30s.
While in DFU mode waiting for the update, the LEDs 4 and 5 blink alternatively green 🟢🟢.
You can then perform firmware upgrades either via a GUI or the command line:

1. Download nRF Util from the [nRF website](https://www.nordicsemi.com/Products/Development-tools/nrf-util)

2. Open a Command Line / Terminal on your PC

3. Install the "device" toolkit by running `nrfutil install device`

4. Download the Chameleon firmware from [GitHub](https://github.com/RfidResearchGroup/ChameleonUltra/releases). At the moment it is better to take the *Development release* but beware bugs can occur. Choose `ultra-dfu-app.zip` for the Ultra or the Devkit, and `lite-dfu-app.zip` for the Lite.

5. Put your Chameleon into DFU mode and install the firmware with the following command: `nrfutil device program --firmware ultra-dfu-app.zip --traits nordicDfu` (keep in mind to change the filename if you are using a Lite).

Step 5: Alternatively you can connect the Chameleon over USB and use the script `firmware/flash-dfu-app.sh` which will take care of flipping it into DFU mode and flashing it with the adequate firmware.

While flashing firmware is in progress, the LEDs 4 and 5 should blink fast blue 🔵🔵 and the firmware update should be finished in a matter of seconds. Using DFU and performing a firmware update also helps recovering from most device-related issues.

If LEDs 4 and 5 are flashing slow red 🔴🔴, it indicates an issue with DFU. Try to unplug and plug again or unplug and wait for it to timeout and try again the whole procedure.

## The Application

The application is the piece of software being loaded by the bootloader. It communicates with the client, emulates, reads and writes cards, drives the LEDs, handles buttons and much more. The application is also writable, it is the piece of software being updated via DFU.

The communication with the application is either done via the CLI or a GUI. Communication can be done over USB or BLE (**B**luetooth **L**ow **E**nergy), although, at time of writing, only GUIs support BLE.

On boot, the application starts in emulation mode, so it can emulate up to 8 HF tags and up to 8 LF tags (one slot can handle both a HF and a LF).

The Chameleon can be awaken:

- by pressing a button
- when it comes close to a HF or LF field, *only if* a card corresponding to that field (HF/LF) is loaded into the active slot.

The white LED labeled RF lights up when it detects a field, again only if the active slot supports it.

In some situations, it can be cumbersome to wait for the boot-up animation. This is configurable, cf e.g. the CLI command `hw settings animation set -h`.

On a new Chameleon (or after a factory reset), 3 slots are defined, slot 1 holding both a HF and a LF:

- slot 1 LF: a EM4100 with UID `DEADBEEF88`
- slot 1 HF: a MIFARE Classic 1k with UID `DEADBEEF`
- slot 2 HF: a MIFARE Classic 1k with UID `DEADBEEF`
- slot 3 LF: a EM4100 with UID `DEADBEEF88`

When a slot is selected, the LED shows what type of card is loaded with the following color code:

- 🟢 HF card loaded
- 🔵 LF card loaded
- 🔴 Both HF and LF loaded

When a dual HF/LF slot is activated by an external field, it will turn green or blue according to the frequency.

The application controls the buttons. The behavior of the buttons is customizable via the CLI or a GUI. The default behavior is the following:

- 🅐 short press: Select previous slot

- 🅑 short press: Select next slot

- 🅐 long press: Copy LF or HF tag UID (only Ultra, not Lite)

- 🅑 long press: Copy LF or HF tag UID (only Ultra, not Lite)

*About UID copy*: the action depends on the current slot support. So to be able to copy an EM4100 LF tag, the slot must be configured firstly to emulate an EM4100 tag. And to be able to copy a HF 14a tag, the slot must be configured for the right type of HF tag. Only the UID will be copied, not the data.

The Chameleon also shows the following LED effects:

- Charging: 4 pulsing green lights

- CLI / GUI connected over USB: Chasing LEDs in the color of the selected slot (left to right for slots 1-4 and right to left for slots 5-8).

The device enters sleep mode after about 5s unless it is plugged in USB or if a client is connected over BLE. You can use the buttons to wake it up again. You can also press quickly a button during the sleep animation to keep the device awake.

## Write Modes
- **Normal**: Behaves like any normal card
- **Denied**: Read-only card, send NACK to write attempts
- **Deceive**: Accepts write commands but don't change any data (reader thinks write was successful but when reading back, nothing changed)
- **Shadow**: Accepts writes but reverts changes when device goes to sleep (reader can read and write like a normal card but changes are kept in RAM and are lost when the chameleon goes to sleep) 

## The SoftDevice

A [SoftDevice](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fstruct_nrf52%2Fstruct%2Fnrf52_softdevices.html) is a precompiled and linked binary software implementing a wireless protocol developed by Nordic Semiconductor.

We are using the [SoftDevice S140](https://infocenter.nordicsemi.com/index.jsp?topic=%2Fstruct_nrf52%2Fstruct%2Fnrf52_softdevices.html) which implements a BLE Central and Peripheral protocol stack solution.

## The User Data

The Chameleon has a reserved space of memory and flash where it stores application settings, active slot and slots configurations and data. This will not be overwritten by DFU updates and the data will only be reset by either issuing `hw factory_reset --i-know-what-im-doing` in the CLI or clicking `Factory reset` in a GUI.
*Warning:* Settings and/or data might be reset to defaults if you downgrade the firmware version up to a version not supporting the newer format.
