Installing the UF2 Bootloader on ChameleonUltra

A practical guide to building and flashing the UF2 bootloader from
[`github.com/nieldk/ChameleonUltra`](https://github.com/nieldk/ChameleonUltra)
onto a stock ChameleonUltra device. After installation, firmware updates
become drag-and-drop onto a USB mass-storage drive — no signed zips, no
nrfutil, no driver install required for day-to-day updates.

This guide assumes you have a working stock ChameleonUltra and want to
replace its signed-DFU bootloader with the UF2 one.

-----

What you'll get

After installation:

- **Drag-and-drop updates.** Boot into DFU mode, drag a `.uf2` file onto
  the `CHAMELEON` drive that appears, done.
- **Serial DFU coexistence.** The bootloader presents a composite USB
  device: a CDC ACM serial port for signed DFU (nrfutil / `flash-dfu-app.sh`)
  alongside the MSC mass-storage drive for UF2. Both work simultaneously.
- **No signing infrastructure for development builds.** Useful if you're
  iterating on firmware modifications.
- **No CDC driver required for UF2.** USB Mass Storage is universal — works
  on Linux, macOS, Windows, even bare ChromeOS.
- **A path back.** Drop `ultra-fullimage.uf2` (built by `build.sh`) onto
  the CHAMELEON drive to restore this fork's complete image (SoftDevice +
  UF2 bootloader + application). To return to the *stock* bootloader
  instead, use `revert-to-stock.sh` (Step 8).

-----

Prerequisites

Hardware

- A ChameleonUltra (this guide targets the Ultra; Lite should work
  identically but is untested by me).
- USB-C cable.
- A computer running Linux or macOS (Windows works under WSL).
- **Recommended but not required:** an SWD adapter (e.g., J-Link EDU
  Mini, Black Magic Probe) for emergency recovery. The whole point of
  this design is that you shouldn't need one — but bricks happen.

Software

On Arch Linux:

```bash
sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib python python-pip git
```

On Debian / Ubuntu / Kali:

```bash
sudo apt install gcc-arm-none-eabi libnewlib-arm-none-eabi python3-pip git
```

On macOS (via Homebrew):

```bash
brew install --cask gcc-arm-embedded
brew install python git
```

**nRF Util (v7 or newer).** The build and flash scripts use the modern,
modular nrfutil — a standalone native binary. This is **not** the legacy
`pip install nrfutil` (v6.x and earlier); that package lacks the
`nrf5sdk-tools` and `device` commands the scripts call and will not work.
Download the `nrfutil` binary for your OS from Nordic's
[nRF Util page](https://www.nordicsemi.com/Products/Development-tools/nRF-Util),
put it on your `PATH`, then install the two command plugins:

```bash
nrfutil install nrf5sdk-tools   # used by build.sh: pkg / settings generate
nrfutil install device          # used by flash-dfu-sdbl.sh: device program
```

You'll also need Nordic's [nRF Command Line Tools](https://www.nordicsemi.com/Products/Development-tools/nRF-Command-Line-Tools)
for the `mergehex` utility (Nordic is archiving these in favour of nRF Util,
but `build.sh` still uses `mergehex` for the hex merge). Install the
appropriate `.deb` / `.rpm` / `.dmg` from Nordic's site.

Verify your toolchain:

```bash
arm-none-eabi-gcc --version          # should be 10.x or newer
nrfutil --version                    # 7.0.0 or newer
nrfutil nrf5sdk-tools --help         # confirms the plugin is installed
nrfutil device --version             # confirms the plugin is installed
mergehex --version                   # comes from nRF Command Line Tools
```

-----

Step 1 — clone the fork

```bash
git clone -b UF2 https://github.com/nieldk/ChameleonUltra.git
cd ChameleonUltra
```

Add the upstream remote and fetch tags — the build uses `git describe`
to generate the firmware version string, and tags only exist on upstream:

```bash
git remote add upstream https://github.com/RfidResearchGroup/ChameleonUltra.git
git fetch upstream --tags
```

Verify tags resolved:

```bash
git describe --tags
# Should output something like v2.0.0-15-gabc1234
```

If it complains "No names found", run:

```bash
git fetch upstream 'refs/tags/*:refs/tags/*'
```

-----

Step 2 — build the firmware

```bash
cd firmware
./build.sh
```

A normal build takes 30 – 90 seconds depending on your machine. Expect
to see output ending in something like:

```
==========================================================
Build complete.
  SD+BL      : objects/ultra-dfu-sdbl.zip
  App        : objects/ultra-dfu-app.zip
  Full image : objects/ultra-fullimage.uf2
Use flash-dfu-sdbl.sh to install both stages.
==========================================================
```

If the build fails on `mergehex: command not found`, the nRF Command
Line Tools aren't on your `PATH`. Add their `bin/` directory to it.

The build produces these artifacts in `firmware/objects/`:

| File                    | Purpose                                                                                          |
|-------------------------|--------------------------------------------------------------------------------------------------|
| `ultra-dfu-sdbl.zip`    | **First-time install, step 1.** SoftDevice + UF2 bootloader, signed for the stock serial-DFU flow. |
| `ultra-application.uf2` | **First-time install, step 2, and all future updates.** Drag-and-drop application image.        |
| `ultra-fullimage.uf2`   | This fork's complete image (SD + BL + app) as one UF2. Drag-and-drop to restore the fork in one shot. |
| `ultra-bootloader.uf2`  | Bootloader-only UF2. Used internally for the stage-1 → stage-2 handoff.                          |
| `ultra-dfu-full.zip`    | Combined signed package (SD + BL + app) for the stock serial-DFU flow (`flash-dfu-full.sh`).    |
| `ultra-dfu-app.zip`     | Signed app-only zip. Only useful if still on the stock bootloader.                               |
| `ultra-binaries.zip`    | Raw hex files for SWD flashing or inspection.                                                    |
| `fullimage.hex`         | Combined SoftDevice + Bootloader + Application hex. Flash via SWD if you have one.              |

Building for the Lite (`CURRENT_DEVICE_TYPE=lite ./build.sh`) produces the
same set with a `lite-` prefix. The flash scripts auto-detect Lite vs Ultra
from USB and pick the matching files, so the steps below are identical for
either target.

-----

Step 3 — put the stock device into DFU mode

> Step 4's `flash-dfu-sdbl.sh` attempts this automatically via
> `resource/tools/enter_dfu.py`. The manual sequence below is the fallback
> if auto-entry doesn't take.

The stock ChameleonUltra enters DFU mode via this sequence:

1. **Unplug** the device.
1. Press and hold the **B button** (the button on the side of the device).
1. While still holding **B**, **plug in** the USB cable.
1. Continue holding **B** for ~2 seconds after plug-in.
1. Release **B**.

The device should now appear as a CDC serial device. On Linux:

```bash
dmesg | tail -5
# Expect: cdc_acm 1-6:1.0: ttyACM0: USB ACM device
```

If it doesn't appear as ACM, you're not in DFU mode. Unplug and try
again, holding **B** more deliberately during the plug-in.

-----

Step 4 — flash the UF2 bootloader and SoftDevice

From inside the `firmware/` directory:

```bash
./flash-dfu-sdbl.sh
```

The script auto-detects Lite vs Ultra from USB, tries to enter serial DFU
for you (`resource/tools/enter_dfu.py`, falling back to the manual
cold-boot + hold-**B** sequence from Step 3), waits for the serial DFU
device (`1915:521f`), then pushes the SoftDevice + UF2 bootloader
(`ultra-dfu-sdbl.zip`) with `nrfutil device program --traits nordicDfu`.

A successful run looks like:

```
=== Flashing composite bootloader (SD+BL) via serial DFU ===
Waiting for serial DFU device (1915:521f)...
[00:00:18]  100% [1/1] Image transfer complete
Done. Composite bootloader (UF2 + CDC serial DFU) installed at 0xF3000.
Flash the application with ./flash-dfu-app.sh or drag the app UF2.
```

-----

Step 5 — flash the application via UF2

After step 4 the device is running the UF2 bootloader with no
application installed. Flash the application:

```bash
./flash-uf2-app.sh
```

The script waits for the `CHAMELEON` drive to appear, then copies
`ultra-application.uf2` onto it. Enter UF2 DFU mode when prompted:

1. Unplug the device.
1. Press and hold **B**.
1. Plug in USB while still holding **B**.
1. Release after ~2 seconds.

The script detects the drive automatically and copies the image. The
device resets and boots the application when the transfer completes.

A successful run looks like:

```
Waiting for CHAMELEON UF2 drive...
Enter UF2 mode: hold B and plug USB
Found at /run/media/user/CHAMELEON — flashing objects/ultra-application.uf2 ...
Done. Device will reset into application.
```

-----

Step 6 — verify the install

After a successful flash, unplug and replug without holding **B**. The
device should boot normally and appear on the network / BLE as a
ChameleonUltra.

To verify UF2 mode still works:

1. Unplug.
1. Hold **B**, plug in USB, hold for ~2 seconds, release.

On Linux, `dmesg` should show:

```
usb 1-6: new full-speed USB device number 41 using xhci_hcd
usb 1-6: Product: ChameleonUltra
usb-storage 1-6:1.0: USB Mass Storage device detected
scsi 0:0:0:0: Direct-Access RRG ChameleonUltra 1.0 PQ:0 ANSI:6
sd 0:0:0:0: [sda] 4026 512-byte logical blocks: (2.06 MB)
sd 0:0:0:0: [sda] Attached SCSI removable disk
```

A `CHAMELEON` drive mounts automatically on Linux desktops with
auto-mount. If not:

```bash
sudo mkdir -p /mnt/chameleon
sudo mount /dev/sda /mnt/chameleon
ls /mnt/chameleon
# INFO_UF2.TXT visible — no FAIL.TXT means previous flash was clean
```

On macOS the drive appears in Finder as `CHAMELEON`. On Windows it
appears in File Explorer with the same label.

-----

Step 7 — use UF2 for future updates

Two ways to enter UF2 DFU mode going forward:

From cold start (always works)

Unplug, hold **B**, plug, release after 2 seconds. CHAMELEON drive
appears.

From the running application

If the firmware is running normally, reboot into the composite bootloader
(which brings up the CHAMELEON drive) from the CLI:

```bash
$ python3 chameleon_cli_main.py
[chameleon] hw dfu
```

`hw dfu` sends `ENTER_BOOTLOADER` and drops the USB link immediately. The
`resource/tools/enter_dfu.py` and `enter_dfu_over_ble.py` helpers do the
same over USB and BLE respectively.

Pushing an update

From either entry point, drag the new `.uf2` onto the drive:

```bash
# Build a new application
./build.sh

# Drop it onto the drive (Linux example)
cp objects/ultra-application.uf2 /run/media/$USER/CHAMELEON/

# Or via dd if auto-mount isn't working
sudo dd if=objects/ultra-application.uf2 of=/dev/sda bs=512 oflag=direct status=progress
```

The device resets automatically when the transfer completes.

Diagnosing a failed flash

If the transfer fails, a `FAIL.TXT` file appears on the `CHAMELEON`
drive (instead of the device resetting). Read it for the failure reason:

```bash
cat /run/media/$USER/CHAMELEON/FAIL.TXT
```

Common reasons and fixes:

| Reason          | Cause                                              | Fix                                      |
|-----------------|----------------------------------------------------|------------------------------------------|
| `WRONG_FAMILY`  | UF2's family ID isn't this fork's custom `0x1B57745F` (e.g. a stock or other-target UF2) | Rebuild from this fork; both Ultra and Lite use family `0x1B57745F` |
| `OUT_OF_BOUNDS` | UF2 targets address outside app region             | Check `--base` address in uf2conv call   |
| `WRITE_ERROR`   | Flash write or verify failed                       | Retry; may indicate worn flash           |

-----

Step 8 — going back to stock (if you ever need to)

These are two different operations — don't confuse them:

**Restore this fork's full image** (SoftDevice + UF2 bootloader + app):
drop `ultra-fullimage.uf2` onto the CHAMELEON drive in UF2 DFU mode. This
rewrites the fork in one shot; it does **not** return you to the stock
bootloader.

**Revert to the upstream stock bootloader** — use the scripted flow:

```bash
./revert-to-stock.sh /path/to/ultra-dfu-full.zip
# or drop the stock zip at ~/Downloads/ultra-dfu-full.zip and run it bare:
./revert-to-stock.sh
```

It takes the stock RRG release package (`ultra-dfu-full.zip` from the
[upstream releases](https://github.com/RfidResearchGroup/ChameleonUltra/releases)),
builds a recovery UF2 that embeds the stock SD+BL (`RECOVERY_ZIP=… ./build.sh`
→ `ultra-revert-to-stock.uf2`), and drops it onto the CHAMELEON drive. The
recovery app runs `bl_updater` to rewrite the bootloader region back to
stock at 0xF3000, then reboots into the stock bootloader in DFU mode, ready
for the stock application via the upstream serial DFU. See
`firmware/tools/RECOVERY_BUILD.md` for the underlying mechanism.

-----

Troubleshooting

CHAMELEON drive doesn't appear after install

Verify both flash steps completed. If both finished but the drive
still doesn't appear:

- **Linux:** `sudo dmesg -w` and replug. If you see
  `config 1 has an invalid interface number`, the bootloader build is
  corrupt — rebuild from a clean tree.
- **Windows:** Windows aggressively caches USB descriptors per
  (VID, PID, serial). Try plugging into a different USB port. On Linux
  it's not an issue.
- **macOS:** `system_profiler SPUSBDataType` will show what the device
  is actually presenting.

Build fails with `nrfutil: command not found` or `unknown command nrf5sdk-tools`

The scripts need the modern nrfutil (v7+) binary on your `PATH`, plus its
plugins. Confirm the version, then install the plugins:

```bash
nrfutil --version               # must report 7.x
nrfutil install nrf5sdk-tools
nrfutil install device
```

If `nrfutil --version` reports 6.x or lower you have the legacy pip
package; remove it (`pip uninstall nrfutil`) and install the standalone
binary from Nordic's nRF Util page.

Build fails with `mergehex: command not found`

Install nRF Command Line Tools from Nordic's site and add the `bin/`
directory to your `PATH`.

Build fails with "Unknown CURRENT_DEVICE_TYPE"

Set the device type explicitly:

```bash
CURRENT_DEVICE_TYPE=ultra ./build.sh   # for Chameleon Ultra
CURRENT_DEVICE_TYPE=lite ./build.sh    # for Chameleon Lite
```

Default is `ultra`.

dmesg shows "Hardware Error" on the SCSI device

The MSC enumerated but SCSI command handling is failing. This was a
bug in earlier UF2 builds. If you see this after building from the
current UF2 branch, file an issue.

FAIL.TXT appears on the drive after a flash attempt

Read the file for the specific reason. See the table in Step 7 above.

-----

Where to ask for help

- **Issues:** [github.com/nieldk/ChameleonUltra/issues](https://github.com/nieldk/ChameleonUltra/issues)
- **Background / design rationale:** the deep-dive blog post at
  [sec1.dk](https://sec1.dk) walks through the engineering of this UF2
  port, including the debugging path that led to the current design.

-----

Acknowledgments

This UF2 bootloader builds on:

- **Nordic's nRF5 SDK 17** — the DFU framework and USBD class layer.
- **Microsoft's UF2 specification** — the drag-and-drop firmware format.
- **Adafruit's tinyuf2** — reference implementation that informed the
  GhostFAT layout.
- **RfidResearchGroup's ChameleonUltra firmware** — the upstream
  application and stock bootloader this fork modifies.

The recovery-to-stock mechanism is original to this fork.
