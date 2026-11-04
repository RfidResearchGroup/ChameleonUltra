# Read-only EM4205/4305 word reads and readable-word backups

```text
hw connect
lf em 4x05 read-word -b 4
lf em 4x05 read-word -b 5 --pwd YOUR_8_HEX_DIGITS
lf em 4x05 backup -o private/readable.json
```

The password example is a placeholder, not a guessed/default credential. Only
an explicitly supplied exact 32-bit password triggers LOGIN; explicit zero is
different from no password. No password search is performed or stored.

## Firmware and protocol scope

This independent proposal introduces provisional Ultra command 3043, subject to
maintainer allocation. Request: `address:u8 | use_password:u8 | password:u32be`,
exactly six bytes. Address 2 is rejected. Unused password bytes must be zero.
The only transmitted commands are READ and optional LOGIN; there is no generic
opcode, programming, PROTECT or DISABLE endpoint.

The forward-link builder follows the manufacturer EM4205/4305 DS v6 command and
45-bit data matrices. The existing 3030 scan command and its historical decoding
are left separate. This host never falls back to 3030.

A USB acquisition requires no BLE connection. Advertising is paused/resumed with
ownership preserved. READ and capture use one bounded timing reservation, with
DMA prepared before PPI starts after the final READ symbol. LOGIN has a separate
bounded reservation, and the field stays on before READ. A supplied LOGIN is an
attempt, not a claim of successful password authentication on publicly readable
words.

Success returns a complete 6144-sample signed ADC window quantized to four bits:
`version:u8=1 | bits:u8=4 | count:u16be=6144 | low:i16be | high:i16be | packed[]`.
This is a 3080-byte payload. Completion is not card detection: the host must
validate actual modulation, preamble, four row parities, eight column parities
and the final stop bit. Flat captures, ambiguous words and unsupported encodings
fail.

The first decoder claims Manchester and bi-phase at RF/8,16,32,40,64. Configuration
word 4 must decode under its own declared encoding/rate. PSK/FSK/NRZ, other rates
and EM4x69-specific behavior are outside this first acceptance scope.

## Backup guarantees

Each word is read twice and must match. Backups require an EM4205/4305 chip code
from word 0 and reread chip/serial/config anchors after acquisition. Keep a single
card in the field; these checks are not cryptographic identity authentication.

All 15 readable words must succeed before a file is published. Password word 2
is always `null` and listed as unreadable, even when a password was supplied.
This is a complete *readable-word* backup, not an independently restorable
16-word EEPROM image. No restore/program command is included.

Files use `chameleon-em4205-4305-readable-v1`, a UTC timestamp, exact words,
explicit unreadable/login metadata and a checksum over the canonical document.
The supplied password is not serialized. Publication requires an exclusive
new file, write/flush/fsync/close and exact reread, after restoring the original
device mode. Cancellation and primary errors survive cleanup failures.

Create the private output directory first; existing files/symlinks are refused.
Use paths without spaces with the current interactive CLI parser.

## Verification boundary

Native forward-link golden vectors cover the READ/LOGIN codes, all readable
addresses and the LSB-first password matrix. Python tests exercise actual
synthetic Manchester/bi-phase waves, all supported rates, inverted amplitude,
configuration self-consistency, preamble/parity/stop corruption, ambiguity,
noise and flat captures. Separate stateful transport/CLI tests isolate the
decoder to verify repeated reads, failure/no-publication, mode restoration,
password omission and absence of programming commands.

These are host/native tests and firmware compilation, not measured card/RF
acceptance. Keep the PR in draft until physical EM4205/4305 cards validate
forward-link timing, response alignment and real noise behavior.

Reference: manufacturer [EM4205/4305 DS v6](https://www.emmicroelectronic.com/sites/default/files/products/datasheets/4205-4305-DS-01.pdf),
particularly memory organization, forward-link timing and command/data matrices.
