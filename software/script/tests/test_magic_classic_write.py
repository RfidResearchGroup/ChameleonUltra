"""Check write ordering and uncertainty with an independent in-memory PICC."""

from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from dataclasses import FrozenInstanceError, replace
from pathlib import Path
from typing import Any, Callable, Optional
from unittest import TestProgram
from unittest.mock import patch

from chameleon_com import Response
from chameleon_enum import Command, Status
from magic_classic import MagicCardIdentity, MagicCardReadError
from magic_classic_write import (
    WRITABLE_BLOCKS,
    MagicBlockChange,
    MagicCardWriteError,
    MagicClassicTransaction,
    MagicWritePlan,
    MagicWriteProgress,
)
from magic_classic_write_io import MagicBackupReceipt

UID = bytes.fromhex("11223344")
BLOCK0 = UID + b"\x44\x08\x04\x00" + bytes(range(8))
IMAGE = BLOCK0 + b"".join(bytes([block]) * 16 for block in range(1, 64))
IDENTITY = MagicCardIdentity(UID, b"\x04\x00", b"\x08")
BACKUP_PATH = Path("before.bin")
OFF = (0, 0, 0, 0, 0, 0)
HALT = (1, 0, 1, 1, 1, 0)
WAKE = (0, 1, 0, 0, 1, 0)
READ = (0, 1, 1, 0, 1, 1)
WRITE = (0, 1, 1, 0, 1, 0)


class MemoryCard:
    """Model only HALT, the two wake-ups, ordinary READ, and two-phase WRITE."""

    def __init__(self):
        self.image = bytearray(IMAGE)
        self.identity = IDENTITY
        self.events: list[str] = []
        self.frames: list[bytes] = []
        self.unlocked = False
        self.wake_started = False
        self.pending: Optional[int] = None
        self.raw_hook: Optional[Callable[[str, Response], Response]] = None
        self.scan_hook: Optional[Callable[[], None]] = None

    def hf14a_scan(self) -> list[dict[str, bytes]]:
        self.events.append("scan")
        if self.scan_hook is not None:
            self.scan_hook()
        return [
            {
                "uid": self.identity.uid,
                "atqa": self.identity.atqa,
                "sak": self.identity.sak,
            }
        ]

    def hf14a_raw_response(
        self,
        options: dict[str, int],
        resp_timeout_ms: int = 100,
        data: bytes = b"",
        bitlen: Optional[int] = None,
    ) -> Response:
        flags = tuple(
            options[name]
            for name in (
                "activate_rf_field",
                "wait_response",
                "append_crc",
                "auto_select",
                "keep_rf_field",
                "check_response_crc",
            )
        )
        assert resp_timeout_ms == 100
        self.frames.append(data)
        reply = b""
        if data == b"":
            assert flags == OFF and bitlen is None
            self.unlocked = self.wake_started = False
            self.pending = None
            event = "off"
        elif data == b"\x50\x00":
            assert flags == HALT and bitlen is None
            self.unlocked = self.wake_started = False
            event = "halt"
        elif data == b"\x40":
            assert flags == WAKE and bitlen == 7
            self.wake_started = True
            event, reply = "wake40", b"\x0a"
        elif data == b"\x43":
            assert flags == WAKE and bitlen is None and self.wake_started
            self.unlocked = True
            event, reply = "wake43", b"\x0a"
        elif len(data) == 2 and data[0] == 0x30:
            assert flags == READ and bitlen is None and self.unlocked
            block = data[1]
            assert block < 64
            event = f"read:{block}"
            reply = bytes(self.image[block * 16 : (block + 1) * 16])
        elif len(data) == 2 and data[0] == 0xA0:
            assert flags == WRITE and bitlen is None and self.unlocked
            block = data[1]
            assert block in WRITABLE_BLOCKS
            assert self.pending is None
            self.pending = block
            event, reply = f"a0:{block}", b"\x0a"
        elif len(data) == 16:
            assert flags == WRITE and bitlen is None and self.unlocked
            assert self.pending is not None
            block = self.pending
            self.image[block * 16 : (block + 1) * 16] = data
            self.pending = None
            event, reply = f"data:{block}", b"\x0a"
        else:
            raise AssertionError(f"unexpected PICC command {data.hex()}")
        self.events.append(event)
        response = Response(Command.HF14A_RAW, Status.HF_TAG_OK, reply)
        if self.raw_hook is not None:
            return self.raw_hook(event, response)
        return response

    @property
    def attempts(self) -> list[str]:
        return [event for event in self.events if event.startswith("a0:")]

    @property
    def programs(self) -> list[str]:
        return [event for event in self.events if event.startswith("data:")]


class JournalStub:
    def __init__(self, card: MemoryCard, plan: MagicWritePlan):
        self.card = card
        self.plan = plan
        self.backup = MagicBackupReceipt(
            BACKUP_PATH,
            hashlib.sha256(plan.original_image).hexdigest(),
            1024,
            Path("before.bin.journal.jsonl"),
        )
        self.errors: dict[str, BaseException] = {}
        self.failure: Optional[tuple[MagicWriteProgress, BaseException]] = None
        self.close_count = 0
        self.after_verified: Optional[Callable[[], None]] = None

    def _event(self, name: str) -> None:
        self.card.events.append("journal:" + name)
        if name in self.errors:
            raise self.errors[name]

    def validate_backup(self, plan: MagicWritePlan) -> None:
        assert plan is self.plan
        self._event("validate")

    def attempt(self, block: int) -> None:
        self._event(f"attempt:{block}")

    def verified(self, block: int) -> None:
        self._event(f"verified:{block}")
        if self.after_verified is not None:
            self.after_verified()

    def finish(self) -> None:
        self._event("finish")

    def fail(self, progress: MagicWriteProgress, error: BaseException) -> None:
        self.failure = progress, error
        self._event("fail")

    def close(self) -> None:
        self.close_count += 1
        self._event("close")


class MagicClassicWriteTest(unittest.TestCase):
    def prepared(
        self, block: int = 1, data: bytes = b"\xa5" * 16
    ) -> tuple[MemoryCard, MagicClassicTransaction, MagicWritePlan, JournalStub]:
        card = MemoryCard()
        transaction = MagicClassicTransaction(card)
        plan = transaction.plan_block(block, data)
        return card, transaction, plan, JournalStub(card, plan)

    @staticmethod
    def factory(card: MemoryCard, journal: JournalStub) -> Callable[..., JournalStub]:
        def create(plan: MagicWritePlan, backup_path: Path) -> JournalStub:
            assert plan is journal.plan
            assert backup_path == BACKUP_PATH
            card.events.append("journal:create")
            return journal

        return create

    def test_allowed_set_is_exactly_the_47_ordinary_blocks(self):
        self.assertIsInstance(WRITABLE_BLOCKS, tuple)
        self.assertEqual(len(WRITABLE_BLOCKS), 47)
        self.assertEqual(
            WRITABLE_BLOCKS,
            tuple(block for block in range(64) if block and block % 4 != 3),
        )

    def test_block_and_data_errors_are_rejected_before_any_rf(self):
        invalid: tuple[Any, ...] = (0, -1, 64, True, False, "1", 1.0, *range(3, 64, 4))
        for block in invalid:
            card = MemoryCard()
            with self.subTest(block=block), self.assertRaises(MagicCardReadError):
                MagicClassicTransaction(card).plan_block(block, b"\x00" * 16)
            self.assertEqual(card.events, [])
        invalid_data: tuple[Any, ...] = (
            b"",
            b"\x00" * 15,
            b"\x00" * 17,
            bytearray(16),
            "00" * 16,
        )
        for data in invalid_data:
            card = MemoryCard()
            with self.subTest(data=data), self.assertRaises(MagicCardReadError):
                MagicClassicTransaction(card).plan_block(1, data)
            self.assertEqual(card.events, [])

    def test_source_shape_and_bcc_are_rejected_before_any_rf(self):
        invalid_sources: tuple[Any, ...] = (
            b"",
            IMAGE[:-1],
            IMAGE + b"\x00",
            bytearray(IMAGE),
            IMAGE[:4] + b"\x00" + IMAGE[5:],
        )
        for source in invalid_sources:
            card = MemoryCard()
            with (
                self.subTest(length=len(source)),
                self.assertRaises(MagicCardReadError),
            ):
                MagicClassicTransaction(card).plan_restore(source)
            self.assertEqual(card.events, [])

    def test_restore_refuses_the_whole_plan_for_every_protected_difference(self):
        for block in (0, *range(3, 64, 4)):
            card = MemoryCard()
            desired = bytearray(IMAGE)
            desired[16] ^= 1  # Also include an otherwise legal ordinary-data change.
            desired[block * 16 + 8] ^= 1
            with (
                self.subTest(block=block),
                self.assertRaisesRegex(MagicCardReadError, "protected"),
            ):
                MagicClassicTransaction(card).plan_restore(bytes(desired))
            self.assertEqual(card.attempts, [])

    def test_planning_is_read_only_and_returns_bound_frozen_changes(self):
        card, transaction, plan, _ = self.prepared()
        self.assertEqual(plan.identity, IDENTITY)
        self.assertEqual(
            plan.changes, (MagicBlockChange(1, b"\x01" * 16, b"\xa5" * 16),)
        )
        self.assertEqual(card.attempts, [])
        self.assertEqual(card.events.count("scan"), 2)
        self.assertEqual(transaction.progress, MagicWriteProgress())
        self.assertNotIn(IMAGE.hex(), repr(plan))
        self.assertNotIn("original_image", repr(plan))
        self.assertNotIn("before=", repr(plan.changes[0]))
        for value, name in (
            (plan, "changes"),
            (plan.changes[0], "before"),
            (transaction.progress, "failed_stage"),
        ):
            with self.subTest(name=name), self.assertRaises(FrozenInstanceError):
                setattr(value, name, None)

    def test_restore_plan_includes_all_47_ordinary_differences_in_order(self):
        desired = bytearray(IMAGE)
        for block in WRITABLE_BLOCKS:
            desired[block * 16 : (block + 1) * 16] = b"\xaa" * 16
        card = MemoryCard()
        plan = MagicClassicTransaction(card).plan_restore(bytes(desired))
        self.assertEqual(
            tuple(change.block for change in plan.changes), WRITABLE_BLOCKS
        )
        self.assertEqual(card.attempts, [])

    def test_apply_validates_forged_plan_without_rf_or_creating_backup(self):
        valid = MagicWritePlan(
            IDENTITY,
            IMAGE,
            IMAGE[:16] + b"\xa5" * 16 + IMAGE[32:],
            (MagicBlockChange(1, b"\x01" * 16, b"\xa5" * 16),),
        )
        bad_change = MagicBlockChange(True, b"\x01" * 16, b"\xa5" * 16)
        forged: tuple[Any, ...] = (
            None,
            replace(valid, changes=()),
            replace(valid, changes=[valid.changes[0]]),
            replace(valid, changes=(bad_change,)),
            replace(valid, changes=(valid.changes[0], valid.changes[0])),
            replace(valid, original_image=IMAGE[:-1]),
            replace(valid, identity=replace(IDENTITY, uid=b"1234567")),
            replace(valid, desired_image=IMAGE[:48] + b"\xff" * 16 + IMAGE[64:]),
        )
        for plan in forged:
            card = MemoryCard()
            with patch("magic_classic_write.WriteJournal.create") as create:
                with self.subTest(plan=plan), self.assertRaises(MagicCardWriteError):
                    MagicClassicTransaction(card).apply(plan, backup_path=BACKUP_PATH)
                create.assert_not_called()
            self.assertEqual(card.events, [])

    def test_success_has_durable_pending_before_a0_and_no_io_before_data(self):
        card, transaction, plan, journal = self.prepared()
        with patch(
            "magic_classic_write.WriteJournal.create",
            side_effect=self.factory(card, journal),
        ):
            result = transaction.apply(plan, backup_path=BACKUP_PATH)
        self.assertEqual(bytes(card.image), plan.desired_image)
        self.assertEqual(result.progress, MagicWriteProgress((1,), (1,), (1,)))
        self.assertEqual(transaction.progress, result.progress)
        self.assertEqual(transaction.backup, journal.backup)
        self.assertEqual(result.backup, journal.backup)
        self.assertLess(
            card.events.index("journal:validate"),
            card.events.index("journal:attempt:1"),
        )
        self.assertLess(
            card.events.index("journal:attempt:1"), card.events.index("a0:1")
        )
        index = card.events.index("a0:1")
        self.assertEqual(card.events[index + 1], "data:1")
        verification = card.events[index + 2 : card.events.index("journal:verified:1")]
        self.assertEqual(verification.count("scan"), 1)
        self.assertEqual(verification.count("wake40"), 1)
        self.assertEqual(verification.count("wake43"), 1)
        self.assertIn("read:1", verification)
        self.assertEqual(journal.close_count, 1)
        self.assertIsNone(journal.failure)
        self.assertEqual(card.events[-2:], ["journal:finish", "journal:close"])

    def test_noop_apply_rechecks_the_card_without_creating_files(self):
        card, transaction, plan, _ = self.prepared(data=b"\x01" * 16)
        self.assertEqual(plan.changes, ())
        with patch("magic_classic_write.WriteJournal.create") as create:
            result = transaction.apply(plan, backup_path=BACKUP_PATH)
            create.assert_not_called()
        self.assertEqual(result.progress, MagicWriteProgress())
        self.assertIsNone(result.backup)
        self.assertEqual(card.events.count("scan"), 4)
        self.assertEqual(card.attempts, [])

    def test_complete_transaction_integrates_with_real_before_image_and_journal(self):
        card, transaction, plan, _ = self.prepared()
        with tempfile.TemporaryDirectory() as directory:
            backup_path = Path(directory) / "original.bin"
            journal_path = backup_path.with_name(backup_path.name + ".journal.jsonl")

            def hook(event: str, response: Response) -> Response:
                if event == "a0:1":
                    # Inspect the durable files before delivering A0's ACK to
                    # the coordinator, not in its ACK-to-data interval.
                    self.assertEqual(backup_path.read_bytes(), IMAGE)
                    records = [
                        json.loads(line)
                        for line in journal_path.read_text().splitlines()
                    ]
                    self.assertEqual(records[-1]["state"], "pending")
                    self.assertEqual(records[-1]["block"], 1)
                return response

            card.raw_hook = hook
            result = transaction.apply(plan, backup_path=backup_path)
            self.assertIsNotNone(result.backup)
            self.assertEqual(backup_path.read_bytes(), IMAGE)
            records = [
                json.loads(line) for line in journal_path.read_text().splitlines()
            ]
            self.assertEqual(
                [record["state"] for record in records],
                ["prepared", "pending", "verified", "complete"],
            )
            self.assertEqual(bytes(card.image), plan.desired_image)

    def test_plan_expiry_or_changed_identity_refuses_apply_before_backup(self):
        for change_identity in (False, True):
            card, transaction, plan, _ = self.prepared()
            if change_identity:
                card.identity = replace(IDENTITY, atqa=b"\x08\x00")
            else:
                card.image[32] ^= 1
            with patch("magic_classic_write.WriteJournal.create") as create:
                with (
                    self.subTest(identity=change_identity),
                    self.assertRaises(MagicCardWriteError) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
                create.assert_not_called()
            self.assertEqual(caught.exception.progress.failed_stage, "apply preflight")
            self.assertEqual(card.attempts, [])

    def test_backup_create_and_validation_failure_make_no_programming_attempt(self):
        for stage in ("create", "validate"):
            card, transaction, plan, journal = self.prepared()
            if stage == "create":
                factory: Any = OSError("backup not durable")
            else:
                factory = self.factory(card, journal)
                journal.errors["validate"] = OSError("backup readback mismatch")
            with patch("magic_classic_write.WriteJournal.create", side_effect=factory):
                with (
                    self.subTest(stage=stage),
                    self.assertRaises(MagicCardWriteError) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertEqual(card.attempts, [])
            self.assertEqual(caught.exception.progress.attempted_blocks, ())
            self.assertEqual(caught.exception.progress.possible_written_blocks, ())

    def test_pending_sync_failure_prevents_a0_and_preserves_receipt(self):
        card, transaction, plan, journal = self.prepared()
        journal.errors["attempt:1"] = OSError("pending fsync failed")
        with patch(
            "magic_classic_write.WriteJournal.create",
            side_effect=self.factory(card, journal),
        ):
            with self.assertRaises(MagicCardWriteError) as caught:
                transaction.apply(plan, backup_path=BACKUP_PATH)
        self.assertEqual(card.attempts, [])
        self.assertEqual(
            caught.exception.progress,
            MagicWriteProgress((), (), (), "record pending", 1),
        )
        self.assertEqual(caught.exception.backup, journal.backup)

    def test_prewrite_old_block_and_block0_are_rechecked_after_backup(self):
        for block in (0, 1):
            card, transaction, plan, journal = self.prepared()

            def hook(event: str, response: Response) -> Response:
                if event == f"read:{block}" and "journal:create" in card.events:
                    response.data = response.data[:8] + b"\xfe" + response.data[9:]
                return response

            card.raw_hook = hook
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with self.subTest(block=block), self.assertRaises(MagicCardWriteError):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertEqual(card.attempts, [])

    def test_both_write_phases_require_success_status_and_exact_ack(self):
        bad = (
            (Status.HF_ERR_CRC, b"\x0a"),
            (Status.HF_COLLISION, b"\x0a"),
            (Status.HF_TAG_NO, b""),
            (Status.SUCCESS, b"\x0a"),
            (Status.HF_TAG_OK, b"\x1a"),
            (Status.HF_TAG_OK, b"\x0a\x00"),
            (Status.HF_TAG_OK, b""),
        )
        for phase in ("a0", "data"):
            for status, data in bad:
                card, transaction, plan, journal = self.prepared()

                def hook(event: str, response: Response) -> Response:
                    if event == f"{phase}:1":
                        return Response(Command.HF14A_RAW, status, data)
                    return response

                card.raw_hook = hook
                with patch(
                    "magic_classic_write.WriteJournal.create",
                    side_effect=self.factory(card, journal),
                ):
                    with (
                        self.subTest(phase=phase, status=status, data=data),
                        self.assertRaises(MagicCardWriteError) as caught,
                    ):
                        transaction.apply(plan, backup_path=BACKUP_PATH)
                self.assertEqual(caught.exception.progress.attempted_blocks, (1,))
                self.assertEqual(
                    caught.exception.progress.possible_written_blocks,
                    (1,) if phase == "data" else (),
                )
                self.assertEqual(caught.exception.progress.verified_blocks, ())
                self.assertEqual(card.programs, ["data:1"] if phase == "data" else [])

    def test_timeout_or_interrupt_after_data_stays_uncertain_and_is_never_retried(self):
        for error_type in (TimeoutError, KeyboardInterrupt):
            card, transaction, plan, journal = self.prepared()

            def hook(event: str, response: Response) -> Response:
                if event == "data:1":
                    raise error_type("lost data ACK")
                return response

            card.raw_hook = hook
            expected: type[BaseException] = (
                KeyboardInterrupt
                if error_type is KeyboardInterrupt
                else MagicCardWriteError
            )
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with (
                    self.subTest(error=error_type),
                    self.assertRaises(expected) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
                frames_before = len(card.frames)
                with self.assertRaises(MagicCardWriteError):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
                self.assertEqual(len(card.frames), frames_before)
            progress = getattr(caught.exception, "progress")
            self.assertEqual(
                progress, MagicWriteProgress((1,), (1,), (), "write data", 1)
            )
            self.assertEqual(getattr(caught.exception, "backup"), journal.backup)
            self.assertEqual(transaction.progress, progress)
            self.assertEqual(card.attempts, ["a0:1"])
            self.assertEqual(card.programs, ["data:1"])
            self.assertEqual(bytes(card.image), plan.desired_image)

    def test_failed_second_block_stops_without_rollback_or_remaining_writes(self):
        card = MemoryCard()
        transaction = MagicClassicTransaction(card)
        desired = IMAGE[:16] + b"\xa5" * 32 + IMAGE[48:64] + b"\xa5" * 16 + IMAGE[80:]
        plan = transaction.plan_restore(desired)
        journal = JournalStub(card, plan)

        def hook(event: str, response: Response) -> Response:
            if event == "data:2":
                raise TimeoutError("unknown completion")
            return response

        card.raw_hook = hook
        with patch(
            "magic_classic_write.WriteJournal.create",
            side_effect=self.factory(card, journal),
        ):
            with self.assertRaises(MagicCardWriteError) as caught:
                transaction.apply(plan, backup_path=BACKUP_PATH)
        self.assertEqual(
            caught.exception.progress,
            MagicWriteProgress((1, 2), (1, 2), (1,), "write data", 2),
        )
        self.assertEqual(card.attempts, ["a0:1", "a0:2"])
        self.assertEqual(card.programs, ["data:1", "data:2"])

    def test_independent_readback_crc_or_mismatch_does_not_verify_the_block(self):
        for corrupt_status in (False, True):
            card, transaction, plan, journal = self.prepared()

            def hook(event: str, response: Response) -> Response:
                if event == "read:1" and card.programs:
                    if corrupt_status:
                        response.status = Status.HF_ERR_CRC
                    else:
                        response.data = b"\x00" * 16
                return response

            card.raw_hook = hook
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with (
                    self.subTest(status=corrupt_status),
                    self.assertRaises(MagicCardWriteError) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertEqual(
                caught.exception.progress,
                MagicWriteProgress((1,), (1,), (), "verify block", 1),
            )
            self.assertNotIn("journal:verified:1", card.events)
            self.assertEqual(card.attempts, ["a0:1"])

    def test_final_image_or_protected_data_change_is_not_a_success(self):
        for block in (0, 2, 3):
            card, transaction, plan, journal = self.prepared()

            def mutate() -> None:
                card.image[block * 16 + 8] ^= 1

            journal.after_verified = mutate
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with (
                    self.subTest(block=block),
                    self.assertRaises(MagicCardWriteError) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertEqual(
                caught.exception.progress,
                MagicWriteProgress((1,), (1,), (1,), "final verification", None),
            )
            self.assertNotIn("journal:finish", card.events)
            self.assertEqual(card.attempts, ["a0:1"])

    def test_verified_record_finish_and_close_errors_retain_known_programming_progress(
        self,
    ):
        for method in ("verified:1", "finish", "close"):
            card, transaction, plan, journal = self.prepared()
            journal.errors[method] = OSError("journal unavailable")
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with (
                    self.subTest(method=method),
                    self.assertRaises(MagicCardWriteError) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertEqual(caught.exception.progress.verified_blocks, (1,))
            self.assertEqual(caught.exception.progress.possible_written_blocks, (1,))
            self.assertEqual(journal.close_count, 1)
            self.assertEqual(card.attempts, ["a0:1"])

    def test_failure_log_and_close_errors_do_not_replace_primary_cause_or_cancel(self):
        for original in (TimeoutError, KeyboardInterrupt):
            card, transaction, plan, journal = self.prepared()
            journal.errors["fail"] = OSError("failed journal")
            journal.errors["close"] = OSError("close failed")

            def hook(event: str, response: Response) -> Response:
                if event == "data:1":
                    raise original("primary failure")
                return response

            card.raw_hook = hook
            expected: type[BaseException] = (
                KeyboardInterrupt
                if original is KeyboardInterrupt
                else MagicCardWriteError
            )
            with patch(
                "magic_classic_write.WriteJournal.create",
                side_effect=self.factory(card, journal),
            ):
                with (
                    self.subTest(original=original),
                    self.assertRaises(expected) as caught,
                ):
                    transaction.apply(plan, backup_path=BACKUP_PATH)
            self.assertIn("primary failure", str(caught.exception))
            self.assertIn("recording failure also failed", str(caught.exception))
            self.assertIn("closing journal also failed", str(caught.exception))
            self.assertEqual(len(getattr(caught.exception, "journal_errors")), 2)
            if original is TimeoutError:
                self.assertIsInstance(caught.exception.__cause__, MagicCardReadError)
                self.assertIsInstance(
                    caught.exception.__cause__.__cause__, TimeoutError
                )
            self.assertEqual(
                getattr(caught.exception, "progress").possible_written_blocks, (1,)
            )


if __name__ == "__main__":
    TestProgram()
