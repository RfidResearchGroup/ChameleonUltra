"""Strict private NTAG snapshots; checksums detect corruption, not forgery."""

from __future__ import annotations

import hashlib
import json
import os
import stat
from pathlib import Path
from typing import Any

from ntag_profile_data import (
    NtagConfig,
    NtagProfile,
    NtagProfileError,
    SlotSnapshot,
    SlotState,
    validate_snapshot,
)

SCHEMA = "chameleon-ntag-slot-v1"
MAX_FILE = 32768


def check_new_path(path: Path) -> None:
    if path.exists() or path.is_symlink() or not path.parent.is_dir():
        raise NtagProfileError("use a new private file in an existing directory")


def _canonical(value: dict[str, Any]) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), allow_nan=False
    ).encode()


def _pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise NtagProfileError("duplicate JSON member")
        result[key] = value
    return result


def _hex(value: Any, max_bytes: int) -> bytes:
    if type(value) is not str or len(value) > max_bytes * 2:
        raise NtagProfileError("invalid hex field")
    try:
        decoded = bytes.fromhex(value)
    except ValueError as error:
        raise NtagProfileError("invalid hex field") from error
    if decoded.hex() != value:
        raise NtagProfileError("noncanonical hex field")
    return decoded


def snapshot_object(snapshot: SlotSnapshot) -> dict[str, Any]:
    validate_snapshot(snapshot)
    state = snapshot.state
    profile = state.profile
    value = {
        "schema": SCHEMA,
        "device_sha256": snapshot.device_sha256,
        "slot": snapshot.slot,
        "tag_type": state.tag_type,
        "enabled": state.enabled,
        "profile": None
        if profile is None
        else {
            "image": profile.image.hex(),
            "uid": profile.uid.hex(),
            "atqa": profile.atqa.hex(),
            "sak": profile.sak.hex(),
            "ats": profile.ats.hex(),
            "config": profile.config.raw.hex(),
        },
    }
    return dict(value, document_sha256=hashlib.sha256(_canonical(value)).hexdigest())


def parse_snapshot(value: Any) -> SlotSnapshot:
    if (
        type(value) is not dict
        or set(value)
        != {
            "schema",
            "device_sha256",
            "slot",
            "tag_type",
            "enabled",
            "profile",
            "document_sha256",
        }
        or value["schema"] != SCHEMA
    ):
        raise NtagProfileError("unsupported NTAG snapshot schema")
    raw = value["profile"]
    if raw is None:
        profile = None
    elif type(raw) is dict and set(raw) == {
        "image",
        "uid",
        "atqa",
        "sak",
        "ats",
        "config",
    }:
        profile = NtagProfile(
            value["tag_type"],
            _hex(raw["image"], 968),
            _hex(raw["uid"], 10),
            _hex(raw["atqa"], 2),
            _hex(raw["sak"], 1),
            _hex(raw["ats"], 254),
            NtagConfig(_hex(raw["config"], 3)),
        )
    else:
        raise NtagProfileError("invalid profile fields")
    snapshot = SlotSnapshot(
        value["device_sha256"],
        value["slot"],
        SlotState(value["tag_type"], value["enabled"], profile),
    )
    validate_snapshot(snapshot)
    payload = {k: v for k, v in value.items() if k != "document_sha256"}
    if hashlib.sha256(_canonical(payload)).hexdigest() != value["document_sha256"]:
        raise NtagProfileError("snapshot checksum mismatch")
    return snapshot


def load_snapshot(path: Path) -> SlotSnapshot:
    try:
        if not stat.S_ISREG(path.lstat().st_mode):
            raise NtagProfileError("snapshot must be a regular file")
        with path.open("rb") as source:
            data = source.read(MAX_FILE + 1)
        if len(data) > MAX_FILE:
            raise NtagProfileError("snapshot is too large")
        value = json.loads(data.decode("utf-8"), object_pairs_hook=_pairs)
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise NtagProfileError(f"cannot read snapshot: {error}") from error
    return parse_snapshot(value)


def save_snapshot(snapshot: SlotSnapshot, path: Path) -> None:
    data = _canonical(snapshot_object(snapshot)) + b"\n"
    check_new_path(path)
    try:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        try:
            output = os.fdopen(descriptor, "wb")
        except BaseException:
            os.close(descriptor)
            raise
        with output:
            if output.write(data) != len(data):
                raise OSError("short write")
            output.flush()
            os.fsync(output.fileno())
        if load_snapshot(path) != snapshot:
            raise OSError("snapshot readback differs")
    except OSError as error:
        raise NtagProfileError(f"snapshot publication failed: {error}") from error
