# MIFARE Classic UID write protection

Issue [#305](https://github.com/RfidResearchGroup/ChameleonUltra/issues/305)
requests FUID/Gen3 emulation and tags with a locked manufacturer block. These
names cover several different protocols; this change addresses the locked
manufacturer block during authenticated Classic operations only.

## Existing settings

After loading the source dump into the intended Classic emulator slot, these
settings disable the Gen1a backdoor and reject authenticated writes to block 0
while allowing writes to other blocks according to their access conditions:

```text
hf mf econfig --disable-gen1a --disable-gen2 --write NORMAL
```

This command changes the active slot. Use `hf mf econfig` to inspect it and
`hw slot store` to persist slot data/settings across restarts. Keep UID, SAK,
ATQA and sector data consistent with the source tag. `--enable-block0` controls
where the four-byte anticollision data comes from; it does not enable block 0
writes. Gen2 mode controls authenticated manufacturer-block writes. Enabling
Gen1a permits backdoor writes independently of Gen2 and the global write mode.

Block 0 is 16 bytes; sector 0 contains blocks 0 through 3. Protecting block 0
does not lock the rest of sector 0 or its trailer. `FF 07 80 69` represents the
three access-condition bytes followed by the general-purpose byte, not a
card-version identifier. The dump supplies these bytes. Global `DENIED` mode
also prevents application data/counters from being updated; it is not the
setting for protecting only the UID.

## Firmware change

Authenticated `WRITE` already rejects block 0 when Gen2 mode is disabled.
`TRANSFER` previously checked sector/access permissions and global write mode,
but could copy the value-operation buffer over block 0. Apply the same Gen2
check to `TRANSFER`, before the global `DECEIVE` handling. A protected
manufacturer block must NAK and remain unchanged even with transport access
conditions. Other blocks, Gen2 writes and the Gen1a backdoor retain their
existing behavior. No command IDs or persistent configuration layouts change.

## Scope and evidence still needed

The [Proxmark3 magic-card notes](https://github.com/RfidResearchGroup/proxmark3/blob/master/doc/magic_cards_notes.md)
document Gen3/APDU programming separately from FUID personalization. Their
protocols are not implemented by this change. It does not emulate a blank
write-once tag, an irreversible OTP transition, or an MF3.2 chip fingerprint.

The discussion in [#324](https://github.com/RfidResearchGroup/ChameleonUltra/issues/324)
reports several distinct failures. The #305 reporter later reported success
with firmware 2.2; others still report authentication/timing failures with
MF3.2 replacements. Rejecting writes alone cannot establish compatibility.
Before adding a silicon-specific profile, obtain a reproducible comparison
between a working tag and CU with identical slot data/settings: reader model
and firmware, tag identification, command/response sequence including rejected
commands, authentication nonce behavior for both keys, and measured frame delay
times. Keep private dumps and keys out of the contribution.

Hardware validation should use a disposable slot and a reader/test fixture:
verify rejected WRITE and RESTORE/TRANSFER to block 0 with Gen2 off, unchanged
block 0 afterward, and successful permitted updates to other blocks (including
sector 0 data blocks). Repeat with Gen2 on, global write modes, and after saving
and reloading the slot. Reader compatibility and RF timing need actual hardware;
host regression tests and firmware builds cannot establish them.
