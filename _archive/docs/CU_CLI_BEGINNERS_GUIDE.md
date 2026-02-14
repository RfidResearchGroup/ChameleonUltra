# ChameleonUltra CLI - Beginner's Guide

A friendly, hands-on guide to using the ChameleonUltra (CU) from the command line.

---

## 1. Introduction

The **ChameleonUltra** is an NFC/RFID research tool that can emulate (pretend to be) many different types of access cards and tags. It supports two radio frequencies:

- **HF (High Frequency, 13.56 MHz)** — Mifare Classic, NTAG, Ultralight, and other ISO 14443-A tags (the kind you tap on readers)
- **LF (Low Frequency, 125 kHz)** — EM410x, HID Prox, Viking, and similar tags (often thicker keyfobs or clamshell cards)

The device has **8 slots**, and each slot can hold **one HF tag + one LF tag** simultaneously. Think of slots like presets — you load card data into a slot, then switch between slots with the buttons.

The **CLI (Command Line Interface)** is a Python interactive shell that gives you full control over the device: loading card data, configuring slots, scanning tags, changing settings, and more. Commands use a tree structure like `hw slot list` or `hf mf eload`.

---

## 2. Installation

### Prerequisites

- **Python 3.9 or newer** — check with `python3 --version`
- **Git** — to clone the repository
- A **USB-C cable** (data-capable, not charge-only)

### Quick Start

```bash
# Clone the repository
git clone https://github.com/RfidResearchGroup/ChameleonUltra.git
cd ChameleonUltra

# Make the launcher executable (one-time)
chmod +x chameleon.sh

# Launch the CLI
./chameleon.sh
```

On the **first run**, the script automatically:
1. Checks for Python 3.9+
2. Creates a virtual environment in `software/.venv`
3. Installs the required packages (colorama, prompt-toolkit, pyserial)
4. Launches the CLI

You'll see output like:

```
First run — setting up Python environment...
Setup complete.

 ██████╗██╗  ██╗ █████╗ ██╗   ██╗███████╗██╗     ███████╗ █████╗ ██╗  ██╗
██╔════╝██║  ██║██╔══██╗███╗ ███║██╔════╝██║     ██╔════╝██╔══██╗███╗ ██║
   ...

[Offline] chameleon -->
```

On **subsequent runs**, `./chameleon.sh` skips setup and goes straight to the CLI.

> **Tip:** The launcher works from any directory. You can run `~/git/ChameleonUltra/chameleon.sh` from anywhere and it will still find everything it needs.

### Alternative: Using UV (for developers)

If you prefer the project's developer tooling:

```bash
cd ChameleonUltra/software
uv sync
uv run python script/chameleon_cli_main.py
```

---

## 3. Connecting to Your Device

### USB (Recommended)

1. Plug your ChameleonUltra into your computer with a USB-C data cable
2. LEDs should light up on the device
3. In the CLI:

```
hw connect
```

You should see:

```
 { Chameleon Ultra connected: v2.0 }
```

The prompt changes from `[Offline]` to `[USB]`:

```
[USB] chameleon -->
```

### Bluetooth (BLE)

1. Power on the ChameleonUltra
2. Pair via your OS Bluetooth settings (e.g., **System Settings > Bluetooth** on macOS)
3. Once paired, run `hw connect` — the CLI auto-detects the BLE serial port

### Device Info

Once connected, try these commands:

```
hw version        # Firmware version
hw battery        # Battery voltage and percentage
hw chipid         # Device chip ID
hw address        # BLE address
```

### Disconnecting

```
hw disconnect
```

Or simply unplug the USB cable. To exit the CLI entirely, type `exit` (or `q`).

---

## 4. Understanding Slots

The CU has **8 slots** (numbered 1-8). Each slot holds:
- One **HF** tag (or empty)
- One **LF** tag (or empty)

Only one slot is **active** at a time. The active slot is what the device emulates when held near a reader.

### View All Slots

```
hw slot list
```

This shows every slot's tag type, UID, and nickname. Add `--short` for a condensed view:

```
hw slot list --short
```

### Switch Active Slot

```
hw slot change -s 3
```

### Naming Slots

Give slots human-readable nicknames to keep track of what's loaded:

```
hw slot nick -s 1 --hf -n "Office Badge"
hw slot nick -s 1 --lf -n "Parking Gate"
```

To delete a nickname:

```
hw slot nick -s 1 --hf -d
```

To view a slot's nickname:

```
hw slot nick -s 1 --hf
```

---

## 5. Button Configuration

The CU has two physical buttons (A and B), each with short-press and long-press actions.

### View Current Settings

```
hw settings btnpress
```

### Set a Button Function

```
hw settings btnpress -a -s -f NEXTSLOT
```

That sets **Button A, short-press** to **cycle to the next slot**. The flags:

| Flag | Meaning |
|------|---------|
| `-a` | Button A |
| `-b` | Button B |
| `-s` | Short-press |
| `-l` | Long-press |
| `-f FUNCTION` | The function to assign |

### Available Functions

| Function | Description |
|----------|-------------|
| `NONE` | No action |
| `NEXTSLOT` | Select next slot |
| `PREVSLOT` | Select previous slot |
| `CLONE` | Read then simulate the ID/UID card number |
| `BATTERY` | Show battery level (LED flash) |
| `FIELDGEN` | Toggle NFC field generator |

### Save Settings

Button changes live in memory until you save:

```
hw settings store
```

---

## 6. Managing HF Slots

### Setting Up a Slot

Before loading data, tell the slot what tag type it should emulate:

```
hw slot type -s 1 -t MIFARE_1024
hw slot init -s 1 -t MIFARE_1024
```

**`type`** sets what the slot pretends to be. **`init`** fills it with default data for that type.

Common HF tag types: `MIFARE_Mini`, `MIFARE_1024`, `MIFARE_2048`, `MIFARE_4096`, `NTAG_213`, `NTAG_215`, `NTAG_216`, `MF0ICU1` (Ultralight), `MF0ICU2` (Ultralight C), `MF0UL11` (Ultralight EV1), `MF0UL21` (Ultralight EV1).

### Mifare Classic

**View emulator config** (UID, ATQA, SAK, magic mode settings):

```
hf mf econfig -s 1
```

**Load a dump file into the emulator:**

```
hf mf eload -f /path/to/dump.bin -s 1
```

Use `-t hex` for hex-format dumps instead of binary.

**Save emulator data to a file:**

```
hf mf esave -f /path/to/output.bin -s 1
```

**View the emulator's block data:**

```
hf mf eview -s 1
```

### NTAG / Mifare Ultralight

**View emulator config:**

```
hf mfu econfig -s 1
```

**Load a dump file:**

```
hf mfu eload -f /path/to/dump.bin
```

**Save emulator data:**

```
hf mfu esave -f /path/to/output.bin
```

**View emulator page data:**

```
hf mfu eview
```

### Importing Flipper Zero .nfc Files

If you have a `.nfc` file saved from a Flipper Zero, you can import it directly:

```
hf mfu nfcimport -f /path/to/file.nfc -s 1
```

- `-f` is the path to your `.nfc` file (drag from Finder into Terminal to paste the path)
- `-s 1` is the target slot (1-8). If omitted, uses the active slot.
- `--amiibo` (optional) derives and writes the correct PWD/PACK for amiibo tags so that readers (e.g., Nintendo Switch) can authenticate the emulated tag.

#### Supported Flipper .nfc Types

| Flipper "Device type" | CU Emulates |
|---|---|
| NTAG213 | NTAG 213 |
| NTAG215 | NTAG 215 |
| NTAG216 | NTAG 216 |
| NTAG210 | NTAG 210 |
| NTAG212 | NTAG 212 |
| Mifare Ultralight | Mifare Ultralight |
| Mifare Ultralight C | Mifare Ultralight C |
| Mifare Ultralight EV1 | Mifare Ultralight EV1 (auto-detected) |

> **Not supported:** Mifare Classic, DESFire, bank cards. For Mifare Classic `.nfc` files, use the `hf mf eload` workflow instead.

#### Example Import

```
hf mfu nfcimport -f ~/Downloads/Kirby.nfc -s 1 --amiibo
```

Output:

```
Importing Flipper NFC file: Kirby.nfc
  Device type: NTAG215 -> NTAG 215
  UID: 04 83 C8 C7 6A 46 4B
  ATQA: 44 00  SAK: 00
  Version: 00 04 04 02 01 00 11 03
  Signature: 00 00 00 ... (32 bytes)
  Counters: 0, 0, 0
  Pages: 135

Setting slot 1 tag type to NTAG 215...
Setting anti-collision data...
Setting version data...
Setting signature data...
Setting counter data...
Writing 135 pages... done
Setting amiibo PWD: EE F7 2B 74, PACK: 80 80...

 - Import complete. Slot 1 is now emulating NTAG215 (Kirby.nfc)
```

> **Note:** The `--amiibo` flag is only needed for amiibo NTAG215 dumps. For other Ultralight/NTAG tags, omit it.

To verify:

```
hf mfu econfig -s 1    # Check UID, ATQA, SAK
hf mfu eview           # View all page data
```

---

## 7. Managing LF Slots

### EM410x

**Set up the slot:**

```
hw slot type -s 2 -t EM410X
hw slot init -s 2 -t EM410X
```

**Set a specific EM410x ID** (10 hex characters):

```
lf em 410x econfig -s 2 --id AABBCCDDEE
```

**Read the current emulated ID:**

```
lf em 410x econfig -s 2
```

### HID Prox

**Set up the slot:**

```
hw slot type -s 3 -t HIDProx
hw slot init -s 3 -t HIDProx
```

**Set HID Prox card data:**

```
lf hid prox econfig -s 3 -f H10301 --fc 123 --cn 45678
```

Arguments:
- `-f` — Card format (e.g., `H10301`, `IND26`, `IND27`, `W2804`, etc.)
- `--fc` — Facility code
- `--cn` — Card number
- `--il` — Issue level (optional, format-dependent)
- `--oem` — OEM code (optional, format-dependent)

### Reading Physical LF Tags

To read a physical tag, switch the device to **reader mode**:

```
hw mode -r
```

Then hold the tag near the device and run the appropriate read command:

```
lf em 410x read
lf hid prox read
lf hid prox read -f H10301
```

When done, switch back to **emulator mode**:

```
hw mode -e
```

---

## 8. Slot Housekeeping

| What you want to do | Command |
|---|---|
| List all slots | `hw slot list` |
| Switch active slot | `hw slot change -s N` |
| Set slot tag type | `hw slot type -s N -t TYPE` |
| Initialize slot with defaults | `hw slot init -s N -t TYPE` |
| Enable a slot sense | `hw slot enable -s N --hf` or `--lf` |
| Disable a slot sense | `hw slot disable -s N --hf` or `--lf` |
| Delete slot sense data | `hw slot delete -s N --hf` or `--lf` |
| Open all slots (reset all to defaults) | `hw slot openall` |
| Save slot config to flash | `hw slot store` |

> **Important:** Changes to slots live in RAM until you run `hw slot store`. If the device loses power before saving, your changes are lost.

---

## 9. Scanning Tags

To scan physical tags, switch to **reader mode** first:

```
hw mode -r
```

### HF (13.56 MHz) Scanning

**Quick scan** — detects tag and prints UID, ATQA, SAK:

```
hf 14a scan
```

**Detailed info** — more protocol details:

```
hf 14a info
```

### LF (125 kHz) Scanning

```
lf em 410x read              # Read EM410x tag
lf hid prox read             # Read HID Prox tag (auto-detect format)
lf hid prox read -f H10301   # Read with format hint
```

### Back to Emulator Mode

When done scanning, switch back so your CU can emulate tags again:

```
hw mode -e
```

Or just check the current mode:

```
hw mode
```

---

## 10. Settings & Maintenance

### LED Animation

```
hw settings animation             # View current mode
hw settings animation -m FULL     # Full animation
hw settings animation -m MINIMAL  # Minimal animation
hw settings animation -m NONE     # LEDs off
```

### BLE Pairing Key

View or change the 6-digit BLE pairing PIN:

```
hw settings blekey                # View current key
hw settings blekey -k 123456     # Set new key
```

### BLE Pairing Toggle

```
hw settings blepair              # View current state
hw settings blepair -e           # Enable BLE pairing
hw settings blepair -d           # Disable BLE pairing
```

### Save All Settings to Flash

```
hw settings store
```

Many commands remind you to do this — settings changes live in RAM until stored.

### Reset Settings to Defaults

```
hw settings reset --force
```

### Factory Reset

Wipes **all** slot data and settings. Use with caution:

```
hw factory_reset --force
```

### Enter DFU Mode (Firmware Update)

```
hw dfu
```

This reboots the device into bootloader mode for flashing new firmware.

---

## 11. Quick Reference

| Command | Description |
|---|---|
| **Connection** | |
| `hw connect` | Connect to device (USB or BLE) |
| `hw connect -p /dev/ttyXXX` | Connect to specific serial port |
| `hw disconnect` | Disconnect from device |
| `hw version` | Show firmware version |
| `hw battery` | Show battery voltage and level |
| **Slots** | |
| `hw slot list` | List all slots with details |
| `hw slot change -s N` | Switch to slot N (1-8) |
| `hw slot type -s N -t TYPE` | Set slot tag type |
| `hw slot init -s N -t TYPE` | Initialize slot with default data |
| `hw slot nick -s N --hf -n "Name"` | Set slot nickname |
| `hw slot enable -s N --hf` | Enable slot HF sense |
| `hw slot disable -s N --lf` | Disable slot LF sense |
| `hw slot store` | Save slot config to flash |
| **Device Mode** | |
| `hw mode` | Show current mode |
| `hw mode -r` | Switch to reader mode |
| `hw mode -e` | Switch to emulator mode |
| **HF - Mifare Classic** | |
| `hf mf econfig -s N` | View MF Classic emulator config |
| `hf mf eload -f FILE -s N` | Load dump into emulator |
| `hf mf esave -f FILE -s N` | Save emulator to dump file |
| `hf mf eview -s N` | View emulator block data |
| **HF - NTAG / Ultralight** | |
| `hf mfu econfig -s N` | View MFU/NTAG emulator config |
| `hf mfu eload -f FILE` | Load dump into emulator |
| `hf mfu esave -f FILE` | Save emulator to dump file |
| `hf mfu eview` | View emulator page data |
| `hf mfu nfcimport -f FILE -s N` | Import Flipper Zero .nfc file |
| `hf mfu nfcimport -f FILE -s N --amiibo` | Import amiibo with PWD/PACK |
| **HF Scanning** | |
| `hf 14a scan` | Scan for ISO 14443-A tags |
| `hf 14a info` | Detailed tag information |
| **LF - EM410x** | |
| `lf em 410x econfig -s N --id HEX` | Set EM410x emulated ID |
| `lf em 410x read` | Read physical EM410x tag |
| **LF - HID Prox** | |
| `lf hid prox econfig -s N -f FMT --fc N --cn N` | Set HID Prox emulated data |
| `lf hid prox read` | Read physical HID Prox tag |
| **Settings** | |
| `hw settings btnpress` | View button config |
| `hw settings btnpress -a -s -f FUNC` | Set button function |
| `hw settings animation -m MODE` | Set LED animation mode |
| `hw settings blekey` | View/set BLE pairing key |
| `hw settings blepair -e/-d` | Enable/disable BLE pairing |
| `hw settings store` | Save settings to flash |
| **Danger Zone** | |
| `hw settings reset --force` | Reset settings to defaults |
| `hw factory_reset --force` | Full factory reset |
| `hw dfu` | Enter DFU/bootloader mode |

---

## Troubleshooting

**"Chameleon not found"**
- Check that your USB cable supports data transfer (some are charge-only)
- Try a different USB port
- For BLE: ensure the device is paired in your OS and not connected to another app

**"Please connect to chameleon device first"**
- Run `hw connect` before issuing device commands

**Command not recognized / wrong arguments**
- Type just the command group to see available subcommands (e.g., `hw slot`, `hf mf`)
- Add `-h` to any command for help (e.g., `hf mf eload -h`)

**Changes lost after reboot**
- Run `hw slot store` and `hw settings store` to persist changes to flash

**Flipper .nfc import says "Unsupported device type"**
- The `hf mfu nfcimport` command only supports Ultralight/NTAG family tags
- For Mifare Classic dumps, use `hf mf eload` instead
