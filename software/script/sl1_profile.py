"""Lossless Classic/SL1 profile codec and bounded acquisition/upload helpers."""

from __future__ import annotations

import copy
import json
import re
import struct
from typing import Any, Protocol

COUNTS = {
    "mini": (20, 5, 1000),
    "1k": (64, 16, 1001),
    "2k": (128, 32, 1002),
    "4k": (256, 40, 1003),
}
COMMANDS = set(range(4045, 4052))


def fields(value: Any, names: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != set(names.split()):
        raise ValueError("Unexpected or missing profile fields")
    return value


def hex_bytes(value: Any, size: int | None = None) -> bytes:
    if not isinstance(value, str) or not re.fullmatch(r"(?:[0-9A-F]{2})*", value):
        raise ValueError("Expected uppercase byte hex")
    result = bytes.fromhex(value)
    if size is not None and len(result) != size:
        raise ValueError("Invalid byte length")
    return result


def sector_range(sector: int) -> range:
    if not 0 <= sector < 40:
        raise ValueError("Invalid sector")
    start = sector * 4 if sector < 32 else 128 + (sector - 32) * 16
    return range(start, start + (4 if sector < 32 else 16))


def access_valid(trailer: bytes) -> bool:
    b6, b7, b8 = trailer[6:9]
    return (
        (b6 & 15) ^ (b7 >> 4) == 15
        and (b6 >> 4) ^ (b8 & 15) == 15
        and (b7 & 15) ^ (b8 >> 4) == 15
    )


def validate(profile: Any) -> dict[str, Any]:
    p = fields(profile, "format version name hf14a classic cardAuth")
    if (
        p["format"] != "chameleon-sl1-profile"
        or type(p["version"]) is not int
        or p["version"] != 1
        or not isinstance(p["name"], str)
    ):
        raise ValueError("Unsupported profile format/version/name")
    h = fields(p["hf14a"], "uid atqa sak ats")
    if len(hex_bytes(h["uid"])) not in (4, 7):
        raise ValueError("UID must have four or seven bytes")
    hex_bytes(h["atqa"], 2)
    hex_bytes(h["sak"], 1)
    if h["ats"] is not None:
        ats = hex_bytes(h["ats"])
        if ats and (len(ats) < 2 or len(ats) > 254 or ats[0] != len(ats)):
            raise ValueError("Invalid TL-prefixed ATS")
    c = fields(p["classic"], "type provenance blocks keys")
    if c["type"] not in COUNTS or c["provenance"] not in (
        "read",
        "imported",
        "synthetic",
        "legacy-unverified",
    ):
        raise ValueError("Invalid Classic type/provenance")
    blocks, sectors, _ = COUNTS[c["type"]]
    if (
        not isinstance(c["blocks"], list)
        or len(c["blocks"]) != blocks
        or not isinstance(c["keys"], list)
        or len(c["keys"]) != sectors
    ):
        raise ValueError("Invalid geometry")
    for b in c["blocks"]:
        fields(b, "data status key errors")
        if not isinstance(b["data"], str) or not re.fullmatch(
            r"(?:[0-9A-F]{2}|\?\?){16}", b["data"]
        ):
            raise ValueError("Invalid block bytes")
        if b["status"] not in ("unread", "read", "error", "imported") or b[
            "key"
        ] not in (None, "A", "B"):
            raise ValueError("Invalid block state")
        if b["status"] in ("unread", "error") and b["data"] != "??" * 16:
            raise ValueError("Unread/error bytes must be unknown")
        if not isinstance(b["errors"], list):
            raise ValueError("Invalid errors")
        for error in b["errors"]:
            fields(error, "key status reason")
            if (
                error["key"] not in ("A", "B")
                or (error["status"] is not None and type(error["status"]) is not int)
                or not isinstance(error["reason"], str)
            ):
                raise ValueError("Invalid read error")
    for keys in c["keys"]:
        fields(keys, "a b")
        for key in keys.values():
            fields(key, "value status evidence")
            if key["status"] not in ("unknown", "provided", "verified") or (
                key["evidence"] is not None and not isinstance(key["evidence"], str)
            ):
                raise ValueError("Invalid key state")
            if key["status"] == "unknown":
                if key["value"] is not None:
                    raise ValueError("Unknown key has a value")
            else:
                hex_bytes(key["value"], 6)
    a = p["cardAuth"]
    if a is not None:
        fields(a, "enabled scenario keyNumber key verification")
        v = fields(a["verification"], "status evidence")
        if (
            type(a["enabled"]) is not bool
            or a["scenario"] != "sl1-cardauth-9004"
            or type(a["keyNumber"]) is not int
            or a["keyNumber"] != 36868
        ):
            raise ValueError("Invalid CardAuth configuration")
        hex_bytes(a["key"], 16)
        if v["status"] not in ("provided", "verified") or (
            v["evidence"] is not None and not isinstance(v["evidence"], str)
        ):
            raise ValueError("Invalid verification")
        if a["enabled"] and (h["ats"] is None or len(hex_bytes(h["ats"])) < 2):
            raise ValueError("Enabled CardAuth requires acquired ATS")
    return p


def loads(source: str) -> dict[str, Any]:
    def unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("Duplicate JSON field")
            result[key] = value
        return result

    return validate(json.loads(source, object_pairs_hook=unique))


def dumps(profile: dict[str, Any]) -> str:
    return json.dumps(validate(profile), indent=2) + "\n"


def image(profile: dict[str, Any]) -> bytes:
    p = validate(profile)
    c = p["classic"]
    if c["provenance"] == "legacy-unverified":
        raise ValueError("Legacy coverage is unverified")
    data = [[b["data"][i : i + 2] for i in range(0, 32, 2)] for b in c["blocks"]]
    for sector, keys in enumerate(c["keys"]):
        trailer = data[sector_range(sector).stop - 1]
        for name, start in (("a", 0), ("b", 10)):
            if keys[name]["value"] is not None:
                value = keys[name]["value"]
                for i in range(6):
                    byte = value[i * 2 : i * 2 + 2]
                    if trailer[start + i] not in ("??", byte):
                        raise ValueError("Conflicting trailer key")
                    trailer[start + i] = byte
    if any("??" in block for block in data):
        raise ValueError("Incomplete profile")
    blocks = [bytes.fromhex("".join(block)) for block in data]
    if any(
        not access_valid(blocks[sector_range(s).stop - 1])
        for s in range(len(c["keys"]))
    ):
        raise ValueError("Invalid access-bit complements")
    return b"".join(blocks)


class Transport(Protocol):
    def sl1_profile_read_block(
        self, block: int, type_value: Any, key: bytes
    ) -> bytes: ...
    def sl1_profile_scan(self) -> dict[str, Any]: ...
    def sl1_profile_check_keys(
        self, block: int, key_type: int, keys: list[bytes]
    ) -> bytes | None: ...
    def set_device_reader_mode(self, reader_mode: bool = True) -> None: ...
    def get_device_capabilities(self) -> list[int]: ...
    def sl1_profile_begin(self, slot: int, payload: bytes) -> None: ...
    def sl1_profile_write(self, slot: int, first: int, data: bytes) -> None: ...
    def sl1_profile_save(self, slot: int) -> None: ...


def dictionary_keys(source: str) -> list[bytes]:
    """Parse a standard .dic: one known six-byte key per non-comment line."""
    keys: list[bytes] = []
    for number, line in enumerate(source.splitlines(), 1):
        text = line.strip()
        if not text or text.startswith("#"):
            continue
        try:
            key = hex_bytes(text.upper(), 6)
        except ValueError as error:
            raise ValueError(
                f"Invalid Classic dictionary key on line {number}"
            ) from error
        if key not in keys:
            keys.append(key)
    if not keys:
        raise ValueError("Known-key dictionary is empty")
    return keys


def unknown_profile(kind: str, identity: dict[str, Any]) -> dict[str, Any]:
    """Construct explicit unknown coverage for a first read, without guessed bytes."""
    if kind not in COUNTS:
        raise ValueError("Unsupported Classic geometry")
    blocks, sectors, _ = COUNTS[kind]
    return validate(
        {
            "format": "chameleon-sl1-profile",
            "version": 1,
            "name": identity["uid"],
            "hf14a": copy.deepcopy(identity),
            "cardAuth": None,
            "classic": {
                "type": kind,
                "provenance": "read",
                "blocks": [
                    {"data": "??" * 16, "status": "unread", "key": None, "errors": []}
                    for _ in range(blocks)
                ],
                "keys": [
                    {
                        name: {"value": None, "status": "unknown", "evidence": None}
                        for name in ("a", "b")
                    }
                    for _ in range(sectors)
                ],
            },
        }
    )


def require_identity(cmd: Transport, expected: dict[str, Any]) -> dict[str, Any]:
    observed = cmd.sl1_profile_scan()
    if any(observed.get(name) != expected[name] for name in ("uid", "atqa", "sak")) or (
        expected["ats"] is not None and observed.get("ats") != expected["ats"]
    ):
        raise ValueError("Selected card identity changed; acquisition rejected")
    return observed


def read_profile(
    cmd: Transport,
    seed: dict[str, Any] | None = None,
    kind: str | None = None,
    candidates: list[bytes] | None = None,
) -> dict[str, Any]:
    """Select one card, then acquire supplied known keys without recovery."""
    if seed is not None:
        result = copy.deepcopy(validate(seed))
        # Reject identity replacement rather than transferring an AES association.
        result["hf14a"] = require_identity(cmd, result["hf14a"])
    else:
        if kind is None:
            raise ValueError("First read requires an explicit Classic geometry")
        result = unknown_profile(kind, cmd.sl1_profile_scan())
    return acquire(cmd, result, candidates=candidates)


def acquire(
    cmd: Transport, profile: dict[str, Any], candidates: list[bytes] | None = None
) -> dict[str, Any]:
    """Read with supplied keys only; retain every failed attempt."""
    from chameleon_enum import MfcKeyType

    result = copy.deepcopy(validate(profile))
    c = result["classic"]
    c["provenance"] = "read"
    for supplied in c["keys"]:
        for key in supplied.values():
            if key["value"] is not None:
                key.update(status="provided", evidence=None)
    if result["cardAuth"] is not None:
        result["cardAuth"]["verification"] = {"status": "provided", "evidence": None}
    for sector, keys in enumerate(c["keys"]):
        require_identity(cmd, result["hf14a"])
        for name, key_type in (("a", 0x60), ("b", 0x61)):
            if not candidates:
                continue
            provided = keys[name]["value"]
            if provided is not None:
                found = cmd.sl1_profile_check_keys(
                    sector_range(sector).start,
                    key_type,
                    [hex_bytes(provided, 6)],
                )
                require_identity(cmd, result["hf14a"])
                if found is not None:
                    keys[name].update(
                        status="verified",
                        evidence="Supplied seed key authenticated",
                    )
                    continue
            for offset in range(0, len(candidates), 83):
                found = cmd.sl1_profile_check_keys(
                    sector_range(sector).start,
                    key_type,
                    candidates[offset : offset + 83],
                )
                if found is not None:
                    keys[name].update(
                        value=found.hex().upper(),
                        status="verified",
                        evidence="Supplied dictionary key authenticated",
                    )
                    break
        require_identity(cmd, result["hf14a"])
        for block in sector_range(sector):
            entry: dict[str, Any] = {
                "data": "??" * 16,
                "status": "unread",
                "key": None,
                "errors": [],
            }
            c["blocks"][block] = entry
            for name, key_type in (("A", MfcKeyType.A), ("B", MfcKeyType.B)):
                key = keys[name.lower()]
                if key["value"] is None:
                    continue
                try:
                    raw = bytes(
                        cmd.sl1_profile_read_block(
                            block, key_type, hex_bytes(key["value"], 6)
                        )
                    )
                    if len(raw) != 16:
                        raise ValueError("Read returned a non-16-byte block")
                except Exception as error:
                    status = getattr(error, "status", None)
                    entry["errors"].append(
                        {
                            "key": name,
                            "status": status if type(status) is int else None,
                            "reason": str(error),
                        }
                    )
                    entry["status"] = "error"
                    continue
                key.update(
                    status="verified", evidence="Classic authenticated block read"
                )
                text = [f"{byte:02X}" for byte in raw]
                if block == sector_range(sector).stop - 1:
                    text[:6] = ["??"] * 6
                    # Key A can read Key B for trailer C1C2C3 = 000, 001, 010.
                    readable_b = (
                        key_type == MfcKeyType.A
                        and access_valid(raw)
                        and (
                            ((raw[7] >> 7) & 1),
                            ((raw[8] >> 3) & 1),
                            ((raw[8] >> 7) & 1),
                        )
                        in ((0, 0, 0), (0, 0, 1), (0, 1, 0))
                    )
                    if readable_b:
                        keys["b"].update(
                            value=raw[10:].hex().upper(),
                            status="verified",
                            evidence="Readable trailer Key B",
                        )
                    if not readable_b:
                        text[10:] = ["??"] * 6
                entry.update(data="".join(text), status="read", key=name)
                break
        require_identity(cmd, result["hf14a"])
    return validate(result)


def begin_payload(profile: dict[str, Any]) -> bytes:
    p = validate(profile)
    h, a = p["hf14a"], p["cardAuth"]
    ats = hex_bytes(h["ats"]) if h["ats"] is not None else b""
    uid = hex_bytes(h["uid"])
    return (
        struct.pack(
            "!HB7s2sBBBBH16s",
            COUNTS[p["classic"]["type"]][2],
            len(uid),
            uid.ljust(7, b"\0"),
            hex_bytes(h["atqa"], 2),
            hex_bytes(h["sak"], 1)[0],
            len(ats),
            1 if a else 0,
            int(a["enabled"]) if a else 0,
            0x9004 if a else 0,
            hex_bytes(a["key"], 16) if a else bytes(16),
        )
        + ats
    )


class ProfileCommandError(RuntimeError):
    def __init__(self, status: int, reason: str):
        self.status = status
        super().__init__(reason)


class ReadError(ProfileCommandError):
    pass


class UploadError(RuntimeError):
    def __init__(self, phase: str, error: Exception):
        self.phase = phase
        self.status = getattr(error, "status", None)
        super().__init__(
            f"Profile upload failed during {phase}: {error}; no automatic commit or retry"
        )


def upload(
    cmd: Transport, slot: int, profile: dict[str, Any], stage_only: bool = False
) -> None:
    data = image(profile)
    payload = begin_payload(profile)
    if type(slot) is not int or not 0 <= slot <= 7:
        raise ValueError("Slot must be 0..7")
    if not COMMANDS.issubset(set(cmd.get_device_capabilities())):
        raise ValueError("Firmware lacks profile capabilities")
    phase = "tag mode" if not stage_only else "begin"
    try:
        if not stage_only:
            cmd.set_device_reader_mode(False)
        phase = "begin"
        cmd.sl1_profile_begin(slot, payload)
        for first in range(0, len(data) // 16, 8):
            phase = f"write block {first}"
            cmd.sl1_profile_write(slot, first, data[first * 16 : (first + 8) * 16])
        if not stage_only:
            phase = "save"
            cmd.sl1_profile_save(slot)
    except Exception as error:
        raise UploadError(phase, error) from error


def save_staged(cmd: Transport, slot: int) -> None:
    """Persist an explicitly staged profile after capability and mode checks."""
    if type(slot) is not int or not 0 <= slot <= 7:
        raise ValueError("Slot must be 0..7")
    if not COMMANDS.issubset(set(cmd.get_device_capabilities())):
        raise ValueError("Firmware lacks profile capabilities")
    phase = "tag mode"
    try:
        cmd.set_device_reader_mode(False)
        phase = "save"
        cmd.sl1_profile_save(slot)
    except Exception as error:
        raise UploadError(phase, error) from error
