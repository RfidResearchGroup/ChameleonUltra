# USB CCID UID reader (Ultra)

Chameleon Ultra exposes a USB CCID interface alongside the existing CDC serial
interface. A PC/SC application can retrieve an ISO14443-A card's anticollision
UID using `FF CA 00 00 00`. Chameleon Lite keeps its existing CDC-only layout.

This is a UID reader. It does not implement PC/SC storage reads, key loading,
authentication, writes, or ISO-DEP APDU pass-through. The existing Chameleon
serial commands remain available for those operations.

## Using it

1. Connect an Ultra by USB.
2. Use the CLI's `hw mode -r` command to enter reader mode explicitly.
3. Open the Chameleon reader through the operating system's PC/SC API.
4. Send `FF CA 00 00 00` using protocol T=1.
5. Disconnect the PC/SC handle and use `hw mode -e` to return to tag mode.

On Windows, the inbox USB CCID driver handles interface 2; no custom driver is
needed. The reader name includes the device's existing USB product string. On
systems using pcsc-lite, a CCID driver and the PC/SC service must be available.
Driver recognition on each operating system should be checked separately.

The response contains a 4-, 7-, or 10-byte UID in anticollision order, followed
by `90 00`. For example, UID `12 85 44 8F` produces `12 85 44 8F 90 00`.
The card's UID and card memory are not modified.

The nonzero `Le` cases follow PC/SC Part 3: a shorter `Le` returns
`6C <UID length>`, while a larger `Le` pads the returned data with zeroes and
ends with `62 82`. Unsupported class/instruction bytes return `6E 00` / `6D 00`;
unsupported P1/P2 return `6A 86`; an unsupported APDU length returns `67 00`.

## Reader state and coexistence

The firmware starts in tag mode as before. CCID reports no card until the user
explicitly enters reader mode; opening a PC/SC application does not change the
device mode or its active emulation slot.

CCID polls at approximately 250 ms intervals while USB is configured and awake.
It reselects the card before returning a UID, comparing UID length, UID, SAK and
ATQA. Removal or replacement invalidates the previous PC/SC activation, so a
cached UID cannot be returned for a different card.

Presence checks deliberately cycle the RF field. This is appropriate for this
UID-only interface, but it does not preserve a card's application or
authentication state. It is not an ISO-DEP keep-alive session.

A CDC/BLE reader command or an offline-copy/field-generator button operation
revokes CCID's RF ownership. Background CCID polling does not touch a field
left on by another reader operation. USB reset/suspend and PC/SC power-off
release only a field owned by CCID. Leaving reader mode clears the CCID card
state before the RC522 is uninitialized.

The returned ATR is a PC/SC storage-card pseudo ATR. It identifies ISO14443-A
and uses an unknown card name for card families that cannot be distinguished
from the available selection data. This ATR describes the host interface;
it does not claim a card has contact-card T=1 or supports arbitrary APDUs.

## USB layout

| Function | Interfaces | Endpoints |
| --- | --- | --- |
| Existing CDC | 0, 1 | Bulk OUT 1, bulk IN 1, interrupt IN 2 |
| CCID | 2 | Bulk OUT 3, bulk IN 3, interrupt IN 4 |

The CCID descriptor advertises one slot, T=1, automatic parameter configuration
and negotiation, and short APDU exchange. The maximum CCID message is 271 bytes
(261-byte APDU plus a 10-byte header). Fixed clock/rate fields describe the
pseudo transport rather than adjustable RF parameters.

Bulk OUT is received in 64-byte packets and assembled using `dwLength`. A
64-byte-multiple message therefore completes without needing an extra short
packet or ZLP. Only one command is accepted at a time, and reply buffers remain
owned by USB until IN completes.

Control and bulk Abort requests are paired by slot/sequence in either order.
An incomplete Abort handshake blocks subsequent commands for the slot.
Abort does not power off a card or terminate a CDC operation.

## Tests

The host tests compile the production reader bridge and CCID protocol parser
against a deterministic RF mock. They require Python 3 and a C11 compiler,
with no Python packages:

```sh
python3 firmware/application/tests/run_ccid_reader_tests.py
```

To select a compiler, pass `--cc /path/to/gcc`. The suites check card selection,
RF ownership, ATR/TCK, APDU status words, message boundaries, parameter errors,
Abort ordering, and stale-card rejection. These tests do not replace a USB
enumeration and PC/SC test on a real Ultra.

Build Ultra and Lite separately using the existing firmware build workflow.
Only the Ultra build includes CCID source files and descriptors.

## References

- [USB CCID Revision 1.1](https://www.usb.org/sites/default/files/DWG_Smart-Card_CCID_Rev110.pdf)
- [PC/SC Part 3 v2.01.09](https://pcscworkgroup.com/Download/Specifications/pcsc3_v2.01.09.pdf)
