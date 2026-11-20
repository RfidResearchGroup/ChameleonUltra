"""Immutable complete MF0/NTAG emulator state, including private memory pages."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from typing import Optional

# tag type -> physical pages, complete emulator pages
PAGE_COUNTS = {
    1100: (45, 56),
    1101: (135, 146),
    1102: (231, 242),
    1103: (16, 16),
    1104: (48, 48),
    1105: (20, 33),
    1106: (41, 54),
    1107: (20, 31),
    1108: (41, 52),
}
READ_PROFILE = 4045
WRITE_PROFILE = 4046
CLASSIC_TYPES = (1000, 1001, 1002, 1003)


class NtagProfileError(ValueError):
    pass


@dataclass(frozen=True)
class NtagConfig:
    raw: bytes = b"\x02\x00\x00"  # write denied, no magic/detection/reserved state

    @property
    def write_mode(self) -> int:
        return (self.raw[0] >> 1) & 7


@dataclass(frozen=True)
class NtagProfile:
    tag_type: int
    image: bytes = field(repr=False)
    uid: bytes
    atqa: bytes
    sak: bytes
    ats: bytes = b""
    config: NtagConfig = NtagConfig()

    @property
    def image_sha256(self) -> str:
        return hashlib.sha256(self.image).hexdigest()


@dataclass(frozen=True)
class SlotState:
    tag_type: int
    enabled: bool
    profile: Optional[NtagProfile] = None


@dataclass(frozen=True)
class SlotStatus:
    hf: int
    lf: int
    hf_enabled: bool
    lf_enabled: bool


@dataclass(frozen=True)
class Inventory:
    device_sha256: str
    active_slot: int
    reader_mode: bool
    slots: tuple[SlotStatus, ...]


@dataclass(frozen=True)
class SlotSnapshot:
    device_sha256: str
    slot: int
    state: SlotState


@dataclass(frozen=True)
class SlotImportPlan:
    inventory: Inventory
    before: SlotSnapshot
    desired: SlotState


def validate_slot(slot: int) -> None:
    if type(slot) is not int or not 1 <= slot <= 8:
        raise NtagProfileError("slot must be an integer from 1 to 8")


def validate_profile(profile: NtagProfile) -> None:
    if not isinstance(profile, NtagProfile) or type(profile.tag_type) is not int:
        raise NtagProfileError("invalid NTAG profile")
    if profile.tag_type not in PAGE_COUNTS:
        raise NtagProfileError("unsupported MF0/NTAG type")
    if (
        type(profile.image) is not bytes
        or len(profile.image) != PAGE_COUNTS[profile.tag_type][1] * 4
    ):
        raise NtagProfileError("complete physical/private page image is required")
    for value, lengths, label in (
        (profile.uid, (4, 7, 10), "UID"),
        (profile.atqa, (2,), "ATQA"),
        (profile.sak, (1,), "SAK"),
    ):
        if type(value) is not bytes or len(value) not in lengths:
            raise NtagProfileError(f"invalid saved {label}")
    if type(profile.ats) is not bytes or len(profile.ats) > 254:
        raise NtagProfileError("invalid saved ATS")
    config = profile.config
    if (
        not isinstance(config, NtagConfig)
        or type(config.raw) is not bytes
        or len(config.raw) != 3
    ):
        raise NtagProfileError("invalid complete emulator configuration")
    if config.write_mode not in (0, 1, 2):
        raise NtagProfileError("Shadow and pending-Shadow profiles are unsupported")


def validate_state(state: SlotState) -> None:
    if (
        not isinstance(state, SlotState)
        or type(state.enabled) is not bool
        or type(state.tag_type) is not int
    ):
        raise NtagProfileError("invalid HF slot state")
    if state.tag_type == 0:
        if state.enabled or state.profile is not None:
            raise NtagProfileError(
                "empty HF state must be disabled and have no profile"
            )
    elif state.tag_type in PAGE_COUNTS and state.profile is not None:
        validate_profile(state.profile)
        if state.profile.tag_type != state.tag_type:
            raise NtagProfileError("profile and slot types differ")
    else:
        raise NtagProfileError("only empty or complete MF0/NTAG targets are supported")


def validate_snapshot(snapshot: SlotSnapshot) -> None:
    if not isinstance(snapshot, SlotSnapshot):
        raise NtagProfileError("invalid snapshot")
    validate_slot(snapshot.slot)
    digest = snapshot.device_sha256
    if (
        type(digest) is not str
        or len(digest) != 64
        or any(c not in "0123456789abcdef" for c in digest)
    ):
        raise NtagProfileError("invalid device fingerprint")
    validate_state(snapshot.state)


def validate_inventory(inventory: Inventory) -> None:
    if not isinstance(inventory, Inventory):
        raise NtagProfileError("invalid inventory")
    validate_slot(inventory.active_slot)
    if type(inventory.reader_mode) is not bool or len(inventory.slots) != 8:
        raise NtagProfileError("invalid inventory shape")
    for slot in inventory.slots:
        if (
            not isinstance(slot, SlotStatus)
            or any(
                type(v) is not int or not 0 <= v <= 65535 for v in (slot.hf, slot.lf)
            )
            or type(slot.hf_enabled) is not bool
            or type(slot.lf_enabled) is not bool
        ):
            raise NtagProfileError("invalid inventory entry")
