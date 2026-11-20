# Guarded ordinary-data writes for Gen1A-compatible Classic 1K

This is a follow-up to the read-only tools in PR #539. It uses the same
four-byte UID / SAK `08` read profile and existing official Ultra raw reader
commands. The read-only `info` and `dump` commands retain their behavior.

## Scope

Only the 47 ordinary data blocks are writable: addresses `1..62`, excluding
every sector trailer (`block % 4 == 3`). Block 0, UID/BCC, sector keys, access
control, configuration, wipe and seal operations are outside this API.
Restore means ordinary-data restoration, not whole-card restoration.

Keep one stationary card in the field, with one reader operator and no
concurrent CDC/BLE reader commands. Equal UID and memory cannot prove that a
physical card was not replaced by an identical clone. The firmware does not
provide an exclusive Gen1A transaction lease, and this host workflow does not
claim to prevent another client from changing RF state.

## Preview, apply and explicit recovery

Preview is the default:

```text
hf mf magic wrbl -b 4 -d 00112233445566778899AABBCCDDEEFF
```

The preview reads the current complete image twice and reports changed block
addresses and original/desired image digests. It does not send a programming
command or print the card image and sector keys.

To apply, supply `--apply` and a new private before-image filename. Its
directory must already exist:

```text
hf mf magic wrbl -b 4 -d 00112233445566778899AABBCCDDEEFF --apply --backup private-backups/before-write.bin
```

The existing interactive CLI splits input on whitespace rather than parsing
quoted paths. Use a path without spaces for these commands; command-tokenizer
changes are outside this follow-up.

The before-image is exactly 1024 raw bytes, including any sector-key fields
returned by the card. Keep it private. Its append-only transaction journal is
`before-write.bin.journal.jsonl` in the same directory. Neither existing file
may be replaced.

Restoring is a fresh, explicitly requested operation:

```text
hf mf magic restore -i private-backups/before-write.bin
hf mf magic restore -i private-backups/before-write.bin --apply --backup private-backups/before-restore.bin
```

A BIN recovery source must match the SHA-256 in its adjacent journal's first
complete `prepared` record. A later failed/pending/torn final record does not
authorize automatic continuation, nor does it invalidate a correct original
before-image. For a BIN without that journal, supply `--sha256` with an
independently retained expected image digest. JSON from `magic dump --format
json` contains its image digest and is validated directly; an external
`--sha256` can be added to that check too.

The source must have exactly 64 complete blocks and a valid UID/BCC. JSON
profile, block counts, identity syntax and SHA-256 are checked before RF.
Source differences in Block 0 or any trailer reject the entire plan. The
tool never silently drops protected differences, reconstructs missing keys,
or treats authenticated-read zero placeholders as actual trailer keys.

## Programming and verification gates

1. Validate the entire plan and allowed address set before apply accesses RF.
2. Re-read the whole card twice. Identity and all 1024 bytes must still match
   the plan's original image.
3. Write, flush, sync and close the original image with no-clobber
   publication, then reopen the published path and verify its exact bytes.
4. Create an exclusive journal and sync its `prepared` record. Failure at
   either storage gate leaves no programming command sent.
5. For each changed block, start a fresh RF session, scan/select/HALT/unlock,
   check Block 0 and the target's expected old bytes, then sync `pending`.
6. Send `A0 block + CRC`, require successful status and exact `0A`, then
   immediately send the prepared 16-byte data frame plus CRC and check its
   status and ACK. There is no filesystem operation between those two frames.
7. Release RF, start another session and CRC-read the target independently.
   Only a matching readback permits a synced `verified` record and the next
   block.
8. Finally read the complete image twice again. It must equal the desired
   image, including every unplanned and protected byte. Finish/close the RF
   journal and restore the original device mode before reporting success.

Unchanged blocks are skipped; a no-difference apply performs no programming
and creates no transaction files. File sync/readback are explicit host gates,
not a guarantee against faulty storage or sudden power loss.

## Failure and progress semantics

The runtime reports three different facts:

- `attempted_blocks`: A0 was about to be sent.
- `possible_written_blocks`: sending the 16-byte data frame was about to
  begin. Lost ACK, timeout, disconnect or cancellation cannot prove the card
  stayed unchanged after this point.
- `verified_blocks`: a new RF session read the requested bytes back.

The journal's pending line precedes A0 to keep disk latency out of the
two-frame write window. An unresolved pending record after a crash must be
treated conservatively as uncertain, even if a stopped runtime would have
known that its data frame was never sent.

On failure the transaction stops. It does not retry, continue other blocks,
roll back, or resume after restart. It retains the original image location,
journal, stage, block and progress; RF/journal/mode cleanup errors do not erase
the programming outcome. Cancellation remains cancellation. A transaction
object cannot be reused for a second apply.

`complete` in the journal denotes successful RF image verification and
recording. Device-mode restoration occurs afterwards; failure there is still
reported along with already-verified blocks. Individual verified blocks do
not establish overall success if final image or host cleanup checks fail.

## Validation

From `software/script`, with the existing CLI requirements:

```sh
python -m unittest discover -s tests -p "test_magic_classic*.py" -v
```

Tests cover protected addresses, forged/stale plans, source corruption,
storage failures and races, durable pending-before-A0 order, both ACKs,
data-frame uncertainty, fresh-session readback, final protected-byte checks,
interrupts and cleanup. The original read-only tests continue to assert no
write opcode on their paths. CI covers Python 3.9 and 3.13.

Physical acceptance must use one ordinary block: save the original image,
preview, program temporary data, independently read it back, explicitly
restore from the verified before-image in a new transaction, then compare
the final full image and device state with the original. This single-block
test does not claim every writable address or every magic-card variant was
physically tested.

On November 3, 2026, this sequence passed on a known unsealed UFUID with
official v2.3.0 firmware. Preview sent no programming frames. Temporary
programming and explicit recovery each changed only block 4 and passed fresh
readback and complete-image verification. An independent host-side guard
checked the saved before-image and pending record before each of the two A0
commands. The final 1024 bytes equaled the initial image; mode, active slot,
slot types/enables, HF14A configuration and settings digest were preserved.
The real CLI entrypoint also completed a restore preview and returned to the
original Emulator mode. Actual images, payloads and keys remain private.

## Primary references

- [Fixed Proxmark3 Gen1A/UFUID protocol notes](https://github.com/RfidResearchGroup/proxmark3/blob/754bbae3d946429a6354cd34a97a2dd29ce6f157/doc/magic_cards_notes.md)
- [Official Classic two-phase writer](https://github.com/RfidResearchGroup/ChameleonUltra/blob/4c5bc2cb6b023d23f7bbc383c9b2b5e39f95419e/firmware/application/src/rfid/reader/hf/rc522.c)
