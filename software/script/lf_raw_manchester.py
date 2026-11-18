"""Strict import of basic, unprotected T55x7 Manchester transmissions."""

import re
import json
from dataclasses import dataclass


@dataclass(frozen=True)
class ManchesterFrame:
    data: bytes
    clock: int
    terminator: bool


@dataclass(frozen=True)
class RawManchesterRecord:
    clock: int
    bits: int
    data: bytes
    terminator: bool = False
    invert: bool = False

    def encode(self) -> bytes:
        if self.clock not in (32, 64) or not 1 <= self.bits <= 224:
            raise ValueError("Expected RF/32 or RF/64 and 1..224 bits")
        if self.terminator and self.invert:
            raise ValueError("Terminator and inversion cannot be combined")
        if len(self.data) != (self.bits + 7) // 8:
            raise ValueError("Payload length does not match bit count")
        if self.bits % 8 and self.data[-1] & ((1 << (8 - self.bits % 8)) - 1):
            raise ValueError("Unused payload bits must be zero")
        return bytes((1, self.clock, int(self.terminator) | (int(self.invert) << 1), self.bits)) + self.data.ljust(28, b"\0")

    @classmethod
    def decode(cls, raw: bytes):
        if len(raw) != 32 or raw[0] != 1 or raw[2] & ~3:
            raise ValueError("Invalid raw Manchester record header/length")
        record = cls(raw[1], raw[3], raw[4:4 + (raw[3] + 7) // 8], bool(raw[2] & 1), bool(raw[2] & 2))
        if record.encode() != raw:
            raise ValueError("Unused record bytes must be zero")
        return record


def load_record(path: str) -> RawManchesterRecord:
    """Read whole JSON dump or fixed binary record before contacting device."""
    with open(path, "rb") as source:
        raw = source.read()
    if raw.lstrip().startswith(b"{"):
        frame = import_t55x7(json.loads(raw))
        record = RawManchesterRecord(frame.clock, len(frame.data) * 8, frame.data, frame.terminator)
        record.encode()
        return record
    return RawManchesterRecord.decode(raw)


def access_slot(cmd, slot=None, record=None, force=False):
    """Non-transactional LF update; always restore active slot, never touch HF."""
    from chameleon_enum import Command, SlotNumber, TagSenseType, TagSpecificType

    required = {Command.RAW_MANCHESTER_GET, Command.SET_ACTIVE_SLOT}
    if record is not None:
        record.encode()
        required |= {Command.RAW_MANCHESTER_SET, Command.SET_ACTIVE_SLOT,
                     Command.SET_SLOT_TAG_TYPE, Command.SET_SLOT_DATA_DEFAULT,
                     Command.SET_SLOT_ENABLE, Command.SLOT_DATA_CONFIG_SAVE}
    capabilities = set(cmd.get_device_capabilities())
    if not required <= capabilities:
        raise ValueError("Firmware lacks raw Manchester/lifecycle capabilities; no changes made")
    previous = SlotNumber.from_fw(cmd.get_active_slot())
    target = SlotNumber(previous if slot is None else slot)
    kind = cmd.get_slot_info()[target - 1]["lf"]
    if record is not None and kind != TagSpecificType.UNDEFINED and not force:
        raise ValueError("LF slot occupied; explicit --force required")
    if record is None and kind != TagSpecificType.RAW_MANCHESTER:
        raise ValueError("Selected LF slot is not Raw Manchester")
    mutated = False
    try:
        if target != previous:
            cmd.set_active_slot(target)
        if record is not None:
            mutated = True
            cmd.set_slot_tag_type(target, TagSpecificType.RAW_MANCHESTER)
            cmd.set_slot_data_default(target, TagSpecificType.RAW_MANCHESTER)
            cmd.raw_manchester_set(record)
        actual = cmd.raw_manchester_get()
        if record is not None:
            if actual != record:
                raise ValueError("Raw Manchester readback mismatch")
            cmd.set_slot_enable(target, TagSenseType.LF, True)
            cmd.slot_data_config_save()
            if target != previous:
                cmd.set_active_slot(previous)
                cmd.slot_data_config_save()
        return actual
    except Exception as exc:
        if mutated:
            raise RuntimeError(f"LF update failed; partial changes may remain in RAM/flash: {exc}") from exc
        raise
    finally:
        if target != previous:
            try:
                cmd.set_active_slot(previous)
            except Exception as exc:
                raise RuntimeError("Active slot restoration failed; partial changes may remain") from exc


def import_t55x7(document: dict) -> ManchesterFrame:
    """Extract transmitted blocks, not factory metadata or unused memory."""
    if not isinstance(document, dict) or document.get("FileType") != "t55x7":
        raise ValueError("Expected a Proxmark t55x7 JSON dump")
    blocks = document.get("blocks")
    if not isinstance(blocks, dict):
        raise ValueError("Missing blocks")
    for key, value in blocks.items():
        if (not isinstance(key, str) or not key.isdecimal()
                or not isinstance(value, str)
                or not re.fullmatch(r"[0-9a-fA-F]{8}", value)):
            raise ValueError("Malformed block data")

    def word(index: int) -> bytes:
        value = blocks.get(str(index))
        if not isinstance(value, str) or not re.fullmatch(r"[0-9a-fA-F]{8}", value):
            raise ValueError(f"Invalid or missing block {index}")
        return bytes.fromhex(value)

    config = int.from_bytes(word(0), "big")
    # Accept only basic Manchester, RF/32 or RF/64, MAXBLK and optional ST.
    allowed = 0x001C0000 | 0x0001F000 | 0x000000E0 | 0x00000008
    if config & ~allowed or (config & 0x0001F000) != 0x00008000:
        raise ValueError("Unsupported T55x7 configuration")
    rate = (config >> 18) & 7
    if rate not in (2, 5):
        raise ValueError("Only RF/32 and RF/64 are supported")
    maximum = (config >> 5) & 7
    if not maximum:
        raise ValueError("MAXBLK=0 is not a user-data transmission")
    card = document.get("Card", {})
    if not isinstance(card, dict):
        raise ValueError("Invalid Card metadata")
    if "ConfigBlock" in card:
        metadata = card["ConfigBlock"]
        if not isinstance(metadata, str) or not re.fullmatch(r"[0-9a-fA-F]{8}", metadata):
            raise ValueError("Invalid configuration metadata")
        if int(metadata, 16) != config:
            raise ValueError("Conflicting configuration metadata")
    return ManchesterFrame(
        b"".join(word(i) for i in range(1, maximum + 1)),
        32 if rate == 2 else 64,
        bool(config & 8),
    )
