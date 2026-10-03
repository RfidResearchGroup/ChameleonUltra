# DESFire emulation engine — provenance and licensing

## Licensing

All sources under this directory are licensed **GPL-2.0-or-later**, matching the
ChameleonUltra application they are compiled into. Every file carries an SPDX
identifier.

The DESFire engine under `engine/` originates in the author's CinderSocket
`desfire_compatible` project and was relicensed by its author (CinderSocket) as
GPL-2.0-or-later for inclusion here. It is a first-class part of this
application, not a vendored third-party subtree.

The engine was originally written against a third-party embedded NFC
application SDK. That coupling has been fully removed — see **Porting notes**.
No code from any such SDK remains in this tree.

## Third-party components

| Path | Component | License |
|---|---|---|
| `crypto/tiny-DES-c/` | tiny-DES-c | Unlicense (public domain) |
| `crypto/tiny-AES-c/` | [kokke/tiny-AES-c](https://github.com/kokke/tiny-AES-c) | Unlicense (public domain) |
| `tests/munit/munit.{c,h}` | [µnit](https://nemequ.github.io/munit/) | MIT — Copyright (c) 2013-2017 Evan Nemerson |

Both crypto libraries are public domain and impose no conditions on this
project. µnit's MIT notice is preserved verbatim in its own headers and is
host-test-only — it is never linked into firmware.

Both crypto libraries are included as Git submodules.

## Porting notes

- The engine's platform dependencies are confined to `shim/dfc_port.h`
  (assertions, logging, randomness, object allocation) and
  `shim/dfc_bytebuf.h` (an append-only byte buffer). The firmware implements
  these in `desfire_shim.c`; host tests implement them in `tests/support/`.
- `shim/mbedtls/{aes,des}.h` + `shim/mbedtls_compat.c` provide the small subset
  of the mbedTLS API the engine calls, implemented over tiny-DES-c and
  tiny-AES-c. There is no mbedTLS or OpenSSL dependency.
- Allocation is tagged (`dfc_platform_alloc`) rather than a generic heap call,
  so the firmware can serve each of the engine's four long-lived objects from
  static storage. See the header for why a heap is unsuitable here.
- Host tests compile the same engine sources and the same crypto path as the
  firmware, so a green `make -C tests test` exercises production code.
- `engine/dfc_der.{c,h}` is the credential codec, and `tests/test_dfc_der.c`
  checks it against `tests/fixtures/worked-example.dfcb`, a credential emitted by
  an independent Python codec. Decode-then-encode reproduces that file octet for
  octet, which is what makes the two implementations comparable rather than
  merely self-consistent. Regenerating the fixture is a format change, not a
  test fix.

## Scope

DESFire EV1 and earlier (D40 / ISO / AES mutual authentication). Commands
outside that scope return `ILLEGAL_COMMAND_CODE` rather than a false success.

Do not cite external specification document names or table numbers in commit
messages or user-facing strings.
