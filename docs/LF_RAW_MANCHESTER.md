# Raw Manchester LF slots (experimental)

This feature is under development. It is not fully hardware-validated and is not a
generic T5577 chip emulator. A memory backup and an on-air frame are different
objects: only configured transmitted blocks belong in the frame.

## Record and wire contract

Provisional tag type: `190 / RAW_MANCHESTER`.
Provisional commands: `5020 / SET`, `5021 / GET`, operating on the active slot.
Allocations require maintainer approval before upstream integration.

Diagnostic commands: `5022 / START`, `5023 / STOP`, `5024 / STATUS`.
All require USB request origin, an open USB port, and no BLE connection.
START takes exactly two bytes: big-endian duration in milliseconds, 1..5000.
It requires tag mode and a valid Raw Manchester record already loaded in the
active LF slot. Lower-level API additionally checks LF sense/playback state;
failure returns a command error, never success. STOP and STATUS take no bytes.
STATUS returns one byte: 0 idle, 1 running, 2 stopped. STOP is idempotent.

```
lf raw diagnostic --ms 1000
lf raw diagnostic --status
lf raw diagnostic --stop
```

Choose exactly one action. Host checks firmware capability before sending any
diagnostic command; it never switches slots or saves settings. Playback is finite,
rounded down to whole frames (fails if none fit), and explicitly experimental.
Diagnostic playback starts only through explicit START; normal field-triggered
emulation is a separate LF lifecycle path. Neither path establishes transparent
reader support. Hardware validation remains outstanding.

Both commands use a fixed 32-byte record (SET request / GET response):

| Byte | Field |
| --- | --- |
| 0 | Format version, must be 1 |
| 1 | Carrier cycles per bit: 32 or 64 |
| 2 | Flags: bit 0 basic T5577 terminator; bit 1 inverted polarity |
| 3 | Number of payload bits, 1 through 224 |
| 4–31 | Payload, most-significant bit first |

Unknown flags, nonzero unused payload bytes or padding bits, unsupported clocks,
and inversion combined with a terminator are rejected. The latter combination
has no independently validated damping/polarity contract. Commands must not
reinterpret another slot type's record. Invalid SET data must not mutate it.

No automatic preamble, checksum, parity or identifier conversion is applied.
Basic T5577 ST adds four bit periods before each repeated payload: Manchester
data 1, full-period damping, Manchester data 1, full-period damping. Reference:
ATA5577C datasheet 9187H, section 5.11.3. This is not the extended-mode sequence
start marker and must not be used as such.

## Import safety

T55x7 imports accept only supported basic Manchester profiles and extract
blocks 1 through MAXBLK. Configuration, factory trace words and untransmitted
blocks are not emitted as payload. Original dump files remain unchanged.
Unsupported profiles must fail before slot mutation. Existing LF contents need
explicit replacement approval; paired HF contents must be preserved.

## Acceptance requirements

### Initial Ultra capture result

### Bounded diagnostic hardware checks

On one Ultra, a 120,000-sample independent Proxmark capture during the finite
diagnostic contained 54 complete frames. All 6,912 payload half-cell centers
and 432 terminator half-cell centers matched the reference capture. Frame
intervals were 2,175 or 2,176 samples, with no additional inter-burst pauses.
This compares digital polarity at sampled centers, not analog waveforms or
calibrated oscillator accuracy. Private identifiers and captures are excluded.

Explicit stop and automatic expiry were exercised. Three consecutive
reader-to-emulator transitions decoded correctly afterward. A further
one-second diagnostic expired with `stopped` status, and the next independent
field activation decoded the ordinary raw frame without a mode reset.
Slot contents survived application DFU/reboot, and existing HF/LF credential
contents were compared with a pre-update backup after the initial update.

Not verified: intended access-reader acceptance, battery-only sleep current,
physical USB removal during playback, BLE interrupt stress, RF/64 on hardware,
or Lite hardware. Normal autonomous playback still retains the shared
ten-frame field-check pauses; bounded diagnostic success does not remove that
limitation or establish autonomous continuous emulation.

An independent Proxmark capture of a 64-bit RF/32 frame with basic terminator
matched all payload half-cells across 18 complete frames. However, the existing
shared LF playback stops every ten frames to check field loss. Two measured
boundaries introduced approximately 256 extra carrier-period samples and
disturbed the terminator. This is NOT continuous waveform equivalence and does
not establish reader compatibility. Private payloads/captures are not fixtures.

Do not remove field-loss handling or blindly enable continuous PWM looping to
hide this limitation: the existing code documents self-drive interference with
field sensing. A raw-mode-specific solution needs separate hardware acceptance.

Host/C tests establish validation and encoded output, not RF correctness.
Before claiming hardware support, build Ultra and Lite, back up device state,
retain a recovery image, and capture the emulated signal with an independent
reader while the original tag is absent. Check bit order, bit duration,
polarity, terminator, frame boundaries and repetition. Verify stored data after
a power cycle. Acceptance by the intended authorized reader is a separate test.

Never commit real access credentials or private captures as test fixtures.
