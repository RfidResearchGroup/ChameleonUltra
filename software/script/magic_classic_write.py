"""One-shot, backed-up writes to ordinary Gen1A-compatible Classic 1K data.

The read-only reader remains a separate API. This coordinator is the only
place that emits A0 and never programs Block 0 or sector trailers. Recovery is
deliberate: a failed or interrupted transaction is not retried or rolled back.
"""

from __future__ import annotations

from contextlib import contextmanager
from dataclasses import dataclass, field
from functools import reduce
from operator import xor
from pathlib import Path
from typing import Iterator, Optional

from magic_classic import (
    MagicCardDump,
    MagicCardIdentity,
    MagicCardReadError,
    MagicClassicCommand,
    MagicClassicReader,
    _options,
)
from magic_classic_write_io import MagicBackupReceipt, WriteJournal

BLOCK_SIZE = 16
BLOCK_COUNT = 64
IMAGE_SIZE = BLOCK_SIZE * BLOCK_COUNT
WRITABLE_BLOCKS: tuple[int, ...] = tuple(
    block for block in range(1, BLOCK_COUNT) if block % 4 != 3
)


@dataclass(frozen=True)
class MagicBlockChange:
    block: int
    before: bytes = field(repr=False)
    after: bytes = field(repr=False)


@dataclass(frozen=True)
class MagicWritePlan:
    identity: MagicCardIdentity
    original_image: bytes = field(repr=False)
    desired_image: bytes = field(repr=False)
    changes: tuple[MagicBlockChange, ...]


@dataclass(frozen=True)
class MagicWriteProgress:
    attempted_blocks: tuple[int, ...] = ()
    possible_written_blocks: tuple[int, ...] = ()
    verified_blocks: tuple[int, ...] = ()
    failed_stage: Optional[str] = None
    failed_block: Optional[int] = None


@dataclass(frozen=True)
class MagicWriteResult:
    progress: MagicWriteProgress
    backup: Optional[MagicBackupReceipt]


class MagicCardWriteError(MagicCardReadError):
    def __init__(
        self,
        message: str,
        *,
        progress: MagicWriteProgress,
        backup: Optional[MagicBackupReceipt] = None,
    ):
        super().__init__(message)
        self.progress = progress
        self.backup = backup


def _check_block(block: int) -> None:
    if type(block) is not int or block not in WRITABLE_BLOCKS:
        raise MagicCardReadError(
            "only the 47 ordinary data blocks are writable; Block 0 and all "
            "sector trailers are protected"
        )


def _check_image(image: bytes, name: str) -> None:
    if not isinstance(image, bytes) or len(image) != IMAGE_SIZE:
        raise MagicCardReadError(f"{name} must contain exactly 1024 immutable bytes")
    if image[4] != reduce(xor, image[:4]):
        raise MagicCardReadError(f"{name} Block 0 BCC is invalid")


def _check_identity(identity: MagicCardIdentity) -> None:
    if (
        not isinstance(identity, MagicCardIdentity)
        or not isinstance(identity.uid, bytes)
        or len(identity.uid) != 4
        or not isinstance(identity.atqa, bytes)
        or len(identity.atqa) != 2
        or identity.sak != b"\x08"
        or not isinstance(identity.sak, bytes)
    ):
        raise MagicCardReadError("write plan requires a 4-byte UID, SAK 08 identity")


def _changes(original: bytes, desired: bytes) -> tuple[MagicBlockChange, ...]:
    changes = tuple(
        MagicBlockChange(
            block, original[offset : offset + 16], desired[offset : offset + 16]
        )
        for block in range(BLOCK_COUNT)
        for offset in (block * BLOCK_SIZE,)
        if original[offset : offset + 16] != desired[offset : offset + 16]
    )
    protected = tuple(
        change.block for change in changes if change.block == 0 or change.block % 4 == 3
    )
    if protected:
        raise MagicCardReadError(
            f"restore image differs in protected blocks {protected}; the whole "
            "plan is refused"
        )
    return changes


def _validate_plan(plan: MagicWritePlan) -> None:
    # A caller can construct frozen dataclasses directly. Validate the complete
    # binding here before apply performs any RF or creates transaction files.
    if not isinstance(plan, MagicWritePlan):
        raise MagicCardReadError("invalid write plan")
    _check_identity(plan.identity)
    _check_image(plan.original_image, "original image")
    _check_image(plan.desired_image, "desired image")
    if plan.original_image[:4] != plan.identity.uid:
        raise MagicCardReadError("write plan UID differs from its original Block 0")
    if not isinstance(plan.changes, tuple):
        raise MagicCardReadError("write plan changes must be an immutable tuple")
    for change in plan.changes:
        if not isinstance(change, MagicBlockChange):
            raise MagicCardReadError("invalid write plan change")
        _check_block(change.block)
        if (
            not isinstance(change.before, bytes)
            or not isinstance(change.after, bytes)
            or len(change.before) != BLOCK_SIZE
            or len(change.after) != BLOCK_SIZE
        ):
            raise MagicCardReadError("write plan blocks must contain exactly 16 bytes")
    if plan.changes != _changes(plan.original_image, plan.desired_image):
        raise MagicCardReadError("write plan changes do not match its bound images")


class MagicClassicTransaction:
    def __init__(self, cmd: MagicClassicCommand):
        self._reader = MagicClassicReader(cmd)
        self._used = False
        self._attempted: list[int] = []
        self._possible_written: list[int] = []
        self._verified: list[int] = []
        self._stage: Optional[str] = None
        self._block: Optional[int] = None
        self._progress = MagicWriteProgress()
        self._backup: Optional[MagicBackupReceipt] = None

    @property
    def progress(self) -> MagicWriteProgress:
        """The last immutable outcome, also available after cancellation."""
        return self._progress

    @property
    def backup(self) -> Optional[MagicBackupReceipt]:
        return self._backup

    @staticmethod
    def _check_dump(result: MagicCardDump) -> None:
        _check_identity(result.identity)
        _check_image(result.image, "observed image")
        if result.image[:4] != result.identity.uid:
            raise MagicCardReadError("observed image UID differs from its scan")
        if type(result.independent_reads) is not int or result.independent_reads < 2:
            raise MagicCardReadError("observed image lacks two independent reads")

    @staticmethod
    def _make_plan(original: MagicCardDump, desired: bytes) -> MagicWritePlan:
        MagicClassicTransaction._check_dump(original)
        _check_image(desired, "desired image")
        plan = MagicWritePlan(
            original.identity,
            original.image,
            desired,
            _changes(original.image, desired),
        )
        _validate_plan(plan)
        return plan

    def plan_block(self, block: int, data: bytes) -> MagicWritePlan:
        _check_block(block)
        if not isinstance(data, bytes) or len(data) != BLOCK_SIZE:
            raise MagicCardReadError(
                "ordinary block data must contain exactly 16 bytes"
            )
        original = self._reader.dump()
        self._check_dump(original)
        offset = block * BLOCK_SIZE
        desired = original.image[:offset] + data + original.image[offset + BLOCK_SIZE :]
        return self._make_plan(original, desired)

    def plan_restore(self, source_image: bytes) -> MagicWritePlan:
        _check_image(source_image, "restore image")
        return self._make_plan(self._reader.dump(), source_image)

    def _set_stage(self, stage: str, block: Optional[int] = None) -> None:
        self._stage, self._block = stage, block

    def _snapshot(self, failed: bool = False) -> MagicWriteProgress:
        return MagicWriteProgress(
            tuple(self._attempted),
            tuple(self._possible_written),
            tuple(self._verified),
            self._stage if failed else None,
            self._block if failed else None,
        )

    @contextmanager
    def _session(self, plan: MagicWritePlan) -> Iterator[None]:
        original: Optional[BaseException] = None
        try:
            self._reader._field_off()
            identity = self._reader._scan_identity()
            if identity != plan.identity:
                raise MagicCardReadError(
                    "tag identity changed before an ordinary-block operation"
                )
            self._reader._unlock()
            block0 = self._reader._read_block(0)
            self._reader._validate_block0(block0, identity)
            if block0 != plan.original_image[:16]:
                raise MagicCardReadError(
                    "Block 0 changed before an ordinary-block operation"
                )
            yield
        except BaseException as error:
            original = error
            raise
        finally:
            try:
                self._reader._field_off()
            except BaseException as cleanup:
                if original is not None:
                    self._reader._record_cleanup_failure(original, cleanup)
                elif isinstance(cleanup, Exception):
                    raise MagicCardReadError(
                        f"RF field cleanup failed: {cleanup}"
                    ) from cleanup
                else:
                    raise

    def _program(self, change: MagicBlockChange) -> None:
        self._set_stage("write command", change.block)
        ack = self._reader._raw(
            "ordinary block WRITE command",
            options=_options(crc=True, keep=True),
            data=bytes((0xA0, change.block)),
            expected_length=1,
        )
        if ack != b"\x0a":
            raise MagicCardReadError(
                "ordinary block WRITE command did not return exact 0A ACK"
            )
        # There must be no filesystem work between A0's ACK and its data. The
        # pending record was synced before A0; this update only changes memory.
        self._set_stage("write data", change.block)
        self._possible_written.append(change.block)
        ack = self._reader._raw(
            "ordinary block WRITE data",
            options=_options(crc=True, keep=True),
            data=change.after,
            expected_length=1,
        )
        if ack != b"\x0a":
            raise MagicCardReadError(
                "ordinary block WRITE data did not return exact 0A ACK"
            )

    def _failure(self, original: BaseException) -> BaseException:
        self._progress = self._snapshot(failed=True)
        description = (
            f"{original or type(original).__name__}; stage={self._stage}, "
            f"block={self._block}; attempted={self._progress.attempted_blocks}, "
            f"possibly written={self._progress.possible_written_blocks}, "
            f"verified={self._progress.verified_blocks}"
        )
        if not isinstance(original, Exception):
            setattr(original, "args", (description,))
            original.__dict__["progress"] = self._progress
            original.__dict__["backup"] = self._backup
            return original
        failure = MagicCardWriteError(
            description, progress=self._progress, backup=self._backup
        )
        failure.__dict__["cleanup_error"] = getattr(original, "cleanup_error", None)
        return failure

    @staticmethod
    def _journal_failure(
        failure: BaseException, secondary: BaseException, action: str
    ) -> None:
        # Logging/close failures are secondary to the programming outcome.
        # Preserve both that outcome and any existing RF cleanup/cause chain.
        setattr(failure, "args", (f"{failure}; {action} also failed: {secondary}",))
        previous = failure.__dict__.get("journal_errors", ())
        failure.__dict__["journal_errors"] = (*previous, secondary)

    def apply(self, plan: MagicWritePlan, *, backup_path: Path) -> MagicWriteResult:
        if self._used:
            raise MagicCardWriteError(
                "this transaction has already attempted apply; create a fresh plan "
                "and transaction instead of retrying",
                progress=self._progress,
                backup=self._backup,
            )
        self._used = True
        journal: Optional[WriteJournal] = None
        close_attempted = False
        try:
            self._set_stage("validate plan")
            _validate_plan(plan)
            if not isinstance(backup_path, Path):
                raise MagicCardReadError("backup_path must be a Path")
            self._set_stage("apply preflight")
            observed = self._reader.dump()
            self._check_dump(observed)
            if (
                observed.identity != plan.identity
                or observed.image != plan.original_image
            ):
                raise MagicCardReadError(
                    "the card no longer matches the original write plan"
                )
            if not plan.changes:
                self._progress = self._snapshot()
                return MagicWriteResult(self._progress, None)
            self._set_stage("create backup")
            journal = WriteJournal.create(plan, backup_path)
            self._backup = journal.backup
            self._set_stage("validate backup")
            journal.validate_backup(plan)

            for change in plan.changes:
                self._set_stage("write preflight", change.block)
                with self._session(plan):
                    if self._reader._read_block(change.block) != change.before:
                        raise MagicCardReadError(
                            "ordinary block changed since the write plan"
                        )
                    self._set_stage("record pending", change.block)
                    journal.attempt(change.block)
                    # An attempted A0 and a possibly accepted data frame are
                    # distinct facts. Never infer "unchanged" from a timeout.
                    self._attempted.append(change.block)
                    self._program(change)
                    self._set_stage("write cleanup", change.block)
                self._set_stage("verify block", change.block)
                with self._session(plan):
                    if self._reader._read_block(change.block) != change.after:
                        raise MagicCardReadError(
                            "independent ordinary-block readback differs"
                        )
                    self._set_stage("verification cleanup", change.block)
                self._verified.append(change.block)
                self._set_stage("record verification", change.block)
                journal.verified(change.block)

            self._set_stage("final verification")
            final = self._reader.dump()
            self._check_dump(final)
            if final.identity != plan.identity or final.image != plan.desired_image:
                raise MagicCardReadError(
                    "final complete image differs from the desired image"
                )
            self._set_stage("complete journal")
            journal.finish()
            self._set_stage("close journal")
            close_attempted = True
            journal.close()
        except BaseException as original:
            if self._backup is None:
                receipt = getattr(original, "backup", None)
                if isinstance(receipt, MagicBackupReceipt):
                    self._backup = receipt
            failure = self._failure(original)
            if journal is not None:
                try:
                    journal.fail(self._progress, failure)
                except BaseException as secondary:
                    self._journal_failure(failure, secondary, "recording failure")
                if not close_attempted:
                    try:
                        journal.close()
                    except BaseException as secondary:
                        self._journal_failure(failure, secondary, "closing journal")
            if failure is original:
                raise
            raise failure from original
        self._progress = self._snapshot()
        return MagicWriteResult(self._progress, self._backup)
