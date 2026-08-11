# Installing the UF2 Bootloader on ChameleonUltra

A practical guide to building and flashing the composite UF2 bootloader from
[`github.com/nieldk/ChameleonUltra`](https://github.com/nieldk/ChameleonUltra)
onto a stock ChameleonUltra.

**No SWD required — for install or for going back to stock.** Everything is
done over USB: the stock serial DFU flashes the SoftDevice + composite
bootloader, UF2 drag-and-drop flashes the application, and the revert flow
rewrites the bootloader from application context. You never need a J-Link or
any programmer.

After installation the bootloader is a composite USB device — a USB Mass-Storage
drive for drag-and-drop `.uf2` updates *and* a CDC serial-DFU port for
`nrfutil` / the `flash-*.sh` scripts, both live at the same time.

Ultra and Lite are both supported; the scripts auto-detect which is attached.

---

## What you get

- **Drag-and-drop app updates.** Enter DFU, drop `ultra-application.uf2` onto
  the `CHAMELEON` drive, done.
- **Serial DFU alongside it.** A CDC ACM serial-DFU port for
  `flash-dfu-app.sh` / `flash-dfu-full.sh` / `flash-dfu-sdbl.sh`, all over USB.
- **No CDC driver for the UF2 path.** Mass storage is universal — Linux, macOS,
  Windows, ChromeOS.
- **A path back to stock, also without SWD** — `revert-to-stock.sh` (see
  `RECOVERY_BUILD.md`).

The one thing to know about the UF2 drive: it writes the **application region
only** (`0x27000–0xF3000`). The SoftDevice and bootloader are updated over
serial DFU, not by dragging a file. Details in Step 7.

---

## Prerequisites

Hardware: a ChameleonUltra or Lite, and a USB-C cable. A computer running Linux
or macOS (Windows via WSL). No programmer or SWD adapter.

Software — ARM toolchain, Python, git:

```bash
# Arch
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib python python-pip git
# Debian / Ubuntu / Kali
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi python3-pip git
# macOS
brew install --cask gcc-arm-embedded && brew install python git
```

**nRF Util (v7 or newer).** The build and flash scripts use the modern, modular
`nrfutil` — a standalone native binary, **not** the legacy `pip install nrfutil`
(v6.x), which lacks the commands used here. Download the `nrfutil` binary from
Nordic's [nRF Util page](https://www.nordicsemi.com/Products/Development-tools/nRF-Util),
put it on your `PATH`, then add the two plugins the scripts call:

```bash
nrfutil install nrf5sdk-tools   # build.sh:  pkg generate, settings generate
nrfutil install device          # flash-*.sh: nrfutil device program
```

**mergehex** comes from Nordic's
[nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools)
and is used by `build.sh` to merge the full-image hex. Install it and put its
`bin/` on your `PATH`.

Verify:

```bash
arm-none-eabi-gcc --version          # 10.x or newer
nrfutil --version                    # 7.0.0 or newer
nrfutil nrf5sdk-tools --help         # plugin present
nrfutil device --version             # plugin present
mergehex --version
```

---

## Step 1 — clone and fetch tags

```bash
git clone -b UF2 https://github.com/nieldk/ChameleonUltra.git
cd ChameleonUltra
git remote add upstream https://github.com/RfidResearchGroup/ChameleonUltra.git
git fetch upstream --tags
```

The build derives its version string from `git describe`, and tags live on
upstream. Confirm:

```bash
git describe --tags        # e.g. v2.0.0-15-gabc1234
```

If it reports "No names found": `git fetch upstream 'refs/tags/*:refs/tags/*'`.

---

## Step 2 — build

```bash
cd firmware
./build.sh                              # Ultra (default)
CURRENT_DEVICE_TYPE=lite ./build.sh     # Lite
```

Tail of a successful build:

```
==========================================================
Build complete.
  SD+BL      : objects/ultra-dfu-sdbl.zip
  App        : objects/ultra-dfu-app.zip
  Full image : objects/ultra-fullimage.uf2
Use flash-dfu-sdbl.sh to install both stages.
==========================================================
```

Artifacts land in `firmware/objects/` (Lite builds carry a `lite-` prefix):

| File                    | Purpose                                                                                            |
|-------------------------|----------------------------------------------------------------------------------------------------|
| `ultra-dfu-sdbl.zip`    | **Install step 1.** SoftDevice + composite bootloader, for `flash-dfu-sdbl.sh` (serial DFU).       |
| `ultra-application.uf2` | **Install step 2, and every app update.** Drag-and-drop application image.                         |
| `ultra-dfu-app.zip`     | App-only package for the serial-DFU path (`flash-dfu-app.sh`).                                      |
| `ultra-dfu-full.zip`    | SD + BL + app package for the serial-DFU path (`flash-dfu-full.sh`).                                |
| `ultra-fullimage.uf2`   | Whole-image UF2 (MBR + SD + BL + app). Only the app region is honoured over the UF2 drive; the full file is for an external programmer if you ever use one. |
| `ultra-bootloader.uf2`  | Bootloader-only UF2, used internally by the staged bootloader self-update.                          |
| `ultra-binaries.zip`    | Raw hex files, for inspection or an external programmer.                                            |
| `fullimage.hex`         | Merged SD + BL + app hex, for an external programmer.                                               |

---

## Step 3 — enter serial DFU

`flash-dfu-sdbl.sh` (Step 4) does this **for you** via
`resource/tools/enter_dfu.py`, which talks to the running device over serial and
commands a reboot into DFU. The manual sequence is only the fallback:

1. **Unplug** the device.
2. Press and hold **B** (the side button).
3. **Plug in** USB while still holding **B**.
4. Hold ~2 seconds, then release.

In serial-DFU mode the device enumerates as `1915:521f` and LEDs 4 & 5 blink.

---

## Step 4 — flash the composite bootloader (serial DFU)

```bash
./flash-dfu-sdbl.sh
```

The script auto-detects Ultra vs Lite, enters DFU (auto, with the Step 3
fallback), waits for the serial-DFU device (`1915:521f`), and programs the
SoftDevice + composite bootloader with `nrfutil device program`:

```
=== Flashing composite bootloader (SD+BL) via serial DFU ===
Waiting for serial DFU device (1915:521f)...
<nrfutil transfer progress>
Done. Composite bootloader (UF2 + CDC serial DFU) installed at 0xF3000.
Flash the application with ./flash-dfu-app.sh or drag the app UF2.
```

The composite bootloader fits the stock 44 KB region at `0xF3000`, so the stock
serial DFU accepts it directly — no two-stage, no SWD.

---

## Step 5 — flash the application

The device is now on the composite bootloader with no application. Two ways,
both over USB:

**UF2 drag-and-drop (recommended):**

```bash
./flash-uf2-app.sh
```

Waits for the `CHAMELEON` drive (enter UF2 mode: hold **B**, plug USB), copies
`ultra-application.uf2` onto it, device resets into the app:

```
Waiting for CHAMELEON UF2 drive...
Enter UF2 mode: hold B and plug USB
Found at /run/media/<user>/CHAMELEON — flashing objects/ultra-application.uf2 ...
Done. Device will reset into application.
```

**Or serial DFU:** `./flash-dfu-app.sh` (pushes `ultra-dfu-app.zip`).

---

## Step 6 — verify

Unplug and replug **without** holding B — the device boots normally.

To confirm the UF2 path, enter UF2 mode (hold **B**, plug USB). The `CHAMELEON`
drive mounts (auto on most Linux desktops; Finder/File Explorer on macOS/Windows);
`dmesg` shows a `RRG ChameleonUltra` USB Mass Storage device. `INFO_UF2.TXT`
present with no `FAIL.TXT` means the last flash was clean.

---

## Step 7 — future updates

**Enter UF2 mode** by cold-boot + hold **B** + plug, or from the running app:

```bash
$ python3 software/script/chameleon_cli_main.py
[chameleon] hw dfu
```

`hw dfu` sends `ENTER_BOOTLOADER` and reboots into the composite bootloader.
(`resource/tools/enter_dfu.py` and `enter_dfu_over_ble.py` do the same over USB
and BLE.)

**App update** — drop the new UF2 on the drive:

```bash
./build.sh
cp objects/ultra-application.uf2 /run/media/$USER/CHAMELEON/
```

**SoftDevice / bootloader / everything** — over serial DFU (still no SWD):

```bash
./flash-dfu-full.sh         # SD + BL + app  (ultra-dfu-full.zip)
./flash-dfu-sdbl.sh         # SD + BL only   (ultra-dfu-sdbl.zip)
```

### What the UF2 drive accepts

The mass-storage (GhostFAT) transport honours a UF2 block only if its family ID
is `0x1B57745F` (Ultra and Lite share it) **and** its target address is inside
the application window `0x27000 – 0xF3000`. Anything else is rejected and a
`FAIL.TXT` appears:

```bash
cat /run/media/$USER/CHAMELEON/FAIL.TXT
```

| Reason          | Cause                                                                | Fix                                    |
|-----------------|----------------------------------------------------------------------|----------------------------------------|
| `WRONG_FAMILY`  | Family ID isn't `0x1B57745F` (a stock or other-target UF2)           | Rebuild from this fork                 |
| `OUT_OF_BOUNDS` | Target outside `0x27000–0xF3000` (SoftDevice or bootloader region)   | Use serial DFU (above) for those       |

So dragging `ultra-fullimage.uf2` onto the drive only writes its app-region
blocks — the SD/BL blocks are rejected. To change the SoftDevice or bootloader,
use the serial-DFU packages, not the drive.

---

## Step 8 — going back to stock (no SWD)

```bash
./revert-to-stock.sh ~/Downloads/ultra-dfu-full.zip
```

Builds a recovery UF2 from the stock release zip, drops it on the drive (the
recovery app rewrites the bootloader region back to stock at `0xF3000` from
application context), then flashes the stock package over serial DFU. Full
mechanism, and how to produce a distributable recovery UF2, in
`firmware/tools/RECOVERY_BUILD.md`.

---

## Troubleshooting

**`nrfutil: command not found` / `unknown command nrf5sdk-tools`** — you need
the modern nrfutil (v7+) on `PATH` plus plugins. `nrfutil --version` must be
7.x; then `nrfutil install nrf5sdk-tools` and `nrfutil install device`. If it's
6.x or lower, `pip uninstall nrfutil` and install the standalone binary.

**`mergehex: command not found`** — install nRF Command Line Tools, add its
`bin/` to `PATH`.

**`Unknown CURRENT_DEVICE_TYPE`** — set `CURRENT_DEVICE_TYPE=ultra` or `=lite`
before `./build.sh` (default `ultra`).

**No `1915:521f` serial-DFU device** — auto-entry couldn't reach the device;
use the manual Step 3 sequence (LEDs 4 & 5 blink in DFU).

**`CHAMELEON` drive doesn't appear** — confirm the bootloader flash finished.
Linux: `sudo dmesg -w` and replug; `config 1 has an invalid interface number`
means a corrupt bootloader build — rebuild clean. Windows: try another USB port
(descriptor caching). macOS: `system_profiler SPUSBDataType`.

**`FAIL.TXT` after a drop** — read it; see the reasons table in Step 7.

---

## Where to ask for help

- **Issues:** [github.com/nieldk/ChameleonUltra/issues](https://github.com/nieldk/ChameleonUltra/issues)
- **Design write-up:** [sec1.dk](https://sec1.dk)

---

## Acknowledgments

- **Nordic nRF5 SDK 17** — DFU framework and USBD class layer.
- **Microsoft UF2** — the drag-and-drop firmware format.
- **Adafruit tinyuf2** — reference GhostFAT implementation.
- **RfidResearchGroup ChameleonUltra** — upstream application and stock
  bootloader this fork modifies.
