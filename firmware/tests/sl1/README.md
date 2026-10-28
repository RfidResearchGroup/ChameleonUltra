# SL1 host tests

Run from the repository root with Python 3.10 or later, a C11 compiler available
as `cc`, and PyCryptodome (`Crypto.Cipher.AES`) installed:

```sh
python3 -m unittest discover -s firmware/tests/sl1 -p 'test_*.py'
```

The tests compile the current C implementation, or selected production functions,
into temporary host libraries. They exercise the portable AES/framing code,
native Classic authentication/read, NFCT lifecycle, command guards, profile
transactions and the synchronous FDS wrapper. The independent reader reference
uses PyCryptodome; no Python AES implementation is shipped in the firmware.

Fixtures are synthetic or published cryptographic vectors. Tests do not open a
device, enumerate ports, flash firmware, or require another source checkout.
They do not bootstrap an SDK or download a toolchain.

SDK interrupts, MMIO, flash, radio, timing and entropy sources are controlled
host boundaries. Fault cases include interrupted persistence, lookup/readback
errors, duplicate records, dirty RAM/configuration, slot changes and reboot
reconstruction. A passing host suite does not establish physical RF/USB/BLE
operation, watchdog timing or flash recovery through actual power loss. Build
Ultra/Lite firmware separately and perform hardware acceptance before use.
