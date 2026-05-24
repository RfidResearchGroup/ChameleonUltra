# Standalone Modes — Tester's Guide

This is a host-less workflow subsystem for ChameleonUltra. With it, you can
configure the device once over USB/BLE, walk away with the device, run
captures or workflows using the two physical buttons, then come back and
pull the results.

This guide walks through bench-testing the feature end-to-end. It assumes
you have a ChameleonUltra (not Lite — the only mode that does anything
useful, AuthTrace, needs reader hardware) and that you've built and flashed
the firmware with the standalone-modes patch applied.

If you find something that doesn't behave the way this doc describes,
that's exactly the kind of bug I want to hear about.


## What you need

- A **ChameleonUltra** with the standalone-modes firmware flashed.
- The matching **CLI** (`chameleon_cli_main.py` from the same patch set —
  the firmware exposes new commands the unpatched CLI doesn't know about).
- A **MIFARE Classic** card (1K or 4K) for AuthTrace testing. Any card
  you have keys for works; even a card you don't have keys for is useful
  for capturing key-discovery traces.
- Optional: `mfkey32v2` from the Proxmark3 repo, if you want to verify
  that captured traces actually recover keys.


## Quick start (5 minutes)

This is the lowest-friction path — uses CLI for everything, so you can verify
the firmware works without touching device buttons.

```
# 1. Confirm the firmware speaks the new commands
chameleon --> standalone status
state: DISARMED  mode: DISABLED  flags: (none)

# 2. Pick a mode and configure it
chameleon --> standalone set-mode authtrace
ok: state=DISARMED mode=AUTHTRACE flags=0x00

chameleon --> standalone config --block 4 --key-type A --key FFFFFFFFFFFF --timeout 5000 authtrace
ok: type=A block=4 timeout=5000ms key=ffffffffffff

# 3. Place a MIFARE card on the CU antenna, then trigger a capture
chameleon --> standalone trigger
triggered

# 4. Pull the result
chameleon --> standalone get-result --dump
1 authtrace sessions

=== session #0  status=ok ===
  -->  [  7 bits]  26
  <--  [ 16 bits]  0400
  -->  [ 16 bits]  9320
  <--  [ 40 bits]  XXXXXXXXXX        (your card's UID + BCC)
  -->  [ 72 bits]  9370...
  <--  [ 24 bits]  08...             (SAK + CRC)
  -->  [ 32 bits]  6004XXXX          (AUTH KEY_A block 4 + CRC)
  <--  [ 32 bits]  XXXXXXXX          (NT - tag nonce)
  -->  [ 64 bits]  XXXXXXXXXXXXXXXX  (NR || AR)
  <--  [ 32 bits]  XXXXXXXX          (AT)
```

If you got something that looks like the output above, the feature works. The
rest of this doc is about exercising the device-side buttons and the other
modes.


## Chord gestures (reference)

Standalone uses **simultaneous-press chords** to avoid conflicting with the
existing single-button `SettingsButtonFunction` config. Hold both A and B at
the same time, then release them together. Duration is measured from "both
held" to "both released":

| Duration | Event | Action |
|---|---|---|
| < 1 second | `BOTH_SHORT` | Mode's primary action (capture / advance slot / etc.) |
| 1 – 5 seconds | `BOTH_LONG` | Toggle arm / disarm |
| ≥ 5 seconds | `BOTH_VLONG` | Destructive — clear captured data |

The trickiest one is `BOTH_SHORT` — both buttons have to register pressed
together, then both released together, within a second. Easier with thumb
and index finger than two thumbs.

Single-button presses (just A or just B) keep doing whatever your normal
button settings say. Standalone never touches that.


## LED feedback

CU has 8 RGB slot LEDs, all driven by one shared color. The feedback patterns:

| Pattern | Meaning |
|---|---|
| Color sweep across LEDs, then solid | Just armed in the mode color |
| Reverse sweep, then off | Just disarmed |
| Red double-flash | Refused (mode needed `--opt-in`) |
| Green triple-flash | Successful operation (e.g. card captured) |
| Red triple-flash | Failed operation (e.g. buffer full) |
| Quick mode-color wave | Long operation starting/ending |

Mode colors:

| Mode | Color | Notes |
|---|---|---|
| AuthTrace | Magenta | May appear as blue/cyan on some CU hardware |
| Slot Cycle | Yellow | |

If magenta looks wrong on your unit, that's a known cosmetic issue —
the underlying capture still works, it's just the indication that's off.
Cosmetic palette tweaks live in `firmware/application/src/standalone_led.c`
at `m_palette[]`.


## AuthTrace mode workflow

What it does: each trigger fires one HF14A reader-side authentication
attempt and captures the complete wire trace (REQA, ATQA, anticollision,
SELECT, SAK, RATS/ATS if present, auth command, NT, NR||AR, AT). Stores up
to 8 sessions; you pull them when you're back at the bench.

Use case: walk up to a reader/card pair, capture 2+ sessions on the same
block with the same key, walk back, feed the traces to mfkey32v2 to recover
the actual block key.

**Bench test — device buttons only:**

```
# Set the mode (still over CLI — modes persist in FDS)
chameleon --> standalone set-mode authtrace
chameleon --> standalone config --block 4 --key-type A --timeout 3000 authtrace

# Disconnect USB if you want a fully host-less test. Optional.

# === Now on the device ===
# Hold both buttons ~2 seconds → magenta LED sweep → ARMED
# Place card on antenna
# Brief simultaneous tap of both buttons → quick flash → one session captured
# Repeat with same card 2-3 times (need ≥2 sessions for mfkey32v2)
# Hold both ~2 seconds → reverse sweep → DISARMED

# === Reconnect USB ===
chameleon --> standalone get-result --dump
# Shows the captured sessions
```

**Verifying the data with mfkey32v2:**

From any pair of sessions on the same card/block/key-type, extract:

```
UID  = first 4 bytes of the anticollision response (5-byte response, last byte is BCC)
NT   = the 4-byte tag response after the auth command
NR   = first 4 bytes of the 8-byte reader response  
AR   = last 4 bytes of the 8-byte reader response
```

Then:

```
mfkey32v2 <UID> <NT0> <NR0> <AR0> <NT1> <NR1> <AR1>
```

If mfkey32v2 prints a 12-hex-char key, the capture is good and the key
actually works for that block + key-type.

**Config knobs:**

```
standalone config --block N --key-type A|B --key <12 hex> --timeout MS authtrace
```

- `--block`: target block to authenticate against. Defaults to 4 (first
  block of sector 1, typical user data on a fresh card). For
  key-discovery use 0 (manufacturer block, every card has it).
- `--key-type`: `A` or `B`. Default `A`.
- `--key`: 6-byte candidate key as 12 hex chars. Doesn't need to be
  correct — auth-fail sessions are still useful (NT + NR||AR are what
  mfkey32v2 wants). Default `FFFFFFFFFFFF`.
- `--timeout`: how long the device polls for a card before giving up,
  in ms. 100–30000. Default 3000.

Read the current config (no setters, no write):

```
standalone config authtrace
```


## Slot Cycle mode workflow

What it does: rotates through enabled emulation slots at a configurable
interval. No tag interaction, no flash writes, no destructive side
effects. The simplest possible reference mode — handy for verifying the
chord/arm/disarm path on a unit without reading the AuthTrace output.

**Bench test:**

```
chameleon --> standalone set-mode slot_cycle

# Hold both buttons ~2s → yellow LED sweep → ARMED, starts cycling
# Watch the slot LEDs change every few seconds
# Brief tap of both → manually advance one slot
# Hold both ≥ 5s → pause/resume (slot_cycle's VLONG action)
# Hold both ~2s → disarm
```

By default it cycles through all 8 slots at 3000 ms each. Currently the only
way to change timing is to set the config blob via raw bytes (no fancy
config flags in the CLI yet). If you need that, file a bug.


## CLI reference

All commands live under the `standalone` subgroup:

```
standalone status                     Show current state, mode, flags
standalone set-mode <name> [--opt-in] Pick the active mode
standalone config <mode> [knobs...]   Read/write mode-specific config
standalone trigger                    Manually fire the active mode's primary action
standalone get-result [--dump|--json|--raw|-f FILE]
                                      Read the active mode's result buffer
standalone clear-result               Discard captured results
```

**Mode names:** `disabled`, `autoclone`, `read_replay`, `authtrace`,
`slot_cycle`, `dict_check`. (Only `authtrace` and `slot_cycle` are
implemented in the current build; the others are placeholders.)

**`--opt-in`** is required for any future mode that writes to tag memory
or modifies emulation slots. Not needed for AuthTrace or Slot Cycle.


## Persistence behavior

What survives a reboot:

- Selected mode and flags (saved to FDS file `0x1010`, record `0x0001`)
- Per-mode config blobs (saved to FDS file `0x1010`, record `0x0100 + mode_id`)

What does NOT survive a reboot:

- The armed/disarmed state — every boot starts DISARMED, even if you were
  armed when power was lost
- Captured session data (lives in RAM only)

This means: configure once, captures must be pulled before powering off.
If you walk away with a USB battery and capture 8 sessions over hours,
make sure the device stays powered until you can drain the result buffer.


## Troubleshooting

**`standalone` subcommand doesn't exist in the CLI**

You're using an unpatched CLI. The standalone commands are added by the
same patch as the firmware — both halves need to be in sync.

**Chord gesture doesn't arm the device (no LED sweep)**

The two buttons need to press and release within roughly the same 50ms
window for the chord rescue path to catch them. Practice with a
deliberate "press both, hold, release both" motion. The status query
will tell you whether arming succeeded — `state: ARMED_IDLE` means yes.

**LEDs are the wrong color**

CU's LED hardware has some quirks where certain RGB enum values render
differently on different units. Doesn't affect functionality. Swap colors
in `standalone_led.c::m_palette[]` if it bothers you.

**Device disconnects ("Serial Error")**

If you can reproduce a disconnect with a specific sequence of commands,
please report it with the exact command order and what was happening on
the device (armed/disarmed, captures in buffer, etc.). The most useful
debugging info is whether the disconnect is deterministic or
intermittent.

**AuthTrace captures with `status=no_tag`**

The device polled for a card during the timeout window and didn't find
one. Check: is the card actually in the antenna's field? CU's HF antenna
is at the top of the device, sweet spot is about 0–10mm from the card.

**AuthTrace captures with `status=auth_fail`**

Tag responded but the auth didn't complete — usually means the candidate
key in your config is wrong. That's fine — the NT and NR||AR from the
trace are still captured and useful for mfkey32v2 attacks. You don't
need correct keys to capture key-recovery traces.

**`get-result` returns empty after captures**

Result buffer is RAM-only and clears on reboot. If the device reset
between capture and read, the captures are gone. If you didn't reboot,
double-check the active mode matches the mode you captured in.


## Reporting bugs

Useful details to include in a bug report:

- Firmware version (`hw version` output)
- Hardware variant (Ultra/Lite, hw rev if you know it)
- Exact CLI command sequence to reproduce
- What you expected to happen
- What actually happened (verbatim CLI output, with the prompt lines)
- Whether it's deterministic (always happens) or intermittent

If the device crashes, including the state it was in (`standalone status`
output right before the crash) is high-value. If you have SEGGER RTT
hooked up to read `NRF_LOG_*` output, attach the log lines — the
standalone subsystem emits diagnostic logs that pinpoint a lot of
failures.
