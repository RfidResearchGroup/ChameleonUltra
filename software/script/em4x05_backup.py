"""Read-only EM4205/4305 word transactions and complete readable-word backups."""

from __future__ import annotations

import hashlib
import json
import os
import struct
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterator, Optional, Protocol, Sequence

from chameleon_com import Response
from chameleon_enum import Command, Status
from em4x05_decode import EMReadError, decode_capture, unpack_capture

READ_CAPTURE_COMMAND = 3043  # Provisional, subject to maintainer allocation.
SCHEMA = "chameleon-em4205-4305-readable-v1"


class Transport(Protocol):
    @property
    def commands(self) -> Sequence[int]: ...

    def send_cmd_sync(
        self, cmd: int, data: Optional[bytes] = None, status: int = 0, timeout: int = 3
    ) -> Response: ...


def _u32(value: Any) -> int:
    if type(value) is not int or not 0 <= value <= 0xFFFFFFFF:
        raise EMReadError("expected a 32-bit unsigned value")
    return value


def check_output(path: Path) -> None:
    if path.exists() or path.is_symlink() or not path.parent.is_dir():
        raise EMReadError("backup needs a new path in an existing private directory")


def _canonical(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode()


def validate_backup(value: Any) -> dict[str, Any]:
    expected = {
        "schema",
        "created_utc",
        "words",
        "unreadable_words",
        "login_attempted",
        "chip",
        "document_sha256",
    }
    if type(value) is not dict or set(value) != expected or value["schema"] != SCHEMA:
        raise EMReadError("unsupported backup document")
    words = value["words"]
    if type(words) is not list or len(words) != 16 or words[2] is not None:
        raise EMReadError("backup must mark password word 2 unreadable")
    for i, word in enumerate(words):
        if i != 2:
            _u32(word)
    if value["unreadable_words"] != [2] or type(value["login_attempted"]) is not bool:
        raise EMReadError("invalid readable-word metadata")
    chip_type = (words[0] >> 1) & 15
    if (
        chip_type not in (8, 9)
        or value["chip"] != {8: "EM4205", 9: "EM4305"}[chip_type]
    ):
        raise EMReadError("first scope supports identified EM4205/4305 chips")
    from em4x05_decode import config_parameters

    config_parameters(words[4])
    try:
        timestamp = datetime.fromisoformat(value["created_utc"])
        if timestamp.utcoffset() is None:
            raise ValueError("missing timezone")
    except (TypeError, ValueError) as error:
        raise EMReadError("invalid backup timestamp") from error
    payload = {key: item for key, item in value.items() if key != "document_sha256"}
    if hashlib.sha256(_canonical(payload)).hexdigest() != value["document_sha256"]:
        raise EMReadError("backup checksum mismatch")
    return value


def save_backup(path: Path, value: dict[str, Any]) -> None:
    validate_backup(value)
    check_output(path)
    data = _canonical(value) + b"\n"
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            output = os.fdopen(descriptor, "wb")
        except BaseException:
            os.close(descriptor)
            raise
        with output:
            if output.write(data) != len(data):
                raise OSError("short backup write")
            output.flush()
            os.fsync(output.fileno())
        with path.open("rb") as saved:
            if saved.read(len(data) + 1) != data:
                raise OSError("backup readback differs")
    except OSError as error:
        raise EMReadError(f"backup publication failed: {error}") from error


class EM4x05Reader:
    def __init__(self, device: Transport):
        self.device = device

    def _call(
        self,
        cmd: int,
        data: Optional[bytes] = None,
        *,
        status: Status = Status.SUCCESS,
        length: Optional[int] = 0,
    ) -> bytes:
        response = self.device.send_cmd_sync(cmd, data)
        if (
            response.status != status
            or type(response.data) is not bytes
            or (length is not None and len(response.data) != length)
        ):
            raise EMReadError(f"command {int(cmd)} returned invalid status/data")
        return response.data

    @contextmanager
    def _reader_mode(self) -> Iterator[None]:
        required = {
            Command.GET_DEVICE_MODE,
            Command.CHANGE_DEVICE_MODE,
            READ_CAPTURE_COMMAND,
        }
        if not required.issubset(self.device.commands):
            raise EMReadError("firmware lacks the read-only EM capture command")
        mode = self._call(Command.GET_DEVICE_MODE, length=1)[0]
        if mode not in (0, 1):
            raise EMReadError("invalid original device mode")
        primary: Optional[BaseException] = None
        try:
            if mode == 0:
                self._call(Command.CHANGE_DEVICE_MODE, b"\x01")
            yield
        except BaseException as error:
            primary = error
            raise
        finally:
            if mode == 0:
                try:
                    self._call(Command.CHANGE_DEVICE_MODE, b"\x00")
                    if self._call(Command.GET_DEVICE_MODE, length=1) != b"\x00":
                        raise EMReadError("original mode readback failed")
                except Exception as error:
                    if primary is None:
                        raise EMReadError("failed to restore original mode") from error
                    setattr(primary, "restore_error", str(error))

    def _read(self, word: int, password: Optional[int], config: Optional[int]) -> int:
        request = struct.pack(
            "!BBI", word, password is not None, 0 if password is None else password
        )
        payload = self._call(
            READ_CAPTURE_COMMAND, request, status=Status.LF_TAG_OK, length=3080
        )
        decoded = decode_capture(
            unpack_capture(payload), config=config, expect_config=word == 4
        )
        result = decoded["word"]
        if type(result) is not int:
            raise EMReadError("decoder returned a malformed word")
        return result

    def _twice(self, word: int, password: Optional[int], config: Optional[int]) -> int:
        first = self._read(word, password, config)
        if self._read(word, password, config) != first:
            raise EMReadError(f"word {word} changed between independent reads")
        return first

    def read_word(self, word: int, password: Optional[int] = None) -> int:
        if type(word) is not int or not 0 <= word < 16 or word == 2:
            raise EMReadError("word must be 0..15 excluding unreadable password word 2")
        if password is not None:
            _u32(password)
        with self._reader_mode():
            config = self._twice(4, password, None)
            return config if word == 4 else self._twice(word, password, config)

    def backup(self, path: Path, password: Optional[int] = None) -> dict[str, Any]:
        check_output(path)
        if password is not None:
            _u32(password)
        with self._reader_mode():
            config = self._twice(4, password, None)
            chip_info = self._twice(0, password, config)
            chip_type = (chip_info >> 1) & 15
            if chip_type not in (8, 9):
                raise EMReadError("unsupported chip identity; requires EM4205/4305")
            serial = self._twice(1, password, config)
            words: list[Optional[int]] = [chip_info, serial, None, None, config] + [
                None
            ] * 11
            for word in range(16):
                if word not in (0, 1, 2, 4):
                    words[word] = self._twice(word, password, config)
            # Power cycles occur between words; reread identity/config anchors.
            if (
                self._twice(0, password, config),
                self._twice(1, password, config),
                self._twice(4, password, None),
            ) != (chip_info, serial, config):
                raise EMReadError("card identity/config changed during backup")
            payload = {
                "schema": SCHEMA,
                "created_utc": datetime.now(timezone.utc).isoformat(),
                "words": words,
                "unreadable_words": [2],
                # Do not serialize the supplied password or pretend it was read.
                "login_attempted": password is not None,
                "chip": {8: "EM4205", 9: "EM4305"}[chip_type],
            }
            document = dict(
                payload, document_sha256=hashlib.sha256(_canonical(payload)).hexdigest()
            )
        # Publish only after original mode restoration has also succeeded.
        save_backup(path, document)
        return document
