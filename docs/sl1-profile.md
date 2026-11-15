# Classic and SL1 CardAuth profiles

This optional profile combines Classic memory, ISO14443-A selection parameters and a separately supplied AES-128 CardAuth key. It implements the `76 04 90` / `72` three-pass CardAuth exchange and the existing Classic authentication/read path. It does not recover AES keys or implement general Plus SL2/SL3. The CardAuth scenario is read-only. Successful AES authentication does not impose an additional access gate on Classic authentication.

The address `0x9004` identifies the standard optional SL1 CardAuth key, not a slot selected for one installation. [NXP AN12706, section 2.3.1, Table 2](https://www.nxp.com/docs/en/application-note/AN12706.pdf) documents the `76 04 90` request. Each profile supplies its own 16-byte secret. Other AES key addresses and their associated operations are outside this contract.

## CLI usage

The `hf mf profile` group reads cards using supplied known Classic keys, manages profile files offline, and uploads to firmware that advertises the profile commands. A known-key dictionary contains one six-byte hex key per line; blank lines and `#` comments are allowed.

```text
hf mf profile read --type 1k --keys-file known.dic --output card.json
hf mf profile file inspect --file card.json
hf mf profile file export --file card.json --aes-key-prompt --output card-auth.json
hf mf profile slot upload --slot 3 --file card-auth.json
hf mf profile slot status --slot 3
```

Choose the actual card geometry for `read`. Failed reads remain unknown with their error statuses; reading a trailer does not recover its masked Key A. To retry with the same identity and known sector keys, use `read --file card.json --output retry.json`. The hidden prompt accepts an already known AES-128 key. Saving it records `provided`, not RF verification. Enabled CardAuth also requires an acquired ATS and complete valid Classic memory.

CLI slot numbers are 1–8. Upload saves explicitly to the chosen slot without changing the active selection. `upload --stage-only` leaves a volatile transaction; `slot save --slot 3` commits it, and `slot cancel --slot 3` discards it. `slot disable` retains the saved key, `slot enable` requests activation of the bound configuration, and `slot clear` removes CardAuth while preserving Classic data. Each control requires `--slot`. Failures retain their device status and are never retried automatically. Check status after a failed save before deciding to retry.

The Flutter GUI imports and exports the same structured JSON. Files and raw transport debug can contain the complete AES key and Classic keys. Keep those private; ordinary `file inspect` and slot status omit the key values.

## Interchange format

The CLI and GUI use UTF-8 JSON with `format: "chameleon-sl1-profile"` and integer `version: 1`. Fields are required unless described as nullable. Hex strings use two uppercase characters per byte, without separators. A block's unknown byte is represented by `??`, never by a guessed zero. Unknown fields and unsupported versions are rejected so an import/export cannot silently discard data.

```json
{
  "format": "chameleon-sl1-profile",
  "version": 1,
  "name": "Example",
  "hf14a": {"uid": "01020304", "atqa": "0400", "sak": "08", "ats": null},
  "classic": {
    "type": "mini",
    "provenance": "read",
    "blocks": [],
    "keys": []
  },
  "cardAuth": null
}
```

The abbreviated example omits the required block and sector entries. It is not an uploadable fixture. `classic.type` is `mini`, `1k`, `2k` or `4k`, requiring respectively 20/64/128/256 blocks and 5/16/32/40 sector entries. Sectors 0–31 have four blocks; sectors 32–39 have sixteen. `hf14a.uid` contains four or seven bytes. ATQA is stored in the same byte order as the CU API. SAK contains one byte. ATS is either null (not acquired) or the complete TL-prefixed bytes; an acquired absence is the empty string. A nonempty ATS must have a matching TL and at most 254 bytes.

Each block is an object with exactly `data` (32 hex/`??` characters), `status` (`unread`, `read`, `error` or `imported`), `key` (`A`, `B` or null), and `errors` (a list of objects containing `key`, nullable integer `status`, and string `reason`). `unread` and `error` blocks contain only unknown bytes. A successful read can still contain masked trailer key bytes. Errors retain failed A/B attempts even if a later attempt succeeds. A fully known zero-valued block remains distinct from an unknown block.

Each sector contains exactly `a` and `b`. Each is an object with `value` (six hex bytes or null), `status` (`unknown`, `provided` or `verified`) and nullable string `evidence`. Unknown keys have a null value; known keys have a value. A read trailer does not establish Key A. Key B is treated as known only if it was independently supplied/verified or its access conditions permit reading those bytes. Read/export stores known A/B separately from observed block bytes. To construct an emulation image, known key values fill unknown trailer positions; conflicting known bytes are rejected.

`classic.provenance` is `read`, `imported`, `synthetic` or `legacy-unverified`. An image is complete only when every required byte is known after merging the key entries, the access-bit complements are valid, and provenance is not `legacy-unverified`. Legacy records remain usable by existing Classic paths; their byte coverage is not inferred from length or zero-filled bytes.

`cardAuth` is null or an object with exactly `enabled` (boolean), `scenario: "sl1-cardauth-9004"`, `keyNumber: 36868`, `key` (16 hex bytes), and `verification` (an object with `status` equal to `provided` or `verified` and nullable string `evidence`). Entering or saving a key does not verify it against a card. An enabled CardAuth profile requires an acquired valid ATS of at least two bytes. Import/export retains the full key and verification state. Ordinary firmware status never returns the AES key. Profiles and raw debug logs can contain credentials and should be handled accordingly.

## Firmware commands (version 1)

Command numbers below are proposed by this implementation. Clients discover them through `GET_DEVICE_CAPABILITIES`, rather than firmware version strings. They are advertised by Ultra; Lite keeps its existing Classic API. All integers in command payloads are big-endian. Slots use firmware indices 0–7. Each request begins with version 1 and a slot byte. An unsupported version/length/value returns `STATUS_PAR_ERR`; field presence or incompatible transaction state returns `STATUS_CMD_ERR`. New commands never change the active slot selection.

| Number | Name | Payload after version/slot |
| --- | --- | --- |
| 4045 | `SL1_PROFILE_STATUS` | Empty |
| 4046 | `SL1_PROFILE_BEGIN` | tag type u16, UID length u8, UID padded to 7 bytes, ATQA[2], SAK u8, ATS length u8, scenario u8, enabled u8, key number u16, AES key[16], ATS bytes |
| 4047 | `SL1_PROFILE_WRITE` | first block u16, 1–8 complete blocks (16 bytes each) |
| 4048 | `SL1_PROFILE_SAVE` | Empty |
| 4049 | `SL1_PROFILE_CANCEL` | Empty |
| 4050 | `SL1_PROFILE_CLEAR` | Empty |
| 4051 | `SL1_PROFILE_SET_ENABLED` | enabled u8 |

BEGIN has a fixed 36-byte prefix including version/slot, followed by the ATS. UID padding is zero. Scenario 1 means CardAuth 9004; key number must be 0x9004. Scenario 0 means Classic only and requires enabled/key number/key to be zero. Type is the existing MF1 type ID 1000–1003. No key is built into the firmware. The stage buffer is separate from the live slot. BEGIN replaces a previous staged transaction, WRITE tracks block coverage, and SAVE requires every block. CANCEL erases only the stage buffer. SAVE/CLEAR/SET_ENABLED are rejected while an RF field is present or while in reader mode. A transport disconnect never implicitly commits. The stage is volatile and is lost on reset/sleep; the last successful save is persistent.

STATUS returns 14 bytes: version u8, slot u8, flags u8, tag type u16, scenario u8, key number u16, received blocks u16, expected blocks u16, last status u8, last stage u8. Flags are: bit 0 configured, bit 1 persistently enabled, bit 2 runtime enabled for this active slot, bit 3 staged, bit 4 stale/corrupt binding or incomplete persistent transaction, bit 5 runtime entropy ready; bit 6 durable incomplete or unknown transaction (a full profile retry is required). Bit 4 remains set as well for incomplete state, preserving older clients. Invalid optional key-1 metadata without a transaction sets bit 4 without bit 6. Stages are 0 idle, 1 staging, 2 disabling previous extension, 3 saving image, 4 saving slot configuration, 5 saving extension, 6 loading runtime, 7 saved. Status contains no credential or verification claim about RF acceptance.

## Persistence and compatibility

The existing MF1 dump layout is unchanged. A 64-byte versioned record at file ID `0x1300 + slot`, key 1 holds the optional AES configuration and a SHA-256 binding to tag type, UID/ATQA/SAK/ATS and the complete memory image. A CRC32 protects the record structure and key from accidental corruption. Existing slots with no record start with CardAuth disabled.

SAVE first persists a separate 64-byte transaction marker at the same per-slot file ID, key 2. Successful marker submission is followed by exact-length, exact-byte and CRC readback before any image/config mutation. The marker has magic `SL1T`, version/slot/intended type, the intended image digest and CRC32; its enabled/scenario/key-number/key fields are zero. It contains no CardAuth credential. The normal key-1 profile record and MF1 dump layout are unchanged.

Presence of key 2 quarantines that slot's HF owner, even if the marker payload is corrupt or cannot be read. Ordinary HF load, live rebind, sensing and legacy saving are blocked before interpreting the dump under slot metadata. LF and other slots are unaffected. This gate survives reboot and prevents an interrupted geometry/type replacement from overwriting a committed new dump with stale RAM or interpreting it as the previous tag format. Corrupt optional key-1 metadata alone continues to leave ordinary Classic usable with CardAuth off.

After the marker, SAVE writes the full MF1 image and selected slot's HF configuration, then validates/reloads the coherent committed image while HF remains quarantined. The configuration commit is based on exactly one valid persisted configuration, or immutable factory defaults only when absence is proven. Lookup errors, duplicates, unreadable/unsupported records fail conservatively. Only the target HF fields change in flash and live RAM; unrelated unsaved active-selection/LF/neighbor edits stay volatile, with a CRC reference to the actual persisted image so ordinary configuration SAVE still detects them. It verifies the persisted slot configuration and image digest, writes the final bound extension (or Classic-only tombstone), and verifies exact final bytes/CRC/binding before deleting the transaction marker last. A positive deletion count is not enough: tri-state FDS lookup must prove key 2 absent before binding or sensing; lookup errors and retained duplicates keep HF quarantined. Only successful completion releases quarantine and activates the live binding. Stage 6 (coherent image reload/validation) can therefore precede stage 5 (final extension and transaction-marker removal). Each failure returns its actual stage/status; these writes are not a single atomic transaction. Active RAM can adopt the already committed image during validation before the final extension succeeds, but that RAM cannot be saved or sensed through ordinary paths while the marker remains.

An incomplete or unknown transaction is exposed by STATUS flag bit 6 (and compatibility bit 4). The last status/stage describes the current boot's observed failure; after reboot the marker indicates incomplete recovery without claiming an unobserved failure phase. CLEAR, SET_ENABLED and legacy invalidation cannot erase transaction evidence or bypass quarantine. CANCEL only erases the volatile stage. Recovery requires an explicit complete profile SAVE retry, using the intended complete profile or a known complete backup; there is no automatic rollback or retry. A failed marker deletion returns a flash-write failure and a retained marker continues to quarantine the slot. Clients must inspect the result and retry explicitly. Factory wipe is rejected while any slot has a pending or unknown transaction, before records are deleted or reset is scheduled. The active-slot legacy HF-emulation namespaces (4000–4999 and 6000–6999) and HF sniff are blocked before dispatch, including getters that require a loaded HF handler. Dedicated profile commands remain available for status/recovery; independent ISO4 reader operations 6004/6005, LF, nicknames and slot selection remain available. Nonactive type/default/delete commands retain their target-slot transaction guards.

Legacy image/identity replacement, slot deletion/defaulting/type replacement and factory wipe invalidate the extension. Unrelated nickname, LF, detection and display settings preserve it. A binding mismatch never enables a key against different card data. Changing bytes back through legacy commands does not revive the old extension after invalidation. Successful disabling preserves the canonical stored key; CLEAR writes a canonical tombstone and preserves Classic data. Key-1 absence is accepted only when the FDS count/lookup proves absence. Unknown lookup state rejects CLEAR and legacy invalidation without mutating card data or acknowledging revocation. Multiple active key-1 records are treated as corrupt optional metadata: AES remains off while Classic remains usable. Explicit CLEAR can canonicalize them; ambiguous enable/disable requires CLEAR or a complete profile replacement first. CLEAR and full profile replacement remove all prior active key-1 records, prove absence, and write/verify exactly one canonical record, so a latent duplicate cannot revive after an acknowledged revocation. Enable/disable accepts only a proven unique record and uses the normal FDS atomic update path without deleting that record first. Its intermediate state is a disabled copy of the same credential and binding, followed by the requested enabled state; a later failure therefore does not require re-importing the profile merely to recover its key. Delete/rewrite/readback failure returns failure rather than claiming revocation; a failed replacement may already have removed prior key records and requires explicit recovery. FDS deletion/tombstones do not provide secure erasure.

RF runtime, storage, commands, GUI and CLI tests use synthetic data. [Host test setup](../firmware/tests/sl1/README.md) describes the firmware regression suite and its controlled boundaries. Host tests do not establish physical RF/BLE operation or persistence through actual loss of power; those require a separate bounded hardware acceptance.
