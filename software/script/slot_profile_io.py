"""Checksummed source profiles and private no-clobber HF slot backups."""

from __future__ import annotations

import hashlib
import json
import os
import re
import tempfile
from dataclasses import asdict
from functools import reduce
from operator import xor
from pathlib import Path
from typing import Optional

from slot_profile_data import (
    ClassicProfile,
    EmulatorConfig,
    SlotProfileError,
    SlotSnapshot,
    SlotState,
    validate_profile,
    validate_snapshot,
)

LIMIT = 65536
FORMAT = "chameleon-classic-slot-v1"


def _hex(value: object, length: int, name: str) -> bytes:
    if (
        not isinstance(value, str)
        or re.fullmatch(rf"[0-9a-fA-F]{{{length * 2}}}", value) is None
    ):
        raise SlotProfileError(f"invalid {name}")
    return bytes.fromhex(value)


def _read(source: Path) -> bytes:
    with source.open("rb") as stream:
        data = stream.read(LIMIT + 1)
    if len(data) > LIMIT:
        raise SlotProfileError("profile file is too large")
    return data


def _object(data: bytes) -> dict:
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise SlotProfileError("duplicate JSON member")
            result[key] = value
        return result

    try:
        value = json.loads(data, object_pairs_hook=unique)
    except (ValueError, UnicodeError, RecursionError) as error:
        raise SlotProfileError("invalid profile JSON") from error
    if not isinstance(value, dict):
        raise SlotProfileError("profile JSON must be an object")
    return value


def _image(blocks: object) -> bytes:
    if not isinstance(blocks, list) or len(blocks) != 64:
        raise SlotProfileError("profile must contain exactly 64 blocks")
    return b"".join(_hex(block, 16, "memory block") for block in blocks)


def load_card_profile(
    source: Path,
    *,
    input_format: Optional[str] = None,
    atqa: Optional[str] = None,
    sak: Optional[str] = None,
    expected_sha256: Optional[str] = None,
) -> ClassicProfile:
    selected = input_format or ("json" if source.suffix.lower() == ".json" else "bin")
    if selected not in ("bin", "json"):
        raise SlotProfileError("source format must be bin or json")
    data = _read(source)
    if selected == "json":
        if atqa is not None or sak is not None:
            raise SlotProfileError("JSON identity must come from its metadata")
        value = _object(data)
        if (
            value.get("format") != "chameleon-gen1a-classic1k-v1"
            or value.get("read_profile") != "gen1a-compatible-classic1k"
            or type(value.get("block_count")) is not int
            or value["block_count"] != 64
            or type(value.get("block_size")) is not int
            or value["block_size"] != 16
        ):
            raise SlotProfileError("unsupported card-image profile")
        reads = value.get("independent_reads")
        if (
            type(reads) is not int
            or reads < 2
            or type(value.get("key_material_verified")) is not bool
        ):
            raise SlotProfileError("invalid card-image read metadata")
        image = _image(value.get("blocks"))
        uid = _hex(value.get("uid"), 4, "UID")
        atqa_bytes = _hex(value.get("atqa_wire_order"), 2, "ATQA")
        sak_bytes = _hex(value.get("sak"), 1, "SAK")
        checksum = _hex(value.get("sha256"), 32, "image SHA-256")
        if checksum != hashlib.sha256(image).digest():
            raise SlotProfileError("card-image SHA-256 does not match")
        if uid != image[:4]:
            raise SlotProfileError("card metadata UID differs from Block 0")
    else:
        image = data
        if atqa is None or sak is None or expected_sha256 is None:
            raise SlotProfileError("BIN requires --atqa, --sak and --sha256")
        atqa_bytes = _hex(atqa, 2, "ATQA")
        sak_bytes = _hex(sak, 1, "SAK")
        uid = image[:4]
    if len(image) != 1024:
        raise SlotProfileError("card image must contain exactly 1024 bytes")
    if image[4] != reduce(xor, image[:4]) or sak_bytes != b"\x08":
        raise SlotProfileError(
            "only valid 4-byte UID / SAK 08 card images are supported"
        )
    if expected_sha256 is not None:
        if (
            _hex(expected_sha256, 32, "expected SHA-256")
            != hashlib.sha256(image).digest()
        ):
            raise SlotProfileError("card image differs from expected SHA-256")
    # A reader dump contains no emulator policy. Import as a predictable
    # read-only profile with both magic write paths disabled, not as a claim
    # that the file's sector keys were independently authenticated.
    profile = ClassicProfile(image, uid, atqa_bytes, sak_bytes)
    validate_profile(profile)
    return profile


def _canonical(document: dict) -> bytes:
    return json.dumps(document, sort_keys=True, separators=(",", ":")).encode("utf-8")


def snapshot_document(snapshot: SlotSnapshot) -> dict:
    validate_snapshot(snapshot)
    state = snapshot.state
    profile = state.profile
    body = {
        "format": FORMAT,
        "device_sha256": snapshot.device_sha256,
        "slot": snapshot.slot,
        "hf_type": state.tag_type,
        "hf_enabled": state.enabled,
        "profile": None
        if profile is None
        else {
            "blocks": [profile.image[i : i + 16].hex() for i in range(0, 1024, 16)],
            "uid": profile.uid.hex(),
            "atqa_wire_order": profile.atqa.hex(),
            "sak": profile.sak.hex(),
            "ats": profile.ats.hex(),
            "config": asdict(profile.config),
        },
    }
    return {**body, "sha256": hashlib.sha256(_canonical(body)).hexdigest()}


def load_snapshot(source: Path) -> SlotSnapshot:
    document = _object(_read(source))
    checksum = _hex(document.pop("sha256", None), 32, "snapshot SHA-256")
    if checksum != hashlib.sha256(_canonical(document)).digest():
        raise SlotProfileError("slot snapshot SHA-256 does not match")
    if document.get("format") != FORMAT:
        raise SlotProfileError("unsupported slot snapshot format")
    raw = document.get("profile")
    profile = None
    if raw is not None:
        if not isinstance(raw, dict) or not isinstance(raw.get("config"), dict):
            raise SlotProfileError("invalid saved profile")
        if set(raw["config"]) != {
            "detection",
            "gen1a",
            "gen2",
            "block0",
            "write_mode",
            "field_reset",
            "prng",
        }:
            raise SlotProfileError("saved configuration must contain every field")
        uid = raw.get("uid")
        ats = raw.get("ats")
        if not isinstance(uid, str) or len(uid) not in (8, 14, 20):
            raise SlotProfileError("invalid saved UID length")
        if not isinstance(ats, str) or len(ats) % 2 or len(ats) > 508:
            raise SlotProfileError("invalid saved ATS length")
        try:
            config = EmulatorConfig(**raw["config"])
        except TypeError as error:
            raise SlotProfileError("invalid saved configuration fields") from error
        profile = ClassicProfile(
            _image(raw.get("blocks")),
            _hex(uid, len(uid) // 2, "saved UID"),
            _hex(raw.get("atqa_wire_order"), 2, "saved ATQA"),
            _hex(raw.get("sak"), 1, "saved SAK"),
            _hex(ats, len(ats) // 2, "saved ATS"),
            config,
        )
    device = document.get("device_sha256")
    slot = document.get("slot")
    tag_type = document.get("hf_type")
    enabled = document.get("hf_enabled")
    if (
        not isinstance(device, str)
        or not isinstance(slot, int)
        or isinstance(slot, bool)
        or not isinstance(tag_type, int)
        or isinstance(tag_type, bool)
        or not isinstance(enabled, bool)
    ):
        raise SlotProfileError("invalid slot snapshot descriptors")
    snapshot = SlotSnapshot(device, slot, SlotState(tag_type, enabled, profile))
    validate_snapshot(snapshot)
    return snapshot


def check_new_path(target: Path) -> None:
    if target.exists() or target.is_symlink():
        raise SlotProfileError(f"refusing to replace existing backup: {target}")
    if not target.parent.is_dir():
        raise SlotProfileError(f"backup directory does not exist: {target.parent}")


def save_snapshot(snapshot: SlotSnapshot, target: Path) -> None:
    check_new_path(target)
    content = (json.dumps(snapshot_document(snapshot), indent=2) + "\n").encode("utf-8")
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="wb",
            dir=target.parent,
            prefix=f".{target.name}.",
            suffix=".tmp",
            delete=False,
        ) as stream:
            temporary = Path(stream.name)
            if stream.write(content) != len(content):
                raise OSError("incomplete slot backup write")
            stream.flush()
            os.fsync(stream.fileno())
        if os.name == "nt":
            os.rename(temporary, target)
            temporary = None
        else:
            os.link(temporary, target)
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass
    # Checking the published path is a separate gate before any destructive
    # slot setter, including the factory-data command that writes flash.
    if load_snapshot(target) != snapshot:
        raise SlotProfileError("published slot backup did not read back exactly")
