"""Conservative, read-only Gen1A-compatible MIFARE Classic 1K access.

An exact wake-up ACK establishes only observed 40/43 compatibility. It does
not identify a chip generation or authorize configuration/write commands.
Keep one stationary tag in the field: equal UIDs and equal readable memory
cannot prove that a tag was not exchanged for an identical clone.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import reduce
from operator import xor
from typing import Optional, Protocol

from chameleon_com import Response
from chameleon_enum import Status

BLOCK_SIZE = 16
CLASSIC_1K_BLOCKS = 64
INDEPENDENT_READS = 2
RAW_TIMEOUT_MS = 100


class MagicCardReadError(ValueError):
    """A complete, consistent read was not established."""

    def __init__(self, message: str):
        super().__init__(message)
        self.cleanup_error: Optional[BaseException] = None


@dataclass(frozen=True)
class MagicCardIdentity:
    uid: bytes
    atqa: bytes
    sak: bytes


@dataclass(frozen=True)
class MagicCardInfo:
    identity: MagicCardIdentity
    block0: bytes


@dataclass(frozen=True)
class MagicCardDump:
    identity: MagicCardIdentity
    image: bytes
    independent_reads: int = INDEPENDENT_READS


class MagicClassicCommand(Protocol):
    def hf14a_scan(self) -> list[dict[str, bytes]]: ...

    def hf14a_raw_response(
        self,
        options: dict[str, int],
        resp_timeout_ms: int = RAW_TIMEOUT_MS,
        data: bytes = b"",
        bitlen: Optional[int] = None,
    ) -> Response: ...


def _options(
    *,
    activate: bool = False,
    response: bool = True,
    crc: bool = False,
    select: bool = False,
    keep: bool = False,
    check_crc: bool = False,
) -> dict[str, int]:
    return {
        "activate_rf_field": int(activate),
        "wait_response": int(response),
        "append_crc": int(crc),
        "auto_select": int(select),
        "keep_rf_field": int(keep),
        "check_response_crc": int(check_crc),
    }


class MagicClassicReader:
    def __init__(self, cmd: MagicClassicCommand):
        self.cmd = cmd

    def _raw(
        self,
        stage: str,
        *,
        options: dict[str, int],
        data: bytes = b"",
        bitlen: Optional[int] = None,
        expected_length: int,
    ) -> bytes:
        try:
            response = self.cmd.hf14a_raw_response(
                options=options,
                resp_timeout_ms=RAW_TIMEOUT_MS,
                data=data,
                bitlen=bitlen,
            )
        except Exception as error:
            raise MagicCardReadError(f"{stage}: transport failed: {error}") from error
        # The legacy raw API returns only data. Inspecting status here also
        # rejects plausible-looking bytes returned with CRC/collision errors.
        if (
            not isinstance(response.status, int)
            or isinstance(response.status, bool)
            or response.status != Status.HF_TAG_OK
        ):
            status = response.status
            if isinstance(status, int):
                reason = f"firmware status 0x{int(status):02X}"
            else:
                reason = "invalid firmware status"
            raise MagicCardReadError(f"{stage}: {reason}")
        if (
            not isinstance(response.data, bytes)
            or len(response.data) != expected_length
        ):
            raise MagicCardReadError(
                f"{stage}: expected exactly {expected_length} response bytes"
            )
        return response.data

    def _field_off(self) -> None:
        self._raw(
            "RF field cleanup",
            options=_options(response=False),
            expected_length=0,
        )

    def _scan_identity(self) -> MagicCardIdentity:
        try:
            tags = self.cmd.hf14a_scan()
        except Exception as error:
            raise MagicCardReadError(f"card scan failed: {error}") from error
        if not isinstance(tags, list) or not tags:
            raise MagicCardReadError("no ISO14443-A tag found")
        if len(tags) != 1:
            raise MagicCardReadError("keep exactly one ISO14443-A tag in the field")
        tag = tags[0]
        if not isinstance(tag, dict):
            raise MagicCardReadError("malformed ISO14443-A scan result")
        uid, atqa, sak = tag.get("uid"), tag.get("atqa"), tag.get("sak")
        if (
            not isinstance(uid, bytes)
            or not isinstance(atqa, bytes)
            or not isinstance(sak, bytes)
            or len(atqa) != 2
            or len(sak) != 1
        ):
            raise MagicCardReadError("malformed ISO14443-A identity")
        if len(uid) != 4 or sak != b"\x08":
            raise MagicCardReadError(
                "only 4-byte UID, SAK 08 Classic 1K profiles are supported"
            )
        return MagicCardIdentity(uid, atqa, sak)

    def _unlock(self) -> None:
        # Select then HALT before the short wake-up, matching the Gen1A state
        # transition. HALT intentionally has no response; status must still be OK.
        self._raw(
            "select/HALT",
            options=_options(
                activate=True, select=True, crc=True, keep=True, response=False
            ),
            data=b"\x50\x00",
            expected_length=0,
        )
        for command, bits, name in (
            (b"\x40", 7, "Gen1A wake-up 40(7)"),
            (b"\x43", None, "Gen1A wake-up 43"),
        ):
            ack = self._raw(
                name,
                options=_options(keep=True),
                data=command,
                bitlen=bits,
                expected_length=1,
            )
            if ack != b"\x0a":
                raise MagicCardReadError(f"{name}: expected exact 0A ACK")

    def _read_block(self, block: int) -> bytes:
        # This private path has no variable opcode: only ordinary READ 30xx
        # is emitted after the two fixed wake-up commands above.
        return self._raw(
            f"read block {block}",
            options=_options(crc=True, keep=True, check_crc=True),
            data=bytes((0x30, block)),
            expected_length=BLOCK_SIZE,
        )

    @staticmethod
    def _validate_block0(block0: bytes, identity: MagicCardIdentity) -> None:
        if block0[:4] != identity.uid:
            raise MagicCardReadError("Block 0 UID differs from the scanned tag")
        if block0[4] != reduce(xor, block0[:4]):
            raise MagicCardReadError("Block 0 BCC does not match its UID")

    @staticmethod
    def _record_cleanup_failure(
        original: BaseException, cleanup: BaseException
    ) -> None:
        # Python 3.9 has neither add_note nor ExceptionGroup. Retain the primary
        # exception and its existing cause, while exposing the cleanup failure
        # in both its message and a secondary attribute. KeyboardInterrupt must
        # remain KeyboardInterrupt so the caller can still cancel normally.
        message = str(original) or type(original).__name__
        setattr(
            original, "args", (f"{message}; RF field cleanup also failed: {cleanup}",)
        )
        original.__dict__["cleanup_error"] = cleanup

    def _read_pass(self, block_count: int) -> tuple[MagicCardIdentity, bytes]:
        original: Optional[BaseException] = None
        try:
            self._field_off()
            identity = self._scan_identity()
            self._unlock()
            block0 = self._read_block(0)
            self._validate_block0(block0, identity)
            blocks = [block0]
            blocks.extend(self._read_block(block) for block in range(1, block_count))
            # Re-read Block 0 before leaving this uninterrupted backdoor
            # session. No partial image escapes if the tag changes or is lost.
            after = self._read_block(0)
            self._validate_block0(after, identity)
            if after != blocks[0]:
                raise MagicCardReadError("Block 0 changed during the read")
            result = identity, b"".join(blocks)
        except BaseException as error:
            original = error
            raise
        finally:
            try:
                self._field_off()
            except BaseException as cleanup:
                if original is not None:
                    self._record_cleanup_failure(original, cleanup)
                elif isinstance(cleanup, Exception):
                    raise MagicCardReadError(
                        f"RF field cleanup failed; no read accepted: {cleanup}"
                    ) from cleanup
                else:
                    raise
        return result

    def _read_consistent(self, block_count: int) -> tuple[MagicCardIdentity, bytes]:
        identity, image = self._read_pass(block_count)
        # A new scan, HALT and wake-up make the second pass independent of the
        # first session's RF state. Equality is a consistency check, not proof
        # of immutable tag identity.
        other_identity, other_image = self._read_pass(block_count)
        if identity != other_identity:
            raise MagicCardReadError("tag identity differs between independent reads")
        if image != other_image:
            raise MagicCardReadError("data differs between independent reads")
        return identity, image

    def read_info(self) -> MagicCardInfo:
        identity, block0 = self._read_consistent(1)
        return MagicCardInfo(identity, block0)

    def dump(self) -> MagicCardDump:
        identity, image = self._read_consistent(CLASSIC_1K_BLOCKS)
        return MagicCardDump(identity, image)
