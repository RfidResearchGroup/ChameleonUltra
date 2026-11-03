"""Exercise the production reader using an independent command transcript."""

from __future__ import annotations

import copy
import unittest
from dataclasses import FrozenInstanceError
from typing import Any, Optional
from unittest import TestProgram

from chameleon_com import Response
from chameleon_enum import Command, Status
from magic_classic import (
    MagicCardDump,
    MagicCardIdentity,
    MagicCardInfo,
    MagicCardReadError,
    MagicClassicReader,
)

UID = bytes.fromhex("11223344")
BLOCK0 = UID + b"\x44\x08\x04\x00" + bytes(range(8))
IDENTITY = {"uid": UID, "atqa": b"\x04\x00", "sak": b"\x08"}
IMAGE = BLOCK0 + b"".join(bytes([block]) * 16 for block in range(1, 64))

OFF = {
    "activate_rf_field": 0,
    "wait_response": 0,
    "append_crc": 0,
    "auto_select": 0,
    "keep_rf_field": 0,
    "check_response_crc": 0,
}
HALT = {
    "activate_rf_field": 1,
    "wait_response": 0,
    "append_crc": 1,
    "auto_select": 1,
    "keep_rf_field": 1,
    "check_response_crc": 0,
}
WAKE = {
    "activate_rf_field": 0,
    "wait_response": 1,
    "append_crc": 0,
    "auto_select": 0,
    "keep_rf_field": 1,
    "check_response_crc": 0,
}
READ = {
    "activate_rf_field": 0,
    "wait_response": 1,
    "append_crc": 1,
    "auto_select": 0,
    "keep_rf_field": 1,
    "check_response_crc": 1,
}


def raw(
    data: bytes, options: dict[str, int], reply: bytes = b"", bits: Optional[int] = None
) -> dict[str, Any]:
    return {
        "kind": "raw",
        "data": data,
        "options": options,
        "bits": bits,
        "reply": Response(Command.HF14A_RAW, Status.HF_TAG_OK, reply),
    }


def transcript(
    image: bytes = IMAGE,
    *,
    blocks: int = 64,
    identity: Optional[dict[str, bytes]] = None,
) -> list[dict[str, Any]]:
    events = [
        raw(b"", OFF),
        {"kind": "scan", "reply": [dict(IDENTITY if identity is None else identity)]},
        raw(b"\x50\x00", HALT),
        raw(b"\x40", WAKE, b"\x0a", 7),
        raw(b"\x43", WAKE, b"\x0a"),
    ]
    for block in range(blocks):
        events.append(
            raw(bytes((0x30, block)), READ, image[block * 16 : (block + 1) * 16])
        )
    events.extend((raw(b"\x30\x00", READ, image[:16]), raw(b"", OFF)))
    return events


class TranscriptCommand:
    def __init__(self, events: list[dict[str, Any]]):
        self.events = copy.deepcopy(events)
        self.calls: list[dict[str, Any]] = []

    def _next(self, kind: str) -> dict[str, Any]:
        if not self.events:
            raise AssertionError(f"unexpected {kind} after transcript ended")
        event = self.events.pop(0)
        if event["kind"] != kind:
            raise AssertionError(f"expected {event['kind']}, got {kind}")
        return event

    def hf14a_scan(self) -> list[dict[str, bytes]]:
        event = self._next("scan")
        self.calls.append({"kind": "scan"})
        reply = event["reply"]
        if isinstance(reply, BaseException):
            raise reply
        return reply

    def hf14a_raw_response(
        self,
        options: dict[str, int],
        resp_timeout_ms: int = 100,
        data: bytes = b"",
        bitlen: Optional[int] = None,
    ) -> Response:
        event = self._next("raw")
        call = {
            "kind": "raw",
            "data": data,
            "options": dict(options),
            "bits": bitlen,
            "timeout": resp_timeout_ms,
        }
        self.calls.append(call)
        assert call["data"] == event["data"], (call, event)
        assert call["options"] == event["options"], (call, event)
        assert call["bits"] == event["bits"], (call, event)
        assert resp_timeout_ms == 100
        reply = event["reply"]
        if isinstance(reply, BaseException):
            raise reply
        return reply


class MagicClassicReaderTest(unittest.TestCase):
    def assert_failed_transcript(
        self,
        events: list[dict[str, Any]],
        error: type[BaseException] = MagicCardReadError,
    ) -> BaseException:
        command = TranscriptCommand(events)
        with self.assertRaises(error) as context:
            MagicClassicReader(command).dump()
        self.assertEqual(command.events, [])
        self.assertEqual(command.calls[-1]["data"], b"")
        self.assertEqual(command.calls[-1]["options"], OFF)
        return context.exception

    def test_complete_dump_is_two_independent_read_only_passes(self):
        command = TranscriptCommand(transcript() + transcript())
        result = MagicClassicReader(command).dump()
        self.assertEqual(result, MagicCardDump(MagicCardIdentity(**IDENTITY), IMAGE, 2))
        self.assertEqual(command.events, [])
        self.assertEqual(sum(call["kind"] == "scan" for call in command.calls), 2)
        commands = [call["data"] for call in command.calls if call["kind"] == "raw"]
        allowed = {b"", b"\x50\x00", b"\x40", b"\x43"}
        allowed.update(bytes((0x30, block)) for block in range(64))
        self.assertLessEqual(set(commands), allowed)
        self.assertEqual(commands.count(b"\x30\x00"), 4)
        self.assertEqual(commands.count(b""), 4)

    def test_info_has_the_same_independent_and_final_block0_checks(self):
        command = TranscriptCommand(transcript(blocks=1) + transcript(blocks=1))
        result = MagicClassicReader(command).read_info()
        self.assertEqual(result, MagicCardInfo(MagicCardIdentity(**IDENTITY), BLOCK0))
        self.assertEqual(command.events, [])
        reads = [
            call["data"]
            for call in command.calls
            if call["kind"] == "raw" and call["data"].startswith(b"\x30")
        ]
        self.assertEqual(reads, [b"\x30\x00"] * 4)

    def test_result_records_are_frozen(self):
        identity = MagicCardIdentity(**IDENTITY)
        for result, field in (
            (identity, "uid"),
            (MagicCardInfo(identity, BLOCK0), "block0"),
            (MagicCardDump(identity, IMAGE), "image"),
        ):
            with self.subTest(field=field), self.assertRaises(FrozenInstanceError):
                setattr(result, field, b"changed")

    def test_scan_rejects_absent_multiple_unsupported_or_malformed_tags(self):
        unsupported: tuple[Any, ...] = (
            [],
            None,
            [IDENTITY, IDENTITY],
            [{"uid": b"1234567", "atqa": b"\x44\x00", "sak": b"\x08"}],
            [{**IDENTITY, "sak": b"\x18"}],
            [{**IDENTITY, "sak": b"\x09"}],
            [{**IDENTITY, "atqa": b"\x04"}],
            [{**IDENTITY, "sak": b""}],
            [{**IDENTITY, "uid": bytearray(UID)}],
            [{"uid": UID}],
            [None],
        )
        for tags in unsupported:
            with self.subTest(tags=tags):
                events = [raw(b"", OFF), {"kind": "scan", "reply": tags}, raw(b"", OFF)]
                self.assert_failed_transcript(events)

    def test_scan_transport_failure_and_interrupt_both_clean_up(self):
        for failure in (TimeoutError("scan timeout"), KeyboardInterrupt("cancelled")):
            with self.subTest(failure=type(failure)):
                events = [
                    raw(b"", OFF),
                    {"kind": "scan", "reply": failure},
                    raw(b"", OFF),
                ]
                expected = (
                    KeyboardInterrupt
                    if isinstance(failure, KeyboardInterrupt)
                    else MagicCardReadError
                )
                error = self.assert_failed_transcript(events, expected)
                if isinstance(error, MagicCardReadError):
                    self.assertIsInstance(error.__cause__, TimeoutError)

    def test_every_raw_stage_rejects_bad_firmware_status_even_with_plausible_data(self):
        statuses = (
            Status.HF_ERR_STAT,
            Status.HF_ERR_CRC,
            Status.HF_COLLISION,
            Status.HF_ERR_PARITY,
            Status.HF_TAG_NO,
            Status.SUCCESS,
            0xFE,
            False,
        )
        # Initial field-off, select/HALT, each wake-up, and a complete data block.
        for index in (0, 2, 3, 4, 5):
            for status in statuses:
                with self.subTest(index=index, status=status):
                    events = transcript()[: index + 1]
                    events[-1]["reply"].status = status
                    events.append(raw(b"", OFF))
                    self.assert_failed_transcript(events)

    def test_every_raw_stage_rejects_transport_failures_and_interrupts(self):
        for index in (0, 2, 3, 4, 5):
            for exception in (TimeoutError, OSError, KeyboardInterrupt):
                with self.subTest(index=index, exception=exception):
                    events = transcript()[: index + 1]
                    events[-1]["reply"] = exception("injected")
                    events.append(raw(b"", OFF))
                    expected = (
                        KeyboardInterrupt
                        if exception is KeyboardInterrupt
                        else MagicCardReadError
                    )
                    self.assert_failed_transcript(events, expected)

    def test_exact_ack_is_required_at_both_wakeup_steps(self):
        for index in (3, 4):
            for reply in (b"", b"\x00", b"\x0b", b"\x1a", b"\xfa", b"\x0a\x00"):
                with self.subTest(index=index, reply=reply):
                    events = transcript()[: index + 1]
                    events[-1]["reply"].data = reply
                    events.append(raw(b"", OFF))
                    self.assert_failed_transcript(events)

    def test_control_and_read_response_lengths_are_exact(self):
        for index, replies in (
            (0, (b"\x00",)),
            (2, (b"\x00",)),
            (5, (b"", b"\x00" * 15, b"\x00" * 17, bytearray(BLOCK0))),
        ):
            for reply in replies:
                with self.subTest(index=index, reply=reply):
                    events = transcript()[: index + 1]
                    events[-1]["reply"].data = reply
                    events.append(raw(b"", OFF))
                    self.assert_failed_transcript(events)

    def test_block0_uid_and_bcc_must_match_before_a_dump_can_be_accepted(self):
        for block0 in (b"\xff" + BLOCK0[1:], BLOCK0[:4] + b"\x00" + BLOCK0[5:]):
            with self.subTest(block0=block0):
                changed = block0 + IMAGE[16:]
                events = transcript(changed)[:6] + [raw(b"", OFF)]
                self.assert_failed_transcript(events)

    def test_final_block0_change_is_rejected(self):
        changed = BLOCK0[:8] + b"\xff" + BLOCK0[9:]
        events = transcript()
        events[-2]["reply"].data = changed
        self.assert_failed_transcript(events)

    def test_final_block0_failure_is_rejected_without_partial_result(self):
        for status, data in (
            (Status.HF_ERR_CRC, BLOCK0),
            (Status.HF_TAG_NO, b""),
            (Status.HF_TAG_OK, BLOCK0[:-1]),
        ):
            with self.subTest(status=status, data=data):
                events = transcript()
                events[-2]["reply"] = Response(Command.HF14A_RAW, status, data)
                self.assert_failed_transcript(events)

    def test_changed_identity_between_passes_is_rejected(self):
        changed_uid = bytes.fromhex("55667788")
        changed_block0 = changed_uid + b"\xcc" + BLOCK0[5:]
        changed_identity = {**IDENTITY, "uid": changed_uid}
        command = TranscriptCommand(
            transcript()
            + transcript(changed_block0 + IMAGE[16:], identity=changed_identity)
        )
        with self.assertRaisesRegex(MagicCardReadError, "identity differs"):
            MagicClassicReader(command).dump()
        self.assertEqual(command.events, [])

    def test_changed_atqa_and_data_between_passes_are_rejected(self):
        for image, identity in (
            (IMAGE, {**IDENTITY, "atqa": b"\x08\x00"}),
            (IMAGE[:16] + b"\xfe" * 16 + IMAGE[32:], IDENTITY),
        ):
            with self.subTest(identity=identity, image=image[:32]):
                command = TranscriptCommand(
                    transcript() + transcript(image, identity=identity)
                )
                with self.assertRaises(MagicCardReadError):
                    MagicClassicReader(command).dump()
                self.assertEqual(command.events, [])

    def test_info_disagreement_is_rejected(self):
        changed = BLOCK0[:8] + b"\xff" + BLOCK0[9:]
        command = TranscriptCommand(
            transcript(blocks=1) + transcript(changed, blocks=1)
        )
        with self.assertRaisesRegex(MagicCardReadError, "data differs"):
            MagicClassicReader(command).read_info()
        self.assertEqual(command.events, [])

    def test_successful_read_with_failed_final_cleanup_returns_no_result(self):
        for reply in (
            Response(Command.HF14A_RAW, Status.HF_ERR_STAT),
            Response(Command.HF14A_RAW, Status.HF_TAG_OK, b"\x00"),
            TimeoutError("field-off timeout"),
        ):
            with self.subTest(reply=reply):
                events = transcript()
                events[-1]["reply"] = reply
                error = self.assert_failed_transcript(events)
                self.assertIn("cleanup", str(error))
                self.assertIsNotNone(error.__cause__)

    def test_cleanup_failure_preserves_primary_error_and_transport_cause(self):
        events = transcript()[:6]
        events[-1]["reply"] = TimeoutError("read timeout")
        cleanup_error = OSError("field-off unavailable")
        events.append(raw(b"", OFF))
        events[-1]["reply"] = cleanup_error
        error = self.assert_failed_transcript(events)
        self.assertIn("read block 0", str(error))
        self.assertIn("cleanup also failed", str(error))
        self.assertIsInstance(error.__cause__, TimeoutError)
        cleanup = getattr(error, "cleanup_error")
        self.assertIsInstance(cleanup, MagicCardReadError)
        self.assertIsInstance(cleanup.__cause__, OSError)

    def test_cleanup_failure_does_not_turn_keyboard_interrupt_into_another_error(self):
        events = transcript()[:6]
        events[-1]["reply"] = KeyboardInterrupt("cancelled")
        events.append(raw(b"", OFF))
        events[-1]["reply"] = TimeoutError("field-off timeout")
        error = self.assert_failed_transcript(events, KeyboardInterrupt)
        self.assertIn("cancelled", str(error))
        self.assertIn("cleanup also failed", str(error))
        self.assertIsInstance(getattr(error, "cleanup_error"), MagicCardReadError)

    def test_interrupt_during_final_cleanup_is_preserved(self):
        events = transcript()
        events[-1]["reply"] = KeyboardInterrupt("stop while closing")
        self.assert_failed_transcript(events, KeyboardInterrupt)


if __name__ == "__main__":
    TestProgram()
