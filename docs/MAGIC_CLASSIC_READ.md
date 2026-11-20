# Read-only Gen1A-compatible Classic 1K backups

These CLI tools use the official Ultra firmware's existing `HF14A_SCAN` and
`HF14A_RAW` commands. No firmware update or private command allocation is
required. They add a backdoor-read workflow alongside the existing
key-authenticated `hf mf dump` command.

## Supported read profile

The initial profile is a four-byte UID, SAK `08` and 64 ordinary 16-byte blocks:
1024 bytes starting at Block 0. The card must acknowledge `40` as a seven-bit
frame, then `43`, and return CRC-checked ordinary block reads after that wakeup.
An unsealed UFUID can support this sequence as well as Gen1A cards.

This is observed read compatibility, not chip, manufacturer or generation
identification. A successful scan does not establish that only one card is
present. SAK and memory contents are configurable on magic cards, so the
profile does not prove the total physical capacity of arbitrary hardware.
Keep a single stationary card in the field throughout each operation.

Seven-byte UIDs, other memory sizes, the alternate `20(7)/23` wakeup and
password-based backdoors are outside this initial profile. The commands do
not try configuration reads: some magic-card families reverse configuration
read/write opcodes. They send no memory-write, UID-change, wipe, seal or
configuration command.

## Commands

```text
hw connect
hf mf magic info
hf mf magic dump -o card.bin
hf mf magic dump -o card.json --format json
```

`info` verifies two independent Block 0 reads against the scanned UID and
Block 0 BCC. It reports the observed read backdoor without asserting a
silicon identity.

`dump` reads all 64 blocks twice, in separately powered and unlocked sessions.
Each session checks the scanned identity and rechecks Block 0 at the end.
The two complete 1024-byte images must agree before a result is accepted.
This detects inconsistent reads or different observed identities; it cannot
distinguish interchangeable cards with the same UID and contents.

The CLI checks required capabilities and the output path before entering
Reader mode. It restores the original device mode before publishing a
backup, including when reading fails. A device originally in Reader mode
stays in Reader mode. Slots and HF14A selection configuration are not changed.

## Backup formats and storage failures

BIN is the default and contains exactly 1024 bytes in ascending block order.
JSON contains the same 64 hexadecimal blocks plus the UID, ATQA in wire
order, SAK, read profile, independent-read count and image SHA-256. The digest
always covers the binary 1024-byte image, not the JSON document.

Sector trailers can contain keys. Complete card data is written only to the
requested backup file; terminal output reports identity, destination and
digest rather than printing every block. Actual card images are not test
fixtures and must not be committed to the repository.

These files preserve exactly the bytes returned through the backdoor. Normal
authenticated Classic reads conceal Key A, so a retained authenticated-read
dump can contain zero placeholders where a backdoor image has usable keys.
Do not infer that card contents changed solely from that difference.

The tool itself does not independently authenticate returned sector keys.
JSON records `key_material_verified: false` and the raw key handling policy,
and both export formats print this limitation. No key reconstruction or
guessed replacement is performed. Verify keys separately before treating a
read image as a verified lossless key backup for a future restore.

Existing destinations are refused before RF activity. After successful
reading and mode restoration, output is staged in a temporary file in the
destination directory. It must be completely written, flushed, synced and
closed before publication. Windows rename and POSIX hard-link creation both
refuse a destination that appeared concurrently. Write, flush or close
failure does not publish a partial backup.

## Protocol and cleanup

Each independent pass starts by turning the RF field off, scans the card, then
selects and sends HALT (`50 00` with CRC) without waiting for a response. The
field stays on for `40(7)`, `43`, and ordinary `30 block` reads with CRC.
The read path checks both firmware status and exact response lengths, and
requires the wakeup response byte to be exactly `0A`.

`ChameleonCMD.hf14a_raw_response()` retains the firmware status for this
workflow. Existing `hf14a_raw()` callers keep their bytes-only interface.
The current raw wire protocol rounds a four-bit ACK to a byte and does not
expose its original bit count, another reason to describe compatibility
rather than strict chip identification.

Every pass releases the RF field on success, timeout, malformed data or
interrupt. A cleanup failure makes the operation fail; if another error
already occurred, that original failure remains in the exception chain.
Failure to restore the device mode also prevents publication.

## Validation

From `software/script`, using the existing CLI requirements:

```sh
python -m unittest discover -s tests -p "test_magic_classic*.py" -v
python chameleon_cli_main.py --help
```

Fixtures cover successful and failed handshakes, status/CRC failures with
plausible data, UID/BCC mismatches, changed identities and memory, cleanup,
legacy raw framing, mode restoration, complete exports and storage faults.
CI runs the tests on Python 3.9 and 3.13.

Physical acceptance uses the existing official firmware and a known
unsealed UFUID in the supported profile. Compare two complete read images
with an independently retained raw image. If a trusted authenticated-read
backup differs in sector keys, verify the returned keys independently by
successful normal authentication and compare the remaining bytes; do not
silently equate placeholder fields with actual keys. Finally verify original
mode, slot, HF14A configuration and settings remain intact. Acceptance of one
UFUID profile is not acceptance of all magic-card families.

On November 3, 2026, a known unsealed UFUID passed `info`, repeated BIN
exports and JSON export on official v2.3.0 firmware. The images matched an
independently retained backdoor-read image byte for byte. Sixteen returned
Key A values were also accepted by separate normal-authentication reads;
this was an acceptance check, not automatic key verification by the new
commands. The real CLI entrypoint restored Emulator mode, and device mode,
active slot, HF14A configuration and settings digest matched their initial
state. No physical card writes or firmware update were needed.

## Primary references

- [Chameleon Ultra Gen1A reader unlock](https://github.com/RfidResearchGroup/ChameleonUltra/blob/4c5bc2cb6b023d23f7bbc383c9b2b5e39f95419e/firmware/application/src/rfid/reader/hf/rc522.c)
- [Proxmark3 magic-card protocol notes at a fixed revision](https://github.com/RfidResearchGroup/proxmark3/blob/754bbae3d946429a6354cd34a97a2dd29ce6f157/doc/magic_cards_notes.md)
