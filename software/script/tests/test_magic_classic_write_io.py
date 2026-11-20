"""Exercise restore-source validation and durable pre-write recovery files."""

import copy
import hashlib
import json
import os
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from typing import Any, Optional
from unittest import TestProgram
from unittest.mock import MagicMock, patch

import magic_classic_io as read_io
import magic_classic_write_io as write_io
from magic_classic import MagicCardIdentity, MagicCardReadError
from magic_classic_write import MagicBlockChange, MagicWritePlan, MagicWriteProgress
from magic_classic_write_io import (
    WriteJournal,
    check_backup_paths,
    journal_path_for,
    load_restore_image,
)

BLOCK_ZERO = bytes.fromhex("01020304040804001122334455667788")
IMAGE = BLOCK_ZERO + bytes((index * 37 + 11) % 256 for index in range(1008))
IDENTITY = MagicCardIdentity(IMAGE[:4], b"\x04\x00", b"\x08")
IMAGE_SHA256 = hashlib.sha256(IMAGE).hexdigest()


def make_plan(blocks: tuple[int, ...] = (1, 2)) -> MagicWritePlan:
    desired: bytearray = bytearray(IMAGE)
    changes: list[MagicBlockChange] = []
    for block in blocks:
        offset = block * 16
        after = bytes([0xA0 + block]) * 16
        changes.append(MagicBlockChange(block, IMAGE[offset : offset + 16], after))
        desired[offset : offset + 16] = after
    return MagicWritePlan(IDENTITY, IMAGE, bytes(desired), tuple(changes))


def read_records(path):
    return [
        json.loads(line)
        for line in path.read_text(encoding="utf-8").splitlines()
    ]


def prepared_record(image=IMAGE):
    return {
        "format": "chameleon-classic-data-write-journal-v1",
        "profile": "gen1a-compatible-classic1k-ordinary-data",
        "sequence": 0,
        "state": "prepared",
        "uid": image[:4].hex().upper(),
        "before_sha256": hashlib.sha256(image).hexdigest(),
    }


def source_document(image=IMAGE):
    """Build an independent fixture for the read-only backup's public format."""
    return {
        "format": "chameleon-gen1a-classic1k-v1",
        "read_profile": "gen1a-compatible-classic1k",
        "uid": image[:4].hex().upper(),
        "atqa_wire_order": "0400",
        "sak": "08",
        "block_size": 16,
        "block_count": 64,
        "independent_reads": 2,
        "key_material_verified": False,
        "key_handling": "raw bytes returned by card; no key reconstruction or authentication",
        "sha256": hashlib.sha256(image).hexdigest(),
        "blocks": [
            image[offset : offset + 16].hex().upper()
            for offset in range(0, len(image), 16)
        ],
    }


def write_json(path, document):
    path.write_text(json.dumps(document), encoding="utf-8")


class RestoreSourceTest(unittest.TestCase):
    def test_bin_and_json_preserve_exact_card_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "source.bin"
            document = Path(directory) / "source.json"
            binary.write_bytes(IMAGE)
            write_json(document, source_document())
            with self.assertRaises(MagicCardReadError):
                load_restore_image(binary)
            self.assertEqual(load_restore_image(document), IMAGE)
            self.assertEqual(load_restore_image(binary, None, IMAGE_SHA256), IMAGE)
            self.assertEqual(load_restore_image(binary, "bin", IMAGE_SHA256), IMAGE)
            self.assertEqual(load_restore_image(document, "json"), IMAGE)
            self.assertEqual(load_restore_image(document, "json", IMAGE_SHA256), IMAGE)

    def test_non_1k_binary_images_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.bin"
            for size in (0, 16, 320, 1023, 1025, 2048, 4096):
                with self.subTest(size=size):
                    path.write_bytes(bytes(size))
                    with self.assertRaises(ValueError):
                        load_restore_image(path)

    def test_json_metadata_and_image_digest_are_checked(self):
        changes = (
            ("format", "unknown-format"),
            ("read_profile", "another-card-profile"),
            ("uid", "AABBCCDD"),
            ("block_size", 15),
            ("block_size", True),
            ("block_count", 63),
            ("independent_reads", True),
            ("independent_reads", 1),
            ("key_material_verified", 0),
            ("atqa_wire_order", "04"),
            ("sak", "18"),
            ("sha256", "0" * 64),
            ("blocks", [IMAGE[:16].hex()] * 63),
            ("blocks", [IMAGE[:16].hex()] * 65),
            ("blocks", ["00"] * 64),
            ("blocks", ["GG" * 16] * 64),
            ("blocks", [17] * 64),
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            for field, value in changes:
                with self.subTest(field=field, value_type=type(value)):
                    document = copy.deepcopy(source_document())
                    document[field] = value
                    write_json(path, document)
                    with self.assertRaises(ValueError):
                        load_restore_image(path)

    def test_json_missing_required_metadata_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            for field in (
                "format",
                "read_profile",
                "uid",
                "block_size",
                "block_count",
                "sha256",
                "blocks",
            ):
                with self.subTest(field=field):
                    document = source_document()
                    del document[field]
                    write_json(path, document)
                    with self.assertRaises(ValueError):
                        load_restore_image(path)

    def test_bcc_failure_cannot_be_hidden_by_a_recomputed_digest(self):
        invalid = bytearray(IMAGE)
        invalid[4] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            write_json(path, source_document(bytes(invalid)))
            with self.assertRaises(ValueError):
                load_restore_image(path)
            path = Path(directory) / "source.bin"
            path.write_bytes(invalid)
            with self.assertRaises(ValueError):
                load_restore_image(
                    path, "bin", hashlib.sha256(invalid).hexdigest()
                )

    def test_malformed_json_and_unknown_explicit_format_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            for content in ('{"blocks":', "[]", "null", '"not a backup"'):
                with self.subTest(content=content):
                    path.write_text(content, encoding="utf-8")
                    with self.assertRaises(ValueError):
                        load_restore_image(path)
            path.write_bytes(IMAGE)
            with self.assertRaises(ValueError):
                load_restore_image(path, "hex")

    def test_oversized_source_is_rejected_before_json_decoding(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "source.json"
            path.write_bytes(b" " * 65537)
            with self.assertRaises(MagicCardReadError):
                load_restore_image(path)

    def test_binary_prepared_record_can_survive_a_later_failed_or_torn_line(self):
        first = (json.dumps(prepared_record()) + "\n").encode("utf-8")
        later_records = (
            b"",
            b'{"state":"failed","error":"lost acknowledgement"}\n',
            b'{"state":"pending","block":1}\n{"torn":',
        )
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "before.bin"
            source.write_bytes(IMAGE)
            for later in later_records:
                with self.subTest(later=later):
                    journal_path_for(source).write_bytes(first + later)
                    self.assertEqual(load_restore_image(source), IMAGE)
                    self.assertEqual(
                        load_restore_image(source, "bin", IMAGE_SHA256), IMAGE
                    )

    def test_corrupted_ordinary_data_cannot_bypass_its_adjacent_before_digest(self):
        corrupted = bytearray(IMAGE)
        corrupted[16] ^= 1
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "before.bin"
            source.write_bytes(corrupted)
            journal_path_for(source).write_text(
                json.dumps(prepared_record()) + "\n", encoding="utf-8"
            )
            # An external digest for the corrupted bytes cannot override the
            # recovery journal's original-image binding.
            for expected in (None, hashlib.sha256(corrupted).hexdigest()):
                with self.subTest(expected=expected):
                    with self.assertRaises(MagicCardReadError):
                        load_restore_image(source, "bin", expected)

    def test_bad_or_incomplete_prepared_record_is_always_rejected(self):
        records = []
        for field, value in (
            ("format", "unknown"),
            ("profile", "another-profile"),
            ("sequence", 1),
            ("sequence", False),
            ("state", "pending"),
            ("uid", "AABBCCDD"),
            ("before_sha256", "0" * 64),
        ):
            record = prepared_record()
            record[field] = value
            records.append((json.dumps(record) + "\n").encode("utf-8"))
        records.extend((b"", b"not JSON\n", b"[]\n", json.dumps(prepared_record()).encode()))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "before.bin"
            source.write_bytes(IMAGE)
            for content in records:
                with self.subTest(content=content):
                    journal_path_for(source).write_bytes(content)
                    for expected in (None, IMAGE_SHA256):
                        with self.assertRaises(MagicCardReadError):
                            load_restore_image(source, "bin", expected)

    def test_expected_digest_is_checked_for_both_source_formats(self):
        with tempfile.TemporaryDirectory() as directory:
            for suffix in ("bin", "json"):
                source = Path(directory) / f"source.{suffix}"
                if suffix == "bin":
                    source.write_bytes(IMAGE)
                else:
                    write_json(source, source_document())
                for expected in ("0" * 64, "1", "GG" * 32, ""):
                    with self.subTest(suffix=suffix, expected=expected):
                        with self.assertRaises(MagicCardReadError):
                            load_restore_image(source, suffix, expected)
                self.assertEqual(
                    load_restore_image(source, suffix, IMAGE_SHA256.upper()), IMAGE
                )


class BackupPathTest(unittest.TestCase):
    def test_new_backup_and_journal_paths_are_only_checked_not_created(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            check_backup_paths(target)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_existing_image_or_companion_journal_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            journal = Path(str(target) + ".journal.jsonl")
            for existing in (target, journal):
                with self.subTest(existing=existing.name):
                    existing.write_bytes(b"existing recovery evidence")
                    with self.assertRaises(MagicCardReadError):
                        check_backup_paths(target)
                    self.assertEqual(existing.read_bytes(), b"existing recovery evidence")
                    existing.unlink()

    def test_missing_directory_is_not_created(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "missing" / "before.bin"
            with self.assertRaises(MagicCardReadError):
                check_backup_paths(target)
            self.assertEqual(list(Path(directory).iterdir()), [])


class WriteJournalTest(unittest.TestCase):
    def test_create_persists_original_image_and_bound_receipt(self):
        plan = make_plan()
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            journal = WriteJournal.create(plan, target)
            try:
                receipt = journal.backup
                self.assertEqual(receipt.path, target.absolute())
                self.assertEqual(receipt.journal_path, journal_path_for(target.absolute()))
                self.assertEqual(receipt.size, 1024)
                self.assertEqual(receipt.image_sha256, hashlib.sha256(IMAGE).hexdigest())
                self.assertEqual(target.read_bytes(), IMAGE)
                self.assertNotEqual(target.read_bytes(), plan.desired_image)
                journal.validate_backup(plan)
                prepared = read_records(receipt.journal_path)
                self.assertEqual(len(prepared), 1)
                self.assertEqual(prepared[0]["state"], "prepared")
                self.assertEqual(prepared[0]["sequence"], 0)
                self.assertEqual(prepared[0]["planned_blocks"], [1, 2])
                self.assertEqual(prepared[0]["before_sha256"], receipt.image_sha256)
                self.assertEqual(
                    prepared[0]["desired_sha256"],
                    hashlib.sha256(plan.desired_image).hexdigest(),
                )
                self.assertEqual(prepared[0]["uid"], IDENTITY.uid.hex().upper())
                self.assertRegex(prepared[0]["operation_id"], r"^[0-9a-f]{32}$")
                # The raw before-image belongs in its dedicated private file;
                # journal records carry identifiers, hashes and progress.
                self.assertNotIn(IMAGE.hex().upper(), receipt.journal_path.read_text())
            finally:
                journal.close()

    def test_pending_verified_and_complete_lines_are_synced_before_return(self):
        plan = make_plan((1,))
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            real_fsync = os.fsync
            with patch.object(write_io.os, "fsync", wraps=real_fsync) as sync:
                journal = WriteJournal.create(plan, target)
                try:
                    count = sync.call_count
                    journal.attempt(1)
                    self.assertEqual(sync.call_count, count + 1)
                    self.assertEqual(read_records(journal.backup.journal_path)[-1]["state"], "pending")
                    journal.verified(1)
                    self.assertEqual(sync.call_count, count + 2)
                    journal.finish()
                    self.assertEqual(sync.call_count, count + 3)
                    records = read_records(journal.backup.journal_path)
                    self.assertEqual(
                        [item["state"] for item in records],
                        ["prepared", "pending", "verified", "complete"],
                    )
                    self.assertEqual([item["sequence"] for item in records], [0, 1, 2, 3])
                    self.assertEqual(len({item["operation_id"] for item in records}), 1)
                    self.assertEqual(records[1]["block"], 1)
                    self.assertEqual(records[2]["block"], 1)
                    self.assertEqual(records[3]["verified_blocks"], [1])
                finally:
                    journal.close()

    def test_incomplete_transaction_cannot_emit_complete(self):
        plan = make_plan()
        with tempfile.TemporaryDirectory() as directory:
            journal = WriteJournal.create(plan, Path(directory) / "before.bin")
            try:
                with self.assertRaises(MagicCardReadError):
                    journal.finish()
                journal.attempt(1)
                with self.assertRaises(MagicCardReadError):
                    journal.finish()
                progress = MagicWriteProgress(
                    attempted_blocks=(1,),
                    possible_written_blocks=(1,),
                    failed_stage="data-frame",
                    failed_block=1,
                )
                journal.fail(progress, TimeoutError("lost data acknowledgement"))
                failed = read_records(journal.backup.journal_path)[-1]
                self.assertEqual(failed["state"], "failed")
                self.assertEqual(failed["progress"]["attempted_blocks"], [1])
                self.assertEqual(failed["progress"]["possible_written_blocks"], [1])
                self.assertEqual(failed["progress"]["verified_blocks"], [])
                self.assertEqual(failed["progress"]["failed_stage"], "data-frame")
                self.assertEqual(failed["progress"]["failed_block"], 1)
                self.assertIn("lost data acknowledgement", failed["error"])
            finally:
                journal.close()

    def test_invalid_duplicate_or_overlapping_attempt_is_refused(self):
        invalid: tuple[Any, ...] = (-1, 0, 3, 63, 64, True, 1.0, "1", None)
        with tempfile.TemporaryDirectory() as directory:
            journal = WriteJournal.create(make_plan(), Path(directory) / "before.bin")
            try:
                for block in invalid:
                    with self.subTest(block=block):
                        with self.assertRaises(MagicCardReadError):
                            journal.attempt(block)
                journal.attempt(1)
                with self.assertRaises(MagicCardReadError):
                    journal.attempt(2)
                with self.assertRaises(MagicCardReadError):
                    journal.verified(2)
                journal.verified(1)
                with self.assertRaises(MagicCardReadError):
                    journal.attempt(1)
                self.assertEqual(
                    [record["state"] for record in read_records(journal.backup.journal_path)],
                    ["prepared", "pending", "verified"],
                )
            finally:
                journal.close()

    def test_changed_before_image_or_another_plan_cannot_be_validated(self):
        for saved in (IMAGE[:-1], IMAGE + b"\x00", bytes(1024)):
            with self.subTest(length=len(saved)):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "before.bin"
                    journal = WriteJournal.create(make_plan(), target)
                    try:
                        target.write_bytes(saved)
                        with self.assertRaises(MagicCardReadError):
                            journal.validate_backup(make_plan())
                        with self.assertRaises(MagicCardReadError):
                            journal.attempt(1)
                        self.assertEqual(
                            [r["state"] for r in read_records(journal.backup.journal_path)],
                            ["prepared"],
                        )
                    finally:
                        journal.close()
        with tempfile.TemporaryDirectory() as directory:
            plan = make_plan()
            journal = WriteJournal.create(plan, Path(directory) / "before.bin")
            try:
                for another in (
                    make_plan((1,)),
                    replace(plan, identity=replace(IDENTITY, uid=b"\x11\x22\x33\x44")),
                    replace(plan, original_image=IMAGE[:-1]),
                ):
                    with self.subTest(another=another):
                        with self.assertRaises(MagicCardReadError):
                            journal.validate_backup(another)
            finally:
                journal.close()

    def test_corrupted_published_backup_is_preserved_but_create_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            real_publish = write_io.publish_backup

            def corrupt_after_publication(path, image):
                real_publish(path, image)
                path.write_bytes(image[:-1])

            with (
                patch.object(write_io, "publish_backup", side_effect=corrupt_after_publication),
                self.assertRaises(MagicCardReadError),
            ):
                WriteJournal.create(make_plan(), target)
            self.assertEqual(target.read_bytes(), IMAGE[:-1])
            self.assertFalse(journal_path_for(target).exists())

    def test_backup_and_journal_filename_races_do_not_replace_evidence(self):
        for collision in ("image", "journal"):
            with self.subTest(collision=collision):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "before.bin"
                    real_publish = write_io.publish_backup
                    journal_path = journal_path_for(target)

                    def race(path, image):
                        if collision == "image":
                            path.write_bytes(b"other recovery image")
                        else:
                            journal_path.write_bytes(b"other recovery journal")
                        real_publish(path, image)

                    with (
                        patch.object(write_io, "publish_backup", side_effect=race),
                        self.assertRaises(FileExistsError) as raised,
                    ):
                        WriteJournal.create(make_plan(), target)
                    if collision == "image":
                        self.assertEqual(target.read_bytes(), b"other recovery image")
                        self.assertFalse(journal_path.exists())
                    else:
                        self.assertEqual(target.read_bytes(), IMAGE)
                        self.assertEqual(journal_path.read_bytes(), b"other recovery journal")
                        receipt = raised.exception.__dict__["backup"]
                        self.assertEqual(receipt.path, target.absolute())
                        self.assertEqual(receipt.image_sha256, IMAGE_SHA256)

    def test_failed_journal_create_keeps_the_verified_before_image_receipt(self):
        real_open = Path.open
        real_fsync = os.fsync
        for failure in ("open", "partial", "short", "flush", "sync", "sync-close"):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "before.bin"
                    journal_path = journal_path_for(target)
                    state: dict[str, Optional[int]] = {"journal_fd": None}

                    def open_stream(path, *args, **kwargs):
                        mode = args[0] if args else kwargs.get("mode", "r")
                        if path != journal_path or mode != "xb":
                            return real_open(path, *args, **kwargs)
                        if failure == "open":
                            raise OSError("journal open failed")
                        actual = real_open(path, *args, **kwargs)
                        state["journal_fd"] = actual.fileno()
                        wrapper = MagicMock()
                        wrapper.fileno.side_effect = actual.fileno

                        def write(content):
                            if failure in ("partial", "short"):
                                count = actual.write(content[:7])
                                if failure == "partial":
                                    raise OSError("prepared record partial write")
                                return count
                            return actual.write(content)

                        def flush():
                            if failure == "flush":
                                raise OSError("prepared flush failed")
                            actual.flush()

                        def close():
                            actual.close()
                            if failure == "sync-close":
                                raise OSError("journal close failed")

                        wrapper.write.side_effect = write
                        wrapper.flush.side_effect = flush
                        wrapper.close.side_effect = close
                        return wrapper

                    def sync(fd):
                        if failure.startswith("sync") and fd == state["journal_fd"]:
                            raise OSError("prepared sync failed")
                        return real_fsync(fd)

                    with (
                        patch.object(Path, "open", new=open_stream),
                        patch.object(write_io.os, "fsync", side_effect=sync),
                        self.assertRaises(OSError) as raised,
                    ):
                        WriteJournal.create(make_plan(), target)
                    receipt = raised.exception.__dict__["backup"]
                    self.assertEqual(receipt.path, target.absolute())
                    self.assertEqual(receipt.journal_path, journal_path.absolute())
                    self.assertEqual(receipt.image_sha256, IMAGE_SHA256)
                    self.assertEqual(target.read_bytes(), IMAGE)
                    self.assertEqual(journal_path.exists(), failure != "open")
                    if failure == "sync-close":
                        self.assertIn(
                            "journal close failed",
                            str(raised.exception.__dict__["journal_close_error"]),
                        )

    def test_backup_storage_failures_do_not_create_a_partial_destination(self):
        real_factory = read_io.tempfile.NamedTemporaryFile
        for failure in ("partial", "short", "flush", "sync", "close"):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "before.bin"

                    def faulty_file(*args, **kwargs):
                        actual = real_factory(*args, **kwargs)
                        wrapper = MagicMock()
                        wrapper.name = actual.name
                        wrapper.__enter__.return_value = wrapper
                        wrapper.fileno.side_effect = actual.fileno

                        def write(content):
                            if failure in ("partial", "short"):
                                written = actual.write(content[:7])
                                if failure == "partial":
                                    raise OSError("storage failed after partial write")
                                return written
                            return actual.write(content)

                        def flush():
                            if failure == "flush":
                                raise OSError("backup flush failed")
                            actual.flush()

                        def finish(*_):
                            actual.close()
                            if failure == "close":
                                raise OSError("backup close failed")
                            return False

                        wrapper.write.side_effect = write
                        wrapper.flush.side_effect = flush
                        wrapper.__exit__.side_effect = finish
                        return wrapper

                    with (
                        patch.object(read_io.tempfile, "NamedTemporaryFile", side_effect=faulty_file),
                        patch.object(
                            read_io.os,
                            "fsync",
                            side_effect=OSError("backup sync failed") if failure == "sync" else None,
                        ),
                        self.assertRaises(OSError),
                    ):
                        WriteJournal.create(make_plan(), target)
                    self.assertEqual(list(Path(directory).iterdir()), [])

    def test_journal_storage_failure_preserves_prefix_and_stops_appending(self):
        for failure in ("partial", "short", "flush", "sync"):
            with self.subTest(failure=failure):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "before.bin"
                    journal = WriteJournal.create(make_plan(), target)
                    actual = journal._stream
                    wrapper = MagicMock()
                    wrapper.fileno.side_effect = actual.fileno
                    wrapper.close.side_effect = actual.close

                    def write(content):
                        if failure in ("partial", "short"):
                            written = actual.write(content[:7])
                            if failure == "partial":
                                raise OSError("torn pending line")
                            return written
                        return actual.write(content)

                    def flush():
                        if failure == "flush":
                            raise OSError("journal flush failed")
                        actual.flush()

                    wrapper.write.side_effect = write
                    wrapper.flush.side_effect = flush
                    journal._stream = wrapper
                    try:
                        with (
                            patch.object(
                                write_io.os,
                                "fsync",
                                side_effect=OSError("journal sync failed") if failure == "sync" else None,
                            ),
                            self.assertRaises(OSError),
                        ):
                            journal.attempt(1)
                        # Closing may flush unsynced bytes; none is interpreted
                        # as permission to retry or repair a failed journal.
                        actual.flush()
                        failed_bytes = journal.backup.journal_path.read_bytes()
                        first = json.loads(failed_bytes.splitlines()[0])
                        self.assertEqual(first["state"], "prepared")
                        with self.assertRaises(OSError):
                            journal.fail(MagicWriteProgress(), OSError("original error"))
                        actual.flush()
                        self.assertEqual(
                            journal.backup.journal_path.read_bytes(), failed_bytes
                        )
                        self.assertEqual(target.read_bytes(), IMAGE)
                    finally:
                        journal.close()

    def test_journal_close_failure_is_reported_without_deleting_recovery_files(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "before.bin"
            journal = WriteJournal.create(make_plan(), target)
            actual = journal._stream
            wrapper = MagicMock()

            def failing_close():
                actual.close()
                raise OSError("journal close failed")

            wrapper.close.side_effect = failing_close
            journal._stream = wrapper
            with self.assertRaises(OSError):
                journal.close()
            self.assertEqual(target.read_bytes(), IMAGE)
            self.assertEqual(read_records(journal.backup.journal_path)[0]["state"], "prepared")

    def test_changed_journal_ownership_blocks_a_new_pending_record(self):
        with tempfile.TemporaryDirectory() as directory:
            journal = WriteJournal.create(make_plan(), Path(directory) / "before.bin")
            try:
                before = journal.backup.journal_path.read_bytes()
                with (
                    patch.object(write_io.os.path, "samestat", return_value=False),
                    self.assertRaises(OSError),
                ):
                    journal.attempt(1)
                self.assertEqual(journal.backup.journal_path.read_bytes(), before)
            finally:
                journal.close()


if __name__ == "__main__":
    TestProgram()
