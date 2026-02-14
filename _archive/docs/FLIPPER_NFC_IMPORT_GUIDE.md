# Importing Flipper Zero .nfc Files to Your ChameleonUltra

A beginner-friendly guide for macOS users.

## What This Does

You have a `.nfc` file saved from a Flipper Zero (like an NTAG215 amiibo dump, an NTAG213 access tag, or a Mifare Ultralight transit card). This guide shows you how to load that file directly onto your ChameleonUltra so it can emulate the tag — UID, data, and all.

## What You Need

- A **ChameleonUltra** (or ChameleonLite)
- A **USB-C cable** (or a BLE serial connection — more on that below)
- A **Mac** with Python 3.9 or newer installed
- Your **`.nfc` file** already on your Mac

### Supported Tag Types

The import command works with these Flipper Zero device types:

| Flipper "Device type" | What ChameleonUltra emulates |
|---|---|
| NTAG213 | NTAG 213 |
| NTAG215 | NTAG 215 |
| NTAG216 | NTAG 216 |
| NTAG210 | NTAG 210 |
| NTAG212 | NTAG 212 |
| Mifare Ultralight | Mifare Ultralight |
| Mifare Ultralight C | Mifare Ultralight C |
| Mifare Ultralight EV1 | Mifare Ultralight EV1 (auto-detected) |

> **Not supported:** Mifare Classic, DESFire, bank cards, or anything that isn't an Ultralight/NTAG family tag. If your `.nfc` file is a Mifare Classic dump, you'll need the `hf mf eload` command instead (different workflow, not covered here).

---

## Step-by-Step

### Step 1: Install the ChameleonUltra CLI

Open **Terminal** (press `Cmd + Space`, type "Terminal", hit Enter).

If you haven't already cloned the repo and set up the CLI:

```bash
# Clone the repo (skip if you already have it)
git clone https://github.com/RfidResearchGroup/ChameleonUltra.git
cd ChameleonUltra/software

# Install dependencies
pip3 install -r requirements.txt
```

> **Tip:** If `pip3` doesn't work, you may need to install Python first:
> `brew install python` (requires [Homebrew](https://brew.sh)).

### Step 2: Connect Your ChameleonUltra

#### Option A: USB (recommended for beginners)

1. Plug your ChameleonUltra into your Mac with a USB-C cable
2. The device should power on (you'll see the LEDs light up)

#### Option B: Bluetooth (BLE)

1. Make sure your ChameleonUltra is powered on
2. On your Mac, pair with the ChameleonUltra via **System Settings > Bluetooth**
3. Once paired, it shows up as a virtual serial port — the CLI will find it automatically

### Step 3: Launch the CLI

In Terminal, navigate to the software directory and start the CLI:

```bash
cd ChameleonUltra/software
python3 script/chameleon_cli_main.py
```

You should see a prompt like:

```
chameleon ultra>
```

### Step 4: Connect to the Device

Type this at the prompt:

```
hw connect
```

You should see something like:

```
 { Chameleon Ultra connected: v2.0 }
```

If it says "Chameleon not found", double-check your cable or BLE pairing.

### Step 5: Import Your .nfc File

Now the fun part! Use the `hf mfu nfcimport` command:

```
hf mfu nfcimport -f /path/to/your/file.nfc -s 1
```

- **`-f`** is the path to your `.nfc` file. Drag the file from Finder into the Terminal window to paste the full path automatically.
- **`-s 1`** means "put it in slot 1". You can use slots 1 through 8. If you leave `-s` off, it uses whichever slot is currently active.
- **`--amiibo`** (optional) derives and writes the correct password (PWD/PACK) for amiibo tags. Use this when importing amiibo NTAG215 dumps so that readers can authenticate the emulated tag. See [Amiibo Tags](#amiibo-tags) below.

#### Example

```
hf mfu nfcimport -f ~/Downloads/Kirby.nfc -s 1
```

You'll see output like this:

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

 - Import complete. Slot 1 is now emulating NTAG215 (Kirby.nfc)
```

That's it! Your ChameleonUltra is now emulating the tag.

#### Amiibo Tags

If your `.nfc` file is an amiibo dump, add the `--amiibo` flag:

```
hf mfu nfcimport -f ~/Downloads/Kirby.nfc -s 1 --amiibo
```

**Why is this needed?** Real NTAG215 chips never reveal their stored password over NFC — it always reads back as zeros. So Flipper `.nfc` dumps always have `00 00 00 00` for the password (page 133) and PACK (page 134). Without the correct password, readers that perform `PWD_AUTH` (like the Nintendo Switch) will reject the emulated tag.

The `--amiibo` flag automatically derives the correct 4-byte password from the tag's UID and sets PACK to the standard `0x80 0x80`, matching what Nintendo devices expect.

### Step 6: Verify (Optional)

Want to double-check it worked? Run these commands:

```
hf mfu econfig -s 1
```

This shows the UID, ATQA, SAK, and tag type for slot 1. It should match what was in your `.nfc` file.

```
hf mfu eview
```

This dumps all the page data from the emulator. Compare it with the pages in your `.nfc` file to confirm everything loaded correctly.

---

## Quick Reference

| What you want to do | Command |
|---|---|
| Import to slot 1 | `hf mfu nfcimport -f myfile.nfc -s 1` |
| Import amiibo to slot 1 | `hf mfu nfcimport -f myfile.nfc -s 1 --amiibo` |
| Import to active slot | `hf mfu nfcimport -f myfile.nfc` |
| Check slot config | `hf mfu econfig -s 1` |
| View page data | `hf mfu eview` |
| Switch active slot | `hw slot change -s 2` |

---

## Troubleshooting

**"File not found"**
Check your file path. Remember you can drag the file from Finder into Terminal to get the exact path.

**"Unsupported Flipper device type"**
Your `.nfc` file contains a tag type that isn't in the Ultralight/NTAG family. This command only handles Ultralight and NTAG tags. For Mifare Classic, you'll need a different workflow using `hf mf` commands.

**"Please connect to chameleon device first"**
You forgot to run `hw connect` before the import command.

**"Chameleon not found"**
- Check that your USB cable supports data (some cables are charge-only)
- Try a different USB port
- For BLE: make sure the device is paired and not connected to another app

**The import ran but scanning the CU doesn't work as expected**
- Make sure the CU is in **tag emulation mode** (not reader mode). The device should already be in tag mode by default.
- Try pressing the button on your CU to cycle to the slot you loaded the tag into.
- **For amiibos:** If a reader (e.g., Nintendo Switch) detects the tag but rejects it, re-import with `--amiibo` to set the correct password. Real NTAG215 chips never reveal their password over NFC, so Flipper dumps always have zeros for it. The `--amiibo` flag derives the correct password from the UID.
