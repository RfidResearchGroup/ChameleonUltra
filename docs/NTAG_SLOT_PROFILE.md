# Complete MF0/NTAG emulator slot profiles

This workflow preserves the full emulator image, rather than only publicly
readable card pages. It supports all nine existing firmware MF0/NTAG types:
Ultralight, Ultralight C, Ultralight EV1 (both sizes), NTAG210/212/213/215/216.
The first host transaction implementation requires Ultra.

```text
hw connect
hw slot ntag backup -s 8 -o private/slot8.json
hw slot ntag import -s 8 -f private/source-slot.json
hw slot ntag import -s 8 -f private/source-slot.json --apply --backup private/before-slot8.json
hw slot ntag restore -s 8 -f private/before-slot8.json
hw slot ntag restore -s 8 -f private/before-slot8.json --apply --backup private/before-restore.json
```

Use an **inactive HF-only slot**, empty or an existing supported MF0/NTAG type.
The target must have no LF type or enabled LF side. Original selection/mode is
restored. The current HF slot must be empty, Classic or MF0/NTAG so its volatile
write policy can be inspected. Shadow/pending-Shadow state and nonempty
authentication logs are refused instead of discarded.

Import/restore default to preview. Apply requires a new backup path in an
existing private directory. Snapshot publication requires exclusive creation,
flush/fsync/close and exact parsed readback before any target factory/data/config
setter. Existing files and symlinks are refused. Use paths without spaces with
the current interactive CLI parser.

## What a snapshot contains

- Device fingerprint, explicit slot, type and HF enable state.
- Saved anti-collision UID/ATQA/SAK/ATS.
- Every physical page, including passwords and the Ultralight-C key pages.
- Emulator-private counter/auth-attempt/tearing bytes, version and signature
  pages where the type has them.
- Complete encoded emulator policy, including opaque reserved configuration
  bits/bytes, without depending on the native C struct/bitfield layout.

Counts are validated for the exact type:

| Type | Physical pages | Complete emulator pages |
|---|---:|---:|
| NTAG213 | 45 | 56 |
| NTAG215 | 135 | 146 |
| NTAG216 | 231 | 242 |
| Ultralight | 16 | 16 |
| Ultralight C | 48 | 48 |
| Ultralight EV1 48-byte | 20 | 33 |
| Ultralight EV1 128-byte | 41 | 54 |
| NTAG210 | 20 | 31 |
| NTAG212 | 41 | 52 |

The `chameleon-ntag-slot-v1` checksum covers the whole canonical document.
Duplicate/unknown members, oversized input, incomplete images, unsupported
policies and metadata corruption are rejected. Checksums detect corruption,
not source forgery. These files contain credential material and must remain
private; the CLI reports summary/status rather than image or key contents.

An ordinary card BIN/EML/Flipper file lacks some private emulator state and is
not silently promoted to a full profile. Import takes a complete snapshot and
can copy its profile to another explicitly selected target. Restore instead
requires the same device fingerprint and the snapshot's matching explicit slot.

Nicknames, LF data, device settings, authentication logs, live RF sessions and
secure-erasure semantics are not part of the package. The original Classic
slot's opaque legacy PRNG is never normalized or set.

## Versioned firmware memory API

Provisional Ultra IDs 4045/4046 require maintainer allocation review.

4045 request: `slot:u8` (0-based). It requires Reader mode and exact equality
with the currently selected slot.

4045 response / 4046 request:

`version:u8=1 | slot:u8 | tag_type:u16be | physical_pages:u16be |
total_pages:u16be | flags:u8 | reserved2:u8 | reserved3:u8 | memory[total*4]`.

Flags encode magic in bit 0, write mode in bits 1–3, detection in bit 4, and
opaque reserved bits 5–7. Word/byte fields are explicitly encoded. The writer
checks exact type, counts, payload length and the explicit active slot before
any mutation. Both current and requested write modes must be normal/denied/
deceive. It accesses only the validated allocated MF0/NTAG memory extent.

The existing card-page commands remain separate. The new API avoids relying on
their missing read bounds or on counter setters that cannot reconstruct every
hidden auth/tearing byte.

## Apply and recovery

Preview captures the target twice while restoring host context. Apply rechecks
inventory and target, verifies the before-slot file, then initializes and writes
only the target. It switches away/back to reload persisted data and compares two
complete reads, including private pages and opaque configuration.

Disabling an HF-only target can auto-select a different slot; the workflow
explicitly reselects before verification. Global slot configuration is saved
only after returning to the original active slot. Unrelated slot type/enable
inventory and restored mode/selection are checked.

Device updates are not atomic. Failure reports stage, possible changes,
verification outcome and the verified before-slot path. There is no automatic
rollback or retry. Use a fresh explicit restore from the checksum-verified
snapshot. If original-slot selection fails, Reader mode is retained so a
partial target is not exposed in Tag mode. Cancellation remains cancellation.

Use one USB controller; do not press device buttons or run another reader client
during the transaction. Per-command binding and host checks are not a lease over
the complete multi-command workflow. Reload and global-save acknowledgement do
not independently prove power-loss/crash consistency of the entire slot table.

## Validation boundary

Stateful host fixtures model save-on-switch, factory flash writes, complete
private memory, disable auto-selection, failed persistence and mode/selection
cleanup. Tests cover all nine types, corruption/stale plans, protected targets,
backup/storage failures, cancellation, no-op apply, empty/disabled restore,
legacy PRNG preservation and actual CLI preview/apply/recovery.

Native GCC/Clang sanitizer tests exercise the real writer's bounds/descriptor/
policy helper, including every truncated length and all type sizes. Both
firmware targets compile in the regular pipeline. Real device data reload,
external-reader behavior and power-cycle persistence remain separate acceptance
steps until explicitly recorded.
