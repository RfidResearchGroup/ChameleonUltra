# Recovery Build — revert from the UF2 bootloader back to stock

This fork ships a built-in path back to upstream stock firmware, **without SWD**.

The mechanism: build a small "recovery" application that embeds the stock
bootloader (extracted from an upstream release zip). On boot it writes that
stock bootloader into the BL region at `0xF3000` from application context,
erases its own vector table so the new bootloader won't try to boot it again,
and resets. The device comes up on the stock bootloader, finds no valid app,
and drops to stock serial DFU — ready for a fresh stock application push.

All the source-level integration is already in place. To produce a recovery UF2
for a given upstream release you only point `build.sh` at the upstream zip.

---

## Quick start

Grab the upstream stable release zip from
<https://github.com/RfidResearchGroup/ChameleonUltra/releases>
(`ultra-dfu-full.zip` or `lite-dfu-full.zip`), then either:

**Produce a distributable recovery UF2:**

```bash
cd firmware
RECOVERY_ZIP=~/Downloads/ultra-dfu-full.zip ./build.sh
# → objects/ultra-revert-to-stock.uf2   (lite- prefix with CURRENT_DEVICE_TYPE=lite)
```

Distribute that `.uf2`. End users drag it onto the `CHAMELEON` drive (see END
USER FLOW), then push the stock app.

**Or do the whole revert locally, end to end:**

```bash
./revert-to-stock.sh ~/Downloads/ultra-dfu-full.zip
```

This runs the same `RECOVERY_ZIP` build, waits for the `CHAMELEON` drive and
copies the recovery UF2 onto it, then — once the device is back on the stock
serial DFU — flashes the stock package with `nrfutil device program`. Three
stages, all over USB, no SWD.

---

## How it works

When `RECOVERY_ZIP` is set, `build.sh` takes a different path:

- Skips the bootloader build (we're embedding the **stock** bootloader, not our
  composite one).
- Runs `tools/make_recovery_header.py` on the upstream zip: it parses the
  manifest, slices the bootloader bytes out of `sd_bl.bin` at the manifest's
  declared offset, sanity-checks the vector table (initial SP; reset handler
  must land in the BL region `0xF3000–0xFE000`), computes a CRC32, and emits
  `application/src/embedded_bootloader.h`.
- Builds the application with `make -j RECOVERY_MODE=1`, which gates the
  recovery hook at the very top of `main()` (`app_main.c`). In a recovery build,
  `main()` does only this before any peripheral or SoftDevice init:

  ```c
  (void)bl_updater_run_and_invalidate_app_force();
  while (1) { __WFE(); }   // only reached if the post-write verify failed
  ```

  `bl_updater_run_and_invalidate_app_force()` erases the BL region pages, writes
  the embedded stock BL bytes, verifies with a post-write `memcmp`, erases its
  own first page (the vector table), and resets. It's the `_force` variant: the
  runtime CRC32 check is skipped (the embedded image's CRC is validated at
  **build** time by `make_recovery_header.py`, and the post-write `memcmp` is
  the runtime integrity check that matters).
- Converts `application.hex` to UF2 and emits only
  `${device_type}-revert-to-stock.uf2`.

**Why a recovery *app*, and not just a UF2 of the stock bootloader?** The UF2
(GhostFAT) transport in this fork only writes the application window
`0x27000–0xF3000` (`uf2_ghostfat.h`); it rejects any block outside it
(`FAIL.TXT` → `OUT_OF_BOUNDS`) and any block whose family ID isn't `0x1B57745F`.
The bootloader region `0xF3000–0xFE000` is therefore **not** writable by a
drag-and-drop. The recovery app is the way in: it runs in the application region
(which UF2 *can* write) and rewrites the BL region from application context,
where the GhostFAT bounds don't apply. This is also why you can't just drop
`ultra-fullimage.uf2` to restore stock — its SD/BL blocks would be rejected.

The recovery UF2 contains only the recovery application (embedding the stock BL
as data). It does **not** carry the stock SoftDevice or stock application, so a
follow-up stock-app push (the upstream `flash-dfu-app.sh`, or `nrfutil` with the
upstream signed zip — `revert-to-stock.sh` does this in stage 3) is required.

---

## What to publish

Each recovery build prints the CRC32 of the embedded stock BL. Pin it in your
release notes so users can match the recovery UF2 to the stock release it
reverts to:

```
manifest: SD = 153140 bytes, BL = 43428 bytes
  initial SP    : 0x20038000
  reset handler : 0x000F4F74  (BL region — OK)
Wrote application/src/embedded_bootloader.h
  size  : 43428 bytes
  CRC32 : 0xF3AC4889          ← include in release notes
```

The Make output also confirms the recovery build is active:

```
Chameleon <Application>: RECOVERY build — embedding stock bootloader, target 0xF3000.
```

If you don't see that line, `RECOVERY_MODE` didn't propagate — `build.sh` didn't
get the env var, or a merge reverted the Makefile hook (see Appendix B).

Suggested filename: `chameleon-revert-to-stock-v<UPSTREAM_VERSION>.uf2`. The
version tag matters: if upstream changes the bootloader between releases, a
versioned name lets users match what they intend to revert to.

Before publishing, round-trip once on your own device: drop the recovery UF2,
confirm the device comes up on the stock bootloader in DFU, and confirm the
stock app push completes. The build-time CRC and post-write `memcmp` catch data
corruption, but not a packaging mistake where the wrong BL got embedded.

---

## End-user flow

1. Cold-boot the device, hold **B**, plug USB (UF2 DFU mode). The `CHAMELEON`
   drive appears.
2. Drag `chameleon-revert-to-stock-vX.uf2` onto the drive.
3. Wait a few seconds. **The drive will disappear — this is the success
   signal, not a failure.** The device resets twice: first into the recovery
   app (which writes the stock bootloader), then into the stock bootloader.
4. The stock bootloader boots, finds no valid app, and enters stock DFU
   automatically.
5. Push the stock application via the upstream release's `flash-dfu-app.sh` (or
   `nrfutil` with the upstream signed zip). `revert-to-stock.sh` does this for
   you.

Tell users up front that the drive vanishing in step 3 is expected — that's what
trips up first-timers.

To put **this fork** back afterward, just re-run the install
(`flash-dfu-sdbl.sh` over the stock serial DFU, then the application) — exactly
like a first-time install. Still no SWD.

---

## Safety notes

- **One-shot.** Once the recovery UF2 runs, the fork's UF2 bootloader is gone
  and the device is back on the stock (signed, serial-only) bootloader — there
  is no UF2 drive in that state. Reinstalling the fork is the normal install
  flow.
- **Self-destructing by design.** The recovery app erases its own vector table
  after a successful BL write; don't expect the device to "stay" on the recovery
  UF2. First invocation is the last.
- **CRC mismatch across releases.** If `make_recovery_header.py` prints a CRC
  that differs from your release notes for a given upstream version, the
  upstream BL bytes changed between releases — regenerate the recovery UF2.
- **Power-loss during the BL write (~1.4 s).** The only genuinely dangerous
  window is the BL erase + write itself. If power drops *after* the BL is
  written but *before* the self-destruct completes, it's safe: on next boot the
  still-valid recovery app runs again, force-writes the same stock BL bytes
  (idempotent — the post-write `memcmp` still passes), then self-destructs.
  Power loss *during* the erase/write leaves the BL region partially populated;
  that is the one situation an external programmer would be needed to recover
  from, so keep the device powered for those ~1.4 s.

---

## Appendix A — file layout

```
firmware/build.sh                               # RECOVERY_ZIP gate
firmware/revert-to-stock.sh                     # end-to-end local revert
firmware/application/Makefile                   # RECOVERY_MODE CFLAGS + $(info)
firmware/application/src/app_main.c             # main() recovery hook
firmware/application/src/bl_updater.c / .h      # BL write mechanism
firmware/application/src/embedded_bootloader.h  # regenerated per recovery build
firmware/tools/make_recovery_header.py          # recovery-build header gen
firmware/tools/gen_embedded_bl.py               # normal-build header gen
firmware/tools/uf2conv.py                       # ihex/bin -> uf2
firmware/bootloader/src/uf2_ghostfat.h          # UF2 app-window bounds
```

---

## Appendix B — porting these patches to another fork

Beyond dropping in `bl_updater.c/.h`, the tools, and `build.sh`:

**B.1 `firmware/application/Makefile`** — add `bl_updater.c` to `SRC_FILES`, and
after the device-type block add:

```make
ifeq ($(RECOVERY_MODE),1)
  CFLAGS += -DRECOVERY_MODE=1
$(info  Chameleon <Application>: RECOVERY build — embedding stock bootloader, target 0xF3000.)
endif
```

`build.sh` passes `RECOVERY_MODE=1` as a make variable (`make -j
RECOVERY_MODE=1`), so the `ifeq` fires. The `$(info)` line is a visible build
signal.

**B.2 `firmware/application/src/app_main.c`** — near the top, alongside the
other conditional includes:

```c
#ifdef RECOVERY_MODE
#include "bl_updater.h"
#endif
```

and as the VERY FIRST statement in `main()` (before `hw_connect_init()` and any
SoftDevice init):

```c
int main(void) {
#ifdef RECOVERY_MODE
    (void)bl_updater_run_and_invalidate_app_force();
    while (1) { __WFE(); }   // only reached if the post-write verify failed
#endif
    hw_connect_init();        // existing init follows
    ...
```

Order matters: the SoftDevice must not be enabled when `bl_updater` runs. In a
non-recovery build the `#ifdef` compiles away and the normal init is unchanged.

**B.3 `firmware/build.sh`** — merge the `RECOVERY_ZIP` branch: when unset, run
`gen_embedded_bl.py` after the bootloader build so `embedded_bootloader.h`
always reflects the current BL; when set, skip the BL build, run
`make_recovery_header.py` on the zip, build with `make -j RECOVERY_MODE=1`, and
emit only `${device_type}-revert-to-stock.uf2`.

---

## Appendix C — design rationale

**Why `bl_updater_run_and_invalidate_app_force()` instead of `bl_updater_run()`?**
`bl_updater_run()` replaces the BL but leaves the application valid — correct
when updating one UF2 BL to a newer UF2 BL. For revert-to-stock the app *is* the
recovery app, and you specifically do not want the new stock bootloader to keep
booting it, so the `_and_invalidate_app` variants erase the app's first page
(the vector table) after the write. The stock BL then fails to validate the
now-headless app and falls through to DFU — exactly the state the user needs.
The `_force` suffix skips the runtime CRC32 gate (build-time CRC + post-write
`memcmp` are the real checks).

**Why can't a UF2 drag write the stock BL directly?** The GhostFAT transport
only accepts writes to `0x27000–0xF3000` and rejects the BL region as
`OUT_OF_BOUNDS`. ACL hardware flash protection has been removed from the
bootloader build (`main.c`) for open-source development, but that only drops the
hardware lock — the GhostFAT bounds check is still in force. So rewriting the BL
region over USB must go through the application-side `bl_updater`, which writes
`0xF3000` from application context (with a post-write `memcmp` verify). That's
what the recovery UF2 does, and what the staged `${device_type}-bootloader.uf2`
handoff uses for BL self-updates. None of it needs SWD.
