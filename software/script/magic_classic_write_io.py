"""Private before-images and write-ahead records for ordinary-block writes."""

from __future__ import annotations

import hashlib
import json
import os
import re
import uuid
from dataclasses import asdict, dataclass
from functools import reduce
from operator import xor
from pathlib import Path
from typing import TYPE_CHECKING, BinaryIO, Optional

from magic_classic import MagicCardReadError
from magic_classic_io import publish_backup

if TYPE_CHECKING:
    from magic_classic_write import MagicWritePlan, MagicWriteProgress

IMAGE_SIZE = 1024
SOURCE_LIMIT = 65536


def _hex_bytes(value: object, length: int, name: str) -> bytes:
    if (
        not isinstance(value, str)
        or re.fullmatch(rf"[0-9a-fA-F]{{{length * 2}}}", value) is None
    ):
        raise MagicCardReadError(f"invalid {name} in restore image")
    return bytes.fromhex(value)


def _recorded_before_digest(source: Path, image: bytes) -> Optional[bytes]:
    record_path = journal_path_for(source)
    if not record_path.exists() and not record_path.is_symlink():
        return None
    with record_path.open("rb") as stream:
        first = stream.readline(SOURCE_LIMIT + 1)
    if len(first) > SOURCE_LIMIT or not first.endswith(b"\n"):
        raise MagicCardReadError("before-image journal has no complete prepared record")
    try:
        record = json.loads(first)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise MagicCardReadError(
            "invalid before-image journal prepared record"
        ) from error
    if not isinstance(record, dict) or (
        record.get("format") != "chameleon-classic-data-write-journal-v1"
        or record.get("profile") != "gen1a-compatible-classic1k-ordinary-data"
        or record.get("state") != "prepared"
        or type(record.get("sequence")) is not int
        or record["sequence"] != 0
    ):
        raise MagicCardReadError("unsupported before-image journal prepared record")
    if _hex_bytes(record.get("uid"), 4, "journal UID") != image[:4]:
        raise MagicCardReadError("before-image journal UID differs from its image")
    return _hex_bytes(record.get("before_sha256"), 32, "journal before-image SHA-256")


def load_restore_image(
    source: Path,
    input_format: Optional[str] = None,
    expected_sha256: Optional[str] = None,
) -> bytes:
    """Validate a complete source before the CLI starts any RF operation."""
    selected = input_format or ("json" if source.suffix.lower() == ".json" else "bin")
    if selected not in ("bin", "json"):
        raise MagicCardReadError("restore format must be bin or json")
    with source.open("rb") as stream:
        content = stream.read(SOURCE_LIMIT + 1)
    if len(content) > SOURCE_LIMIT:
        raise MagicCardReadError("restore source exceeds the supported size")
    if selected == "bin":
        image = content
    else:
        try:
            document = json.loads(content)
        except (ValueError, UnicodeError, RecursionError) as error:
            raise MagicCardReadError("invalid restore JSON") from error
        if not isinstance(document, dict):
            raise MagicCardReadError("restore JSON must be an object")
        if (
            document.get("format") != "chameleon-gen1a-classic1k-v1"
            or document.get("read_profile") != "gen1a-compatible-classic1k"
        ):
            raise MagicCardReadError("unsupported restore JSON profile")
        for field, expected in (("block_size", 16), ("block_count", 64)):
            value = document.get(field)
            if type(value) is not int or value != expected:
                raise MagicCardReadError(f"invalid {field} in restore JSON")
        reads = document.get("independent_reads")
        if type(reads) is not int or reads < 2:
            raise MagicCardReadError("restore JSON lacks independent-read metadata")
        if type(document.get("key_material_verified")) is not bool:
            raise MagicCardReadError("invalid key verification metadata")
        blocks = document.get("blocks")
        if not isinstance(blocks, list) or len(blocks) != 64:
            raise MagicCardReadError("restore JSON must contain exactly 64 blocks")
        image = b"".join(_hex_bytes(block, 16, "block") for block in blocks)
        uid = _hex_bytes(document.get("uid"), 4, "UID")
        _hex_bytes(document.get("atqa_wire_order"), 2, "ATQA")
        if _hex_bytes(document.get("sak"), 1, "SAK") != b"\x08":
            raise MagicCardReadError("restore JSON does not use the SAK 08 profile")
        if image[:4] != uid:
            raise MagicCardReadError("restore JSON UID differs from Block 0")
        digest = _hex_bytes(document.get("sha256"), 32, "SHA-256")
        if digest != hashlib.sha256(image).digest():
            raise MagicCardReadError("restore JSON image SHA-256 does not match")
    if len(image) != IMAGE_SIZE:
        raise MagicCardReadError("restore image must contain exactly 1024 bytes")
    if image[4] != reduce(xor, image[:4]):
        raise MagicCardReadError("restore image Block 0 BCC is invalid")
    expected = []
    if expected_sha256 is not None:
        expected.append(_hex_bytes(expected_sha256, 32, "expected image SHA-256"))
    if selected == "bin":
        # A BIN has no embedded checksum. Use the prepared record saved before
        # programming, or an explicitly supplied digest from a trusted source.
        # A later torn/pending log entry does not invalidate that first record.
        recorded = _recorded_before_digest(source, image)
        if recorded is not None:
            expected.append(recorded)
        if not expected:
            raise MagicCardReadError(
                "BIN restore requires its adjacent write journal or --sha256"
            )
    if any(digest != hashlib.sha256(image).digest() for digest in expected):
        raise MagicCardReadError("restore image does not match the expected SHA-256")
    return image


def journal_path_for(backup_path: Path) -> Path:
    return backup_path.with_name(backup_path.name + ".journal.jsonl")


def check_backup_paths(backup_path: Path) -> None:
    for target in (backup_path, journal_path_for(backup_path)):
        if target.exists() or target.is_symlink():
            raise MagicCardReadError(f"refusing to replace transaction file: {target}")
        if not target.parent.is_dir():
            raise MagicCardReadError(
                f"transaction directory does not exist: {target.parent}"
            )


@dataclass(frozen=True)
class MagicBackupReceipt:
    path: Path
    image_sha256: str
    size: int
    journal_path: Path


class WriteJournal:
    """Own one exclusive append-only journal until a transaction stops.

    A synced pending line precedes A0, not the gap between A0's ACK and the
    data frame. After a crash, an unresolved pending entry is conservatively
    uncertain. This log is diagnostic; it never authorizes automatic resume.
    """

    def __init__(
        self, plan: MagicWritePlan, backup: MagicBackupReceipt, stream: BinaryIO
    ):
        self.backup = backup
        self._stream = stream
        self._original = plan.original_image
        self._target_sha256 = hashlib.sha256(plan.desired_image).hexdigest()
        self._identity = plan.identity
        self._planned = tuple(change.block for change in plan.changes)
        self._verified: list[int] = []
        self._pending: Optional[int] = None
        self._sequence = 0
        self._operation_id = uuid.uuid4().hex
        self._healthy = True
        self._closed = False

    @classmethod
    def create(cls, plan: MagicWritePlan, backup_path: Path) -> WriteJournal:
        check_backup_paths(backup_path)
        original = plan.original_image
        if not isinstance(original, bytes) or len(original) != IMAGE_SIZE:
            raise MagicCardReadError("cannot back up an incomplete original image")
        target = backup_path.absolute()
        journal_path = journal_path_for(target)
        digest = hashlib.sha256(original).hexdigest()
        # Publication checks short writes/flush/fsync/close. Reopening the
        # published name is a separate gate before any RF programming command.
        publish_backup(target, original)
        with target.open("rb") as source:
            saved = source.read(IMAGE_SIZE + 1)
        if saved != original:
            raise MagicCardReadError("published before-image did not read back exactly")
        receipt = MagicBackupReceipt(target, digest, IMAGE_SIZE, journal_path)
        try:
            stream = journal_path.open("xb")
        except BaseException as error:
            error.__dict__["backup"] = receipt
            raise
        journal = cls(plan, receipt, stream)
        try:
            journal._append(
                "prepared",
                {
                    "backup_path": str(target),
                    "before_sha256": digest,
                    "desired_sha256": journal._target_sha256,
                    "profile": "gen1a-compatible-classic1k-ordinary-data",
                    "uid": plan.identity.uid.hex().upper(),
                    "atqa_wire_order": plan.identity.atqa.hex().upper(),
                    "sak": plan.identity.sak.hex().upper(),
                    "planned_blocks": list(journal._planned),
                },
            )
            journal.validate_backup(plan)
        except BaseException as error:
            error.__dict__["backup"] = receipt
            try:
                journal.close()
            except BaseException as cleanup:
                error.__dict__["journal_close_error"] = cleanup
            raise
        return journal

    def _append(self, state: str, details: dict) -> None:
        if self._closed or not self._healthy:
            raise OSError("transaction journal is not writable")
        # Retain the open file's ownership even if a pathname is replaced.
        # Refuse to program after losing the named, user-visible journal.
        if not os.path.samestat(
            os.fstat(self._stream.fileno()), self.backup.journal_path.stat()
        ):
            raise OSError("transaction journal pathname changed")
        document = {
            "format": "chameleon-classic-data-write-journal-v1",
            "operation_id": self._operation_id,
            "sequence": self._sequence,
            "state": state,
            **details,
        }
        encoded = (json.dumps(document, sort_keys=True) + "\n").encode("utf-8")
        try:
            if self._stream.write(encoded) != len(encoded):
                raise OSError("incomplete transaction journal write")
            self._stream.flush()
            os.fsync(self._stream.fileno())
        except BaseException:
            # Appending another JSON object after a torn line would obscure
            # the last complete durable record. Stop instead of repairing it.
            self._healthy = False
            raise
        self._sequence += 1

    def validate_backup(self, plan: MagicWritePlan) -> None:
        if (
            plan.original_image != self._original
            or plan.identity != self._identity
            or hashlib.sha256(plan.desired_image).hexdigest() != self._target_sha256
            or tuple(change.block for change in plan.changes) != self._planned
        ):
            raise MagicCardReadError("before-image receipt belongs to another plan")
        self._check_saved_image()

    def _check_saved_image(self) -> None:
        with self.backup.path.open("rb") as source:
            saved = source.read(IMAGE_SIZE + 1)
        if (
            saved != self._original
            or hashlib.sha256(saved).hexdigest() != self.backup.image_sha256
        ):
            raise MagicCardReadError("persisted before-image changed or is incomplete")

    def attempt(self, block: int) -> None:
        if (
            type(block) is not int
            or block not in self._planned
            or self._pending is not None
            or block in self._verified
        ):
            raise MagicCardReadError("invalid or duplicate journal write attempt")
        self._check_saved_image()
        self._append(
            "pending",
            {
                "block": block,
                "recovery": "A0/data may follow; unresolved pending is uncertain",
            },
        )
        self._pending = block

    def verified(self, block: int) -> None:
        if self._pending != block:
            raise MagicCardReadError("journal verification has no pending block")
        self._append("verified", {"block": block})
        self._verified.append(block)
        self._pending = None

    def finish(self) -> None:
        if self._pending is not None or tuple(self._verified) != self._planned:
            raise MagicCardReadError("cannot finish an incomplete write journal")
        self._append("complete", {"verified_blocks": self._verified})

    def fail(self, progress: MagicWriteProgress, error: BaseException) -> None:
        self._append(
            "failed",
            {"progress": asdict(progress), "error": str(error)},
        )

    def close(self) -> None:
        if not self._closed:
            self._closed = True
            self._stream.close()
