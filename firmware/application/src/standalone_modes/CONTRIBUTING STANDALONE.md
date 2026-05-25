# Contributing to Standalone Modes

This document covers how to add a new standalone mode to ChameleonUltra.
For general project contribution guidelines (build setup, code style,
PR workflow) see the [development guide](https://deepwiki.com/RfidResearchGroup/ChameleonUltra/7-development-guide).

## What is a standalone mode?

A standalone mode is a self-contained workflow that runs on the device
without a connected host. The user configures it once over USB or BLE,
arms it with a button chord, operates it using the physical buttons, and
retrieves results when reconnected.

The framework handles:

- Persisting the selected mode and its config to flash (FDS)
- Arm/disarm state machine and button chord classification
- LED feedback primitives
- Host command dispatch (CMD 7000-7006)
- Python CLI plumbing (`standalone status/set-mode/trigger/get-result/config`)

A mode only needs to implement five callbacks and export one descriptor struct.

## Quick start

1. Copy `firmware/application/src/standalone_modes/mode_template.c` to
   `mode_<name>.c` in the same directory.
1. Follow the numbered `TODO` comments in the template.
1. Register the mode in the framework (see steps below).
1. Add CLI config support if your mode has user-configurable parameters.
1. Test on hardware — see the testing checklist at the bottom of this doc.

## Step by step

### 1. Assign a mode ID

Open `firmware/application/src/app_standalone.h` and add your mode to
`standalone_mode_t` **before** `STANDALONE_MODE__COUNT`:

```c
typedef enum {
    STANDALONE_MODE_DISABLED    = 0x00,
    STANDALONE_MODE_AUTOCLONE   = 0x01,
    STANDALONE_MODE_READ_REPLAY = 0x02,
    STANDALONE_MODE_AUTHTRACE   = 0x03,
    STANDALONE_MODE_SLOT_CYCLE  = 0x04,
    STANDALONE_MODE_DICT_CHECK  = 0x05,
    STANDALONE_MODE_MY_MODE     = 0x06,  /* <-- add here */

    STANDALONE_MODE__COUNT
} standalone_mode_t;
```

> **Important:** Mode IDs are persisted to flash. Never renumber or reuse
> an existing ID. Always append.

Add the extern declaration for your descriptor in the same file:

```c
extern const standalone_mode_iface_t mode_my_mode_iface;
```

If your mode requires reader hardware (RC522, not available on ChameleonLite),
gate the declaration:

```c
#if defined(PROJECT_CHAMELEON_ULTRA)
extern const standalone_mode_iface_t mode_my_mode_iface;
#endif
```

### 2. Register in the mode registry

Open `firmware/application/src/app_standalone.c`. Add a compile-time
enable flag with a default near the top:

```c
#ifndef CONFIG_STANDALONE_MY_MODE
#define CONFIG_STANDALONE_MY_MODE  1
#endif
```

Add the registry entry in `m_modes[]`:

```c
static const standalone_mode_iface_t * const m_modes[] = {
    ...
#if CONFIG_STANDALONE_MY_MODE
    &mode_my_mode_iface,
#endif
    ...
};
```

If reader hardware is required, combine the gates:

```c
#if CONFIG_STANDALONE_MY_MODE && defined(PROJECT_CHAMELEON_ULTRA)
    &mode_my_mode_iface,
#endif
```

### 3. Add to the Makefile

Open `firmware/application/Makefile` and add your source file to
`SRC_FILES`:

```makefile
$(PROJ_DIR)/standalone_modes/mode_my_mode.c \
```

### 4. Add an LED color

Open `firmware/application/src/standalone_led.c` and assign a color to
your mode in `m_palette[]`:

```c
static chameleon_rgb_type_t m_palette[STANDALONE_MODE__COUNT] = {
    ...
    [STANDALONE_MODE_MY_MODE] = RGB_CYAN,   /* pick an unused color */
};
```

Available colors: `RGB_RED`, `RGB_GREEN`, `RGB_BLUE`, `RGB_MAGENTA`,
`RGB_YELLOW`, `RGB_CYAN`, `RGB_WHITE`. Red and green are reserved for
error and success feedback respectively.

### 5. Add CLI support (optional)

If your mode has user-configurable parameters, add a config handler in
`software/script/chameleon_cli_unit.py` following the pattern in the
`StandaloneConfig.on_exec()` method. Add your mode name to the CLI
help text and add a parser for your config struct fields.

Add your mode to `StandaloneMode` in
`software/script/chameleon_enum.py`:

```python
class StandaloneMode(enum.IntEnum):
    ...
    MY_MODE = 0x06
```

## Implementing the callbacks

All callbacks run in the main loop context. See `mode_template.c` for
the full skeleton with inline documentation. Key rules:

### on_enter(cfg, cfg_len)

Called when the framework arms your mode. Apply your config, initialise
state, acquire any hardware resources you need. Return
`STANDALONE_RC_INVALID_STATE` if prerequisites are not met (e.g. a mode
that requires a specific slot to be configured).

Do **not** power on the HF antenna here.

### on_exit()

Called on disarm. Release hardware resources. If you entered reader mode,
restore emulation mode:

```c
pcd_14a_reader_antenna_off();
tag_mode_enter();
```

### on_button(evt)

Called with the classified chord gesture. `BOTH_LONG` is handled by the
framework and never reaches your mode. Handle `BOTH_SHORT` (primary
action) and `BOTH_VLONG` (typically: clear captured data).

May block for the duration of one RF operation. If an operation takes
more than one second, feed the watchdog:

```c
bsp_wdt_feed();
```

### on_tick(now_ms)

Called at approximately 10 Hz when `wants_tick = true`. Must return
quickly. No blocking, no `bsp_delay_ms()`, no FDS writes. Use for
timeout checking, state machine advancement, or periodic LED updates.

Set `wants_tick = false` and provide a NULL `on_tick` if your mode
does not need background processing.

### read_result / clear_result

The host drains the result buffer via repeated `GET_RESULT` calls.
Implement a `write_cursor` / `read_cursor` pattern (see
`mode_authtrace.c`). Return `STANDALONE_RC_NO_RESULT` when all data
has been read, signalling end-of-data to the host.

Result buffers are RAM only and are lost on reboot. Document this for
your users.

## Hardware constraints

### Antenna ownership

The HF antenna is switched between the nRF52 NFCT peripheral (tag
emulation) and the RC522 (reader). They cannot run simultaneously.

Power the antenna only during an active RF operation, never while
idle in ARMED state. Leaving the antenna on between operations causes
unexpected device resets due to interactions with BLE/USB stack and
power management.

Pattern to follow:

```c
pcd_14a_reader_reset();
pcd_14a_reader_antenna_on();
bsp_delay_ms(8);
/* ... perform operation ... */
pcd_14a_reader_antenna_off();
```

### FDS write alignment

`fds_write_sync()` requires 4-byte-aligned data. Stack-local buffers
with `__attribute__((aligned(4)))` are not reliable on nRF52 at
runtime. Use a file-static `uint32_t[]` staging buffer and `memcpy`
into it before writing:

```c
static uint32_t m_cfg_save_buf[(MY_CFG_SIZE + 3) / 4];

memset(m_cfg_save_buf, 0, sizeof(m_cfg_save_buf));
memcpy(m_cfg_save_buf, cfg, cfg_len);
fds_write_sync(file_id, record_key, cfg_len, m_cfg_save_buf);
```

### Destructive modes

Any mode that writes to tag memory or modifies slot data must set
`writes_tag` and/or `writes_slot` to `true` in its descriptor. The
framework will refuse to arm it unless the host has explicitly set
`STANDALONE_FLAG_HOST_OPTED_IN` via `standalone set-mode --opt-in`.

### ChameleonLite compatibility

ChameleonLite has no RC522. Any mode that requires reader hardware must
be conditionally compiled:

```c
/* mode_my_mode.c */
#if defined(PROJECT_CHAMELEON_ULTRA)

/* ... full implementation ... */

#endif /* PROJECT_CHAMELEON_ULTRA */
```

Apply the same gate to the extern decl in `app_standalone.h` and the
registry entry in `app_standalone.c`. Modes that do not require reader
hardware (slot manipulation, LF operations, etc.) should compile and
function on both variants.

## Testing checklist

Before submitting a PR, verify:

- [ ] Builds clean for both Ultra and Lite (`PROJECT_CHAMELEON_ULTRA`
  defined and undefined)
- [ ] `standalone status` returns correct state after `set-mode`
- [ ] Mode config persists across a power cycle (`standalone config <mode>` after reboot shows correct values)
- [ ] Arm / disarm via button chord works (ARMED_IDLE state confirmed
  with `standalone status`)
- [ ] Primary action (BOTH_SHORT) behaves correctly with and without a
  tag present
- [ ] BOTH_VLONG clears captured data
- [ ] `standalone get-result` returns data after captures and empty
  after `standalone clear-result`
- [ ] Device does not reset spontaneously while idle in ARMED state
  (leave armed for 30+ seconds and poll `standalone status`)
- [ ] `standalone trigger` from CLI produces the same result as the
  button chord
- [ ] No unexpected behavior on the existing modes after your change
  (`standalone set-mode slot_cycle`, arm, observe slot rotation)

## File layout reference

```
firmware/application/src/
    app_standalone.h              Framework interface, mode IDs, descriptor struct
    app_standalone.c              Framework implementation, mode registry, FDS
    app_cmd_standalone.c          Host command handlers (CMD 7000-7006)
    standalone_led.h / .c         LED feedback vocabulary
    standalone_modes/
        mode_template.c           Copy this to start a new mode
        mode_authtrace.c          Reference: reader-side, result buffer, HW gate
        mode_slot_cycle.c         Reference: tick-driven, no results, Lite-safe

software/script/
    chameleon_enum.py             StandaloneMode, StandaloneState, StandaloneFlag
    chameleon_cmd.py              ChameleonCMD methods for standalone commands
    chameleon_cli_unit.py         CLI subgroup and per-command classes
```