# Presco LF support

Presco uses Manchester modulation at RF/32 (125 kHz carrier), with a 128-bit
frame and a four-bit-time sequence terminator. This implementation supports
slot emulation on Ultra and Lite, T5577 writing on Ultra, and offline code
encoding. Presco reading is not implemented.

## CLI

Encode without connecting a device:

```text
lf presco encode --printed 018363467
lf presco encode --id 039CA0CF
```

Both produce `10D000000000000000000000039CA0CF`. Printed codes are base 12:
`*` represents 10 and `#` represents 11. Quote codes containing these characters
when passing commands through a shell. Codes must fit in 32 bits; overflow is
rejected rather than truncated.

Configure an emulation slot (after connecting):

```text
hw slot type -s 1 -t Presco
lf presco econfig -s 1 --id 039CA0CF
lf presco econfig -s 1
hw slot store
```

Use the normal slot enable and device mode controls to activate emulation.
`econfig` requires an existing Presco slot and does not change another slot type.
Changes take effect in RAM; `hw slot store` persists the slot data.

Write a blank T5577 (Ultra only):

```text
lf presco write --id 039CA0CF
```

This replaces the tag's data and configuration using the existing CLI password
workflow: it attempts the standard old passwords and installs the standard new
password (`chameleon_cmd.new_key`). As with the other protocol-specific LF
writers, T5577 writes have no acknowledgement. A successful command means the
write sequence was sent; verify the result with an independent Presco reader.

## Frame and wire protocol

The stored code is four bytes in big-endian order. Preserve all four bytes:
the most significant byte is the site code, the least significant two bytes
are the user code, and the remaining byte has an unknown meaning. The frame
is four big-endian words: `10D00000 00000000 00000000 <code>`.

The terminator is not part of the 128 data bits: high for 16 carrier cycles,
low for 16, high for 48, low for 16, high for 32. T5577 block 0 selects
Manchester, RF/32, the sequence terminator, password protection and max block
4 (`00088098`). Blocks 1-4 hold the frame. The emulation PWM sequence uses the
same data and explicitly appends that terminator.

| Command | Request | Response on success |
| --- | --- | --- |
| 3040 `PRESCO_WRITE_TO_T55XX` | code[4], new password[4], one or more old passwords[4 each] | Empty, `LF_TAG_OK` |
| 5030 `PRESCO_SET_EMU_ID` | code[4] | Empty, `SUCCESS` |
| 5031 `PRESCO_GET_EMU_ID` | Empty | code[4], `SUCCESS` |

Tag type: 160 (`Presco`). Emulation commands require an active Presco slot.
The write command is advertised only by Ultra firmware. Existing command IDs
and payloads are unchanged. The chosen new IDs are subject to upstream review.

## Validation

Run `bash firmware/tests/run-presco.sh` for tests of the actual C encoder and
PWM generator with AddressSanitizer and UndefinedBehaviorSanitizer. The test
expands the PWM sequence to pin levels, independently decodes each Manchester
bit, checks all terminator ticks and the loop boundary, and verifies the T5577
configuration for zero, all-one, alternating, walking-bit and example codes.
It does not access a device.

From `software/`, run:

```sh
PYTHONPATH=script uv run --frozen python -m unittest discover -s script/tests -p test_presco.py -v
```

These tests cover code parsing, overflow, frame encoding, CLI arguments, wrong
slot rejection, command payloads and malformed responses without hardware.

Hardware validation is still required: check emulation with a Presco reader,
read back an independently programmed T5577, and confirm saved slot data
survives a reboot. Builds and host tests do not establish real-reader acceptance.

References: [Proxmark Presco framing and printed-code conversion](https://github.com/RfidResearchGroup/proxmark3/blob/master/client/src/cmdlfpresco.c),
[Proxmark Manchester sequence terminator](https://github.com/RfidResearchGroup/proxmark3/blob/master/armsrc/lfops.c).
