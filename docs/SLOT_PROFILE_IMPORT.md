# Verified Classic 1K HF slot profile import

This CLI workflow uses existing official Ultra emulator commands. It imports
a complete image together with saved UID/ATQA/SAK/ATS, HF type, enable state
and exposed Classic emulator settings, then reloads and compares the result.
It does not change physical cards or add a firmware protocol.

## Commands and supported targets

```text
hw connect
hw slot profile import -s 8 -f private-backups/card.json
hw slot profile import -s 8 -f private-backups/card.json --apply --backup private-backups/before-slot8.json
hw slot profile backup -s 8 -o private-backups/slot8.json
hw slot profile restore -s 8 -f private-backups/before-slot8.json
hw slot profile restore -s 8 -f private-backups/before-slot8.json --apply --backup private-backups/before-restore.json
```

Import and restore default to preview. `--apply` requires a new private backup
path with an existing directory; existing files and symlinks are refused.
The current interactive CLI splits on whitespace, so use paths without spaces.

The first scope is an **inactive HF-only slot**, either empty or Classic 1K.
The target must have no LF type or enabled LF side. The current HF slot must
also be empty or Classic 1K so its volatile write policy can be inspected.
Mini/2K/4K, NTAG, dual-frequency targets and current-slot replacement are
outside this version.

Import leaves the original slot selected. Activate the imported slot later
through the normal slot-selection controls. CardAuth/AES extensions and
custom-firmware fields are not part of this standard Classic snapshot.

Preview/backup require temporary selection of the target, because existing
firmware exposes memory through the active slot. The workflow first enters
Reader mode, then restores the original selection and mode. Slot switching
can save the previous RAM through existing firmware behavior; preview sends
no target data, factory, metadata or configuration setters.

Use one USB controller and do not operate buttons or another reader client
during the workflow. Existing per-block emulator commands do not carry an
explicit slot ID and provide no exclusive transaction lease. The host checks
Reader mode and selection around setters but cannot make concurrent external
commands atomic.

## Source profiles

The JSON card-image format is `chameleon-gen1a-classic1k-v1`, as emitted by
the read-only tools in PR #539. Supporting that file format does not require
its code or firmware; this implementation is based directly on official main.

The parser requires 64 complete blocks, matching image SHA-256, four-byte
UID/BCC, SAK `08`, and well-formed identity/read metadata. Duplicate JSON
members and oversized files are rejected. Metadata overlays are not accepted
for JSON.

For a raw 1024-byte BIN, specify the missing metadata and a retained digest:

```text
hw slot profile import -s 8 -f private-backups/card.bin --atqa 0400 --sak 08 --sha256 EXPECTED_64_HEX_DIGEST
```

ATQA is in wire byte order. UID comes from validated Block 0. Card images
contain no emulator policy, so they import with these explicit defaults:

- Classic 1K HF type, enabled, empty ATS.
- Independent saved anti-collision metadata; Block 0 override disabled.
- Gen1A/Gen2 modes and detection disabled.
- Write-denied mode, field-off reset disabled, weak/LFSR PRNG type `1`.

These are emulator policy choices, not inferred properties of the physical
card. Key bytes are copied as provided, never guessed or authenticated by this
workflow. Byte/metadata verification does not prove authentication or
application compatibility with an external reader.

## Private target snapshots and recovery

Before the first destructive setter (including factory initialization), the
entire observed target state is written to a private, no-clobber snapshot:

- A device fingerprint and explicit slot number.
- Empty/Classic 1K type and HF enable state.
- All 1024 memory bytes for a populated slot.
- Saved UID, ATQA, SAK, ATS, detection, Gen1A, Gen2, Block 0 policy, write mode,
  field-off reset and PRNG settings.

The `chameleon-classic-slot-v1` checksum covers the complete canonical
document, including metadata and configuration, not just memory. Publication
requires complete write/flush/fsync/close, followed by reopening, parsing and
an exact state comparison. Slot restore requires both the same device and
the explicitly requested matching slot.

Snapshots contain keys and other private data. They are not repository test
fixtures and the CLI does not print their contents. They describe observable
HF profile state, not raw FDS records, secure erasure or transient RF sessions.
Nicknames and LF content are not changed by this workflow.

Shadow/pending-Shadow modes are rejected: firmware deliberately skips saving
their volatile RAM. Nonempty authentication logs are also refused instead of
silently clearing them. Targets with reserved PRNG values cannot be captured
as restorable profiles. In particular, existing firmware may expose PRNG `3`
but refuses to set it; the original, non-target slot's opaque value is
preserved without normalization.

## Apply, persistence and failure behavior

The preview captures the target twice while restoring host context. Apply
checks inventory and target state again, verifies the before-slot snapshot
on disk, then initializes/writes only the chosen HF target. Every setter
checks status and response shape, including the PRNG setter's echoed byte.

After writing, the workflow switches away and back to reload target memory
and emulator configuration, then compares both complete reads. It verifies
unrelated slot types/enables and the target's unused LF side too. Disabling
an HF-only target can automatically select another slot in firmware; the
workflow explicitly reselects before continuing.

Only after verification does it restore the original active slot and issue
the global save command, so the target is not persisted as the active slot.
It then restores the original device mode. A no-difference apply creates no
backup and sends no profile setters or global save.

Device updates are not atomic. A failure reports stage, possible target
changes, verification outcome and any verified before-slot backup path.
There is no automatic retry or target rollback. Use a fresh, explicitly
requested restore from the checksum-verified snapshot. If original-slot
restoration fails, the workflow keeps Reader mode rather than exposing a
partial target in Tag mode. Secondary cleanup errors retain the primary
failure and cancellation remains cancellation.

Reload verifies persisted HF image/configuration. The global save response
and RAM readback do not independently prove power-cycle persistence of the
global slot table; no raw flash or crash-consistency guarantee is made.

## Validation

```sh
cd software/script
python -m unittest discover -s tests -p "test_slot_profile*.py" -v
```

Stateful fixtures model save-on-switch, factory flash writes, active-buffer
ownership, disable auto-selection and failed persistence. Tests cover file
corruption/storage faults, stale plans, protected targets, unsupported modes,
cleanup, cancellation, legacy PRNG preservation and complete recovery.

On November 3, 2026, official v2.3.0 on Ultra passed JSON and BIN imports into
previously empty slot 8, saved-image reload, metadata/configuration comparison,
restore to an earlier populated state, restore to disabled state (including
auto-selection), and restoration of the initial empty slot. Independent raw
observations confirmed that memory and exposed metadata/settings of original
slots 1–4, including reserved PRNG values, remained identical. Original
selection/mode, settings digest and HF reader configuration were preserved.
No physical-card RF command or firmware update was used.
The real noninteractive CLI also completed a profile preview and restored
the original Emulator mode.

External-reader RF behavior and full power-cycle persistence were not tested.

## References

- [Official slot save/load and type behavior](https://github.com/RfidResearchGroup/ChameleonUltra/blob/4c5bc2cb6b023d23f7bbc383c9b2b5e39f95419e/firmware/application/src/rfid/nfctag/tag_emulation.c)
- [Official emulator command responses](https://github.com/RfidResearchGroup/ChameleonUltra/blob/4c5bc2cb6b023d23f7bbc383c9b2b5e39f95419e/firmware/application/src/app_cmd.c)
