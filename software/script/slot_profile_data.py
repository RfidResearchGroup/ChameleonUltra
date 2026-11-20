"""Immutable, observable Classic 1K slot state; no card transport is involved."""

from __future__ import annotations

import hashlib
from dataclasses import dataclass, field
from typing import Optional

CLASSIC_1K = 1001


class SlotProfileError(ValueError):
    """A validated slot operation could not complete."""


@dataclass(frozen=True)
class EmulatorConfig:
    detection: bool = False
    gen1a: bool = False
    gen2: bool = False
    block0: bool = False
    write_mode: int = 1
    field_reset: bool = False
    prng: int = 1


@dataclass(frozen=True)
class ClassicProfile:
    image: bytes = field(repr=False)
    uid: bytes
    atqa: bytes
    sak: bytes
    ats: bytes = b""
    config: EmulatorConfig = EmulatorConfig()

    @property
    def image_sha256(self) -> str:
        return hashlib.sha256(self.image).hexdigest()


@dataclass(frozen=True)
class SlotState:
    tag_type: int
    enabled: bool
    profile: Optional[ClassicProfile] = None


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
        raise SlotProfileError("slot must be an integer from 1 to 8")


def validate_profile(profile: ClassicProfile) -> None:
    if not isinstance(profile, ClassicProfile):
        raise SlotProfileError("invalid Classic profile")
    if not isinstance(profile.image, bytes) or len(profile.image) != 1024:
        raise SlotProfileError("Classic 1K memory must contain exactly 1024 bytes")
    for value, lengths, name in (
        (profile.uid, (4, 7, 10), "UID"),
        (profile.atqa, (2,), "ATQA"),
        (profile.sak, (1,), "SAK"),
    ):
        if not isinstance(value, bytes) or len(value) not in lengths:
            raise SlotProfileError(f"invalid saved {name}")
    if not isinstance(profile.ats, bytes) or len(profile.ats) > 254:
        raise SlotProfileError("invalid saved ATS")
    config = profile.config
    if not isinstance(config, EmulatorConfig):
        raise SlotProfileError("invalid emulator configuration")
    for name in ("detection", "gen1a", "gen2", "block0", "field_reset"):
        if type(getattr(config, name)) is not bool:
            raise SlotProfileError(f"{name} must be boolean")
    if type(config.write_mode) is not int or config.write_mode not in (0, 1, 2):
        # Shadow RAM is deliberately not serializable as a normal persisted
        # profile: existing firmware skips saving it on slot changes.
        raise SlotProfileError("Shadow and pending-Shadow profiles are unsupported")
    if type(config.prng) is not int or config.prng not in (0, 1, 2):
        raise SlotProfileError("invalid emulator PRNG type")


def validate_state(state: SlotState) -> None:
    if not isinstance(state, SlotState) or type(state.enabled) is not bool:
        raise SlotProfileError("invalid HF slot state")
    if type(state.tag_type) is not int or state.tag_type not in (0, CLASSIC_1K):
        raise SlotProfileError("only empty or Classic 1K HF slots are supported")
    if state.tag_type == 0:
        if state.enabled or state.profile is not None:
            raise SlotProfileError(
                "empty HF state must be disabled and have no profile"
            )
    else:
        if state.profile is None:
            raise SlotProfileError("Classic 1K state has no profile")
        validate_profile(state.profile)


def validate_snapshot(snapshot: SlotSnapshot) -> None:
    if not isinstance(snapshot, SlotSnapshot):
        raise SlotProfileError("invalid slot snapshot")
    validate_slot(snapshot.slot)
    if (
        not isinstance(snapshot.device_sha256, str)
        or len(snapshot.device_sha256) != 64
        or any(char not in "0123456789abcdef" for char in snapshot.device_sha256)
    ):
        raise SlotProfileError("invalid snapshot device fingerprint")
    validate_state(snapshot.state)


def validate_inventory(inventory: Inventory) -> None:
    if not isinstance(inventory, Inventory):
        raise SlotProfileError("invalid inventory")
    validate_slot(inventory.active_slot)
    if type(inventory.reader_mode) is not bool:
        raise SlotProfileError("invalid device mode")
    if not isinstance(inventory.slots, tuple) or len(inventory.slots) != 8:
        raise SlotProfileError("inventory must contain exactly eight slots")
    for status in inventory.slots:
        if not isinstance(status, SlotStatus):
            raise SlotProfileError("invalid slot descriptor")
        if any(
            type(value) is not int or not 0 <= value <= 65535
            for value in (status.hf, status.lf)
        ):
            raise SlotProfileError("invalid slot type")
        if type(status.hf_enabled) is not bool or type(status.lf_enabled) is not bool:
            raise SlotProfileError("invalid slot flags")
    validate_snapshot(SlotSnapshot(inventory.device_sha256, 1, SlotState(0, False)))
