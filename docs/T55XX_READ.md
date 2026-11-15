# Experimental T5577 read-only blocks and Page 0 backups

This change adds an ordinary Page 0 direct-access READ capture on Chameleon
Ultra and a conservative host decoder for basic ASK Manchester RF/32 and RF/64.
Chameleon Lite does not advertise the capture command.

**RF hardware acceptance is pending.** The C timing/cleanup fixtures and synthetic
Manchester vectors do not prove reception of a physical T5577. The restricted
first-response acquisition window and its stability across block addresses
must be checked using a known, unprotected T5577 before this profile is
described as hardware-tested.

## Scope

The reader sends only `1,0,0,A2,A1,A0`: Page 0, ordinary access, blocks 0–7.
It never transmits a data word, password, lock request, configuration change,
or password reset. Existing writers and their protocol layout are unchanged.

Supported decoder profiles are basic Manchester RF/32 and RF/64. Extended mode,
PSK/FSK/direct/biphase, password access, answer-on-request, OTP, sequence
terminators, fast write, power-on delay and basic-mode reserved bits are
rejected, including bit 1 (the extended-mode inverse-data option). This does
not prevent calibration of analog receiver polarity from the waveform. A protected or
unsupported tag may produce samples without producing a valid block value.
This is a compatible configuration/profile check, not silicon identification.

Use USB with BLE disconnected. The firmware rejects an active BLE connection.
It pauses advertising only for this acquisition and resumes it only if it was
previously active. Advertising errors abort before RF is enabled.

## CLI

On firmware advertising the new capture capability:

```text
hw connect
lf t55xx read -b 0
lf t55xx read -b 3
lf t55xx dump -o page0.json
lf t55xx dump -o page0.bin --format bin
```

The reader commands use the existing explicit reader-mode switch in the CLI.
Firmware without this capability is rejected before changing mode or sending
the new command. Use `hw mode -e` to return to tag mode afterwards.

The first block-0 read identifies a supported configuration and calibrates the
response's sample position and polarity. All three independent captures must
agree. Each data word must repeat three times within its capture, and three
independent addressed READs must return the same word.

A dump reads all eight words, including words above the configuration's regular
transmission `MaxBlock`, and checks block 0 again at the end. A configuration
change or disagreement fails the operation. It does not create an output file
until the complete read and final configuration check succeed; existing
destination files are never overwritten. The complete output is written,
flushed and closed in a temporary file in the destination directory before
publication. Write, flush or close failures remove that temporary file and
leave the destination absent. A concurrent creator of the destination also
causes publication to fail without replacing its file.

Page 0 has no immutable unique identifier. Rechecking the configuration cannot
detect replacement with another tag having the same configuration. Keep one
stationary tag in the field throughout a backup.

The default JSON format records the eight hexadecimal words, configuration,
calibration parameters, independent-read counts, and the SHA-256 of the
32-byte image. `--format bin` writes exactly eight big-endian 32-bit words,
starting with block 0. No restore/write command is part of this change.

## Acquisition and decoding

The ADC is prepared with its PWM/PPI trigger disabled. Sampling begins directly
after the final command gap, without the existing writer's six-millisecond
post-command delay. A private buffer receives complete DMA batches without
using or changing the existing `lf sniff` circular queue.

The command and acquisition share a 90 ms radio timeslot. The bounded 70 ms
sampling wait uses microsecond delays rather than the BSP timeout: the existing
timeslot helper masks RTC1. SAADC remains interrupt-driven. The calculated
complete capture is about 72 ms including power-up and command transmission.
The existing helper's wait to obtain a radio timeslot is unchanged and has no
timeout; the 70 ms bound applies only after sampling starts, not to the whole
command.

The firmware returns 8176 carrier-period samples, quantized to four bits each.
The nominal sample rate is 125 kHz (one sample per carrier period). Capturing
success means ADC data was acquired; the host still has to validate the signal.

There is no data CRC or independent word delimiter in this mode. An arbitrary
repeated 32-bit value admits cyclic rotations, so the host does not search the
whole recording for the highest-scoring word. It searches for a unique
self-consistent configuration in a restricted first-response window, then
reuses that calibrated sample position and polarity for data reads.

The default first-response window is samples 48–160 after command completion,
with a ±4-sample allowance for subsequent data reads. This is an acquisition
hypothesis awaiting physical captures. A whole-bit systematic timing shift
cannot be detected from arbitrary data alone. Independent agreement and the
final configuration check are useful consistency checks, not an independent
reference reader or proof that the timing hypothesis is correct.

## Wire command

`3041 / LF_T55XX_READ_CAPTURE` is a **provisional** allocation requiring
maintainer confirmation. Existing `3032` and open LF PR allocations are not
reused.

Request: one byte, Page 0 block number `0..7`. Invalid length/address is rejected
before RF/ADC activity. Success returns `STATUS_SUCCESS`; capture/setup failure
returns the existing `STATUS_CMD_ERR`. Neither status claims a decoded tag.

The response is a 4096-byte payload, with network byte order:

| Offset | Size | Meaning |
| --- | --- | --- |
| 0 | 1 | Capture version `1` |
| 1 | 1 | Bits per sample `4` |
| 2 | 2 | Sample count `8176` |
| 4 | 2 | Signed 16-bit minimum |
| 6 | 2 | Signed 16-bit maximum |
| 8 | 4088 | High-nibble-first quantized samples |

The minimum and maximum retain the original ADC range. Quantization does not
turn a flat/noisy trace into evidence of a card; amplitude, Manchester
half-cells, repeated words and independent captures are checked separately.

## Validation

Production acquisition C tests:

```sh
python3 firmware/application/tests/run_t55xx_read_tests.py
```

Decoder and CLI tests, using only the existing CLI requirements:

```sh
cd software/script
python3 -m unittest discover -s tests -p "test_t55xx*.py" -v
```

The tests cover the six transmitted bits and gap timing, ADC trigger order,
DMA batch limits, quantization, BLE ownership/errors, cleanup, malformed wire
data, inversion, clock/offset/noise/jitter, ambiguity rejection, repeated-read
disagreement, and complete/non-overwriting exports. CI includes Python 3.9
and 3.13. Ultra and Lite must both be built, and Lite must have no read-capture
command or linked acquisition code.

Physical acceptance still requires a known unprotected T5577 at each supported
rate, comparison with independently known data, and repeated reads of all
eight addresses. Flat/no-tag and differently configured tags must fail cleanly.
The existing HF UFUID card is not a substitute for those LF measurements.

## References

- [Microchip ATA5577C data sheet](https://ww1.microchip.com/downloads/en/DeviceDoc/Atmel-9187-RFID-ATA5577C_Datasheet.pdf)
- [Proxmark3 LF implementation at a fixed revision](https://github.com/RfidResearchGroup/proxmark3/blob/754bbae3d946429a6354cd34a97a2dd29ce6f157/armsrc/lfops.c)
- [Proxmark3 T55xx host decoding and anchor handling](https://github.com/RfidResearchGroup/proxmark3/blob/754bbae3d946429a6354cd34a97a2dd29ce6f157/client/src/cmdlft55xx.c)
