"""Verified complete MF0/NTAG slot transactions over versioned memory commands."""

from __future__ import annotations

import hashlib
import struct
from contextlib import contextmanager
from dataclasses import dataclass
from pathlib import Path
from typing import Iterator, Optional, Protocol, Sequence

from chameleon_com import Response
from chameleon_enum import Command, Status
from ntag_profile_data import (
    PAGE_COUNTS,
    CLASSIC_TYPES,
    READ_PROFILE,
    WRITE_PROFILE,
    NtagProfile,
    NtagConfig,
    Inventory,
    SlotImportPlan,
    NtagProfileError,
    SlotSnapshot,
    SlotState,
    SlotStatus,
    validate_inventory,
    validate_profile,
    validate_slot,
    validate_snapshot,
    validate_state,
)
from ntag_profile_io import check_new_path, load_snapshot, save_snapshot

REQUIRED_COMMANDS = frozenset(
    {
        Command.GET_DEVICE_MODEL,
        Command.GET_DEVICE_CHIP_ID,
        Command.GET_DEVICE_MODE,
        Command.CHANGE_DEVICE_MODE,
        Command.GET_ACTIVE_SLOT,
        Command.SET_ACTIVE_SLOT,
        Command.GET_SLOT_INFO,
        Command.GET_ENABLED_SLOTS,
        Command.SET_SLOT_TAG_TYPE,
        Command.SET_SLOT_DATA_DEFAULT,
        Command.SET_SLOT_ENABLE,
        Command.DELETE_SLOT_SENSE_TYPE,
        Command.SLOT_DATA_CONFIG_SAVE,
        Command.HF14A_GET_ANTI_COLL_DATA,
        Command.HF14A_SET_ANTI_COLL_DATA,
        Command.MF1_GET_EMULATOR_CONFIG,
        Command.MF1_GET_DETECTION_COUNT,
        Command.MF0_NTAG_GET_DETECTION_COUNT,
        Command.MF0_NTAG_GET_EMULATOR_CONFIG,
        READ_PROFILE,
        WRITE_PROFILE,
    }
)


class SlotTransport(Protocol):
    @property
    def commands(self) -> Sequence[int]: ...

    def send_cmd_sync(
        self,
        cmd: int,
        data: Optional[bytes] = None,
        status: int = 0,
        timeout: int = 3,
    ) -> Response: ...


@dataclass(frozen=True)
class ImportProgress:
    stage: str = "not started"
    possibly_changed: bool = False
    data_verified: bool = False
    backup_path: Optional[Path] = None


class NtagProfileManager:
    def __init__(self, device: SlotTransport):
        self.device = device
        self.progress = ImportProgress()
        self._used = False
        self._persist_context = False

    def _stage(
        self,
        stage: str,
        *,
        changed: Optional[bool] = None,
        verified: Optional[bool] = None,
        backup: Optional[Path] = None,
    ) -> None:
        old = self.progress
        self.progress = ImportProgress(
            stage,
            old.possibly_changed if changed is None else changed,
            old.data_verified if verified is None else verified,
            old.backup_path if backup is None else backup,
        )

    def _caps(self) -> None:
        if not REQUIRED_COMMANDS.issubset(self.device.commands):
            raise NtagProfileError("firmware lacks required slot profile commands")

    def _call(
        self, command: int, data: Optional[bytes] = None, length: Optional[int] = 0
    ) -> bytes:
        try:
            response = self.device.send_cmd_sync(command, data)
        except Exception as error:
            raise NtagProfileError(f"command {int(command)} failed: {error}") from error
        if (
            not isinstance(response.status, int)
            or isinstance(response.status, bool)
            or response.status != Status.SUCCESS
        ):
            raise NtagProfileError(f"command {int(command)} returned an error status")
        if not isinstance(response.data, bytes) or (
            length is not None and len(response.data) != length
        ):
            raise NtagProfileError(f"command {int(command)} returned malformed data")
        return response.data

    def _byte(self, command: Command, allowed: range) -> int:
        value = self._call(command, length=1)[0]
        if value not in allowed:
            raise NtagProfileError(f"command {int(command)} returned an invalid value")
        return value

    def _fingerprint(self) -> str:
        return hashlib.sha256(
            self._call(Command.GET_DEVICE_CHIP_ID, length=8)
        ).hexdigest()

    def _inventory(self) -> Inventory:
        self._caps()
        if self._byte(Command.GET_DEVICE_MODEL, range(2)) != 0:
            raise NtagProfileError("slot profile transactions currently require Ultra")
        types = list(
            struct.iter_unpack("!HH", self._call(Command.GET_SLOT_INFO, length=32))
        )
        flags = self._call(Command.GET_ENABLED_SLOTS, length=16)
        if any(value not in (0, 1) for value in flags):
            raise NtagProfileError("invalid slot enable flags")
        slots = tuple(
            SlotStatus(hf, lf, bool(flags[2 * i]), bool(flags[2 * i + 1]))
            for i, (hf, lf) in enumerate(types)
        )
        return Inventory(
            self._fingerprint(),
            self._byte(Command.GET_ACTIVE_SLOT, range(8)) + 1,
            bool(self._byte(Command.GET_DEVICE_MODE, range(2))),
            slots,
        )

    @staticmethod
    def _target(inventory: Inventory, slot: int) -> None:
        validate_slot(slot)
        if slot == inventory.active_slot:
            raise NtagProfileError("choose an inactive slot; current slot is protected")
        state = inventory.slots[slot - 1]
        if state.lf != 0 or state.lf_enabled:
            raise NtagProfileError("target must be HF-only with no LF state")
        if state.hf != 0 and state.hf not in PAGE_COUNTS:
            raise NtagProfileError("target must be empty or MF0/NTAG")
        if state.hf == 0 and state.hf_enabled:
            raise NtagProfileError("enabled empty target is inconsistent")

    def _active(self) -> int:
        return self._byte(Command.GET_ACTIVE_SLOT, range(8)) + 1

    def _mode(self) -> bool:
        return bool(self._byte(Command.GET_DEVICE_MODE, range(2)))

    def _select(self, slot: int) -> None:
        if self._active() != slot:
            self._call(Command.SET_ACTIVE_SLOT, bytes([slot - 1]))
        if self._active() != slot:
            raise NtagProfileError("active slot did not change as requested")

    def _set_mode(self, reader: bool) -> None:
        if self._mode() != reader:
            self._call(Command.CHANGE_DEVICE_MODE, bytes([reader]))
        if self._mode() != reader:
            raise NtagProfileError("device mode did not change as requested")

    def _quiet(self) -> None:
        for command in (
            Command.MF1_GET_DETECTION_COUNT,
            Command.MF0_NTAG_GET_DETECTION_COUNT,
        ):
            count = struct.unpack("!I", self._call(command, length=4))[0]
            if count:
                raise NtagProfileError(
                    "preserve authentication logs before slot transactions"
                )

    def _origin_guard(self, inventory: Inventory) -> None:
        tag_type = inventory.slots[inventory.active_slot - 1].hf
        self._quiet()
        if tag_type == 0:
            return
        if tag_type in CLASSIC_TYPES:
            flags = self._call(Command.MF1_GET_EMULATOR_CONFIG, length=5)
            if any(value not in (0, 1) for value in flags[:4]) or flags[4] not in (
                0,
                1,
                2,
            ):
                raise NtagProfileError(
                    "cannot leave original Classic Shadow/pending-Shadow state"
                )
            # Do not normalize or set legacy Classic PRNG values on the original slot.
        elif tag_type in PAGE_COUNTS:
            flags = self._call(Command.MF0_NTAG_GET_EMULATOR_CONFIG, length=3)
            if (
                flags[0] not in (0, 1)
                or flags[1] not in (0, 1)
                or flags[2] not in (0, 1, 2)
            ):
                raise NtagProfileError("cannot leave original MF0/NTAG Shadow state")
        else:
            raise NtagProfileError("current HF slot must be empty, Classic or MF0/NTAG")

    @contextmanager
    def _window(self, inventory: Inventory, slot: int) -> Iterator[None]:
        self._persist_context = False
        self._origin_guard(inventory)
        original: Optional[BaseException] = None
        try:
            self._set_mode(True)
            self._select(slot)
            yield
        except BaseException as error:
            original = error
            raise
        finally:
            errors = []
            restored = False
            try:
                if self._fingerprint() != inventory.device_sha256:
                    raise NtagProfileError(
                        "device identity changed during the operation"
                    )
                self._set_mode(True)
                self._select(inventory.active_slot)
                restored = True
            except BaseException as error:
                errors.append(error)
            persist_context = self._persist_context
            self._persist_context = False
            if restored and persist_context:
                try:
                    # Save only after selecting the original slot. Persisting
                    # the target as active would expose it after a reboot.
                    self._call(Command.SLOT_DATA_CONFIG_SAVE)
                except BaseException as error:
                    errors.append(error)
            if restored:
                try:
                    self._set_mode(inventory.reader_mode)
                except BaseException as error:
                    errors.append(error)
            # If slot restoration fails, leave Reader mode rather than expose
            # a partial or unintended target in Tag mode.
            if errors:
                message = "; ".join(
                    str(error) or type(error).__name__ for error in errors
                )
                if original is not None:
                    original.args = (
                        f"{original}; restoring host context also failed: {message}",
                    )
                    original.__dict__["context_errors"] = tuple(errors)
                else:
                    cancellation = next(
                        (error for error in errors if not isinstance(error, Exception)),
                        None,
                    )
                    if cancellation is not None:
                        # Ctrl-C during cleanup is still cancellation even if
                        # target verification already succeeded. Keep the
                        # complete outcome for the caller's progress report.
                        cancellation.args = (
                            f"host context restoration interrupted: {message}",
                        )
                        cancellation.__dict__["context_errors"] = tuple(errors)
                        raise cancellation
                    raise NtagProfileError(
                        f"restoring host context failed: {message}"
                    ) from errors[0]

    def _selected(self, slot: int) -> None:
        if not self._mode() or self._active() != slot:
            raise NtagProfileError(
                "slot/mode changed; stop concurrent readers or button use"
            )

    def _read_state(self, slot: int) -> SlotState:
        self._selected(slot)
        inventory = self._inventory()
        status = inventory.slots[slot - 1]
        if status.lf != 0 or status.lf_enabled:
            raise NtagProfileError("target LF state changed")
        if status.hf == 0:
            state = SlotState(0, status.hf_enabled)
        elif status.hf in PAGE_COUNTS:
            physical, total = PAGE_COUNTS[status.hf]
            data = self._call(READ_PROFILE, bytes([slot - 1]), length=11 + total * 4)
            if struct.unpack("!BBHHH", data[:8]) != (
                1,
                slot - 1,
                status.hf,
                physical,
                total,
            ):
                raise NtagProfileError(
                    "complete profile descriptor differs from target"
                )
            raw = self._call(Command.HF14A_GET_ANTI_COLL_DATA, length=None)
            if not raw or raw[0] not in (4, 7, 10):
                raise NtagProfileError("invalid saved UID length")
            size = raw[0]
            if len(raw) < size + 5 or len(raw) != size + 5 + raw[size + 4]:
                raise NtagProfileError("invalid anti-collision response")
            profile = NtagProfile(
                status.hf,
                data[11:],
                raw[1 : size + 1],
                raw[size + 1 : size + 3],
                raw[size + 3 : size + 4],
                raw[size + 5 :],
                NtagConfig(data[8:11]),
            )
            state = SlotState(status.hf, status.hf_enabled, profile)
        else:
            raise NtagProfileError("target changed to unsupported type")
        validate_state(state)
        self._selected(slot)
        self._quiet()
        return state

    def _snapshot(self, inventory: Inventory, slot: int) -> SlotSnapshot:
        first = self._read_state(slot)
        if self._read_state(slot) != first:
            raise NtagProfileError("target state changed between independent reads")
        return SlotSnapshot(inventory.device_sha256, slot, first)

    def _plan(
        self, slot: int, desired: SlotState, required_device: Optional[str] = None
    ) -> SlotImportPlan:
        validate_slot(slot)
        validate_state(desired)
        inventory = self._inventory()
        self._target(inventory, slot)
        if required_device is not None and required_device != inventory.device_sha256:
            raise NtagProfileError("backup belongs to another device")
        self._stage("capture target")
        with self._window(inventory, slot):
            before = self._snapshot(inventory, slot)
        if self._inventory() != inventory:
            raise NtagProfileError("device inventory changed during target inspection")
        return SlotImportPlan(inventory, before, desired)

    def plan_import(self, slot: int, profile: NtagProfile) -> SlotImportPlan:
        validate_profile(profile)
        return self._plan(slot, SlotState(profile.tag_type, True, profile))

    def plan_restore(self, snapshot: SlotSnapshot) -> SlotImportPlan:
        validate_snapshot(snapshot)
        return self._plan(snapshot.slot, snapshot.state, snapshot.device_sha256)

    def backup(self, slot: int, path: Path) -> SlotSnapshot:
        check_new_path(path)
        # The desired state is unused: this performs only inspected selection
        # and snapshot publication, never factory/data/config writes.
        plan = self._plan(slot, SlotState(0, False))
        save_snapshot(plan.before, path)
        return plan.before

    def _mutate(self, slot: int, command: int, data: bytes) -> None:
        self._selected(slot)
        self._stage(self.progress.stage, changed=True)
        self._call(command, data)
        self._selected(slot)

    def _install(self, slot: int, desired: SlotState) -> None:
        if desired.tag_type == 0:
            self._stage("remove imported HF state")
            self._mutate(slot, Command.DELETE_SLOT_SENSE_TYPE, bytes([slot - 1, 2]))
            return
        profile = desired.profile
        assert profile is not None
        self._stage("initialize target")
        self._mutate(
            slot,
            Command.SET_SLOT_DATA_DEFAULT,
            struct.pack("!BH", slot - 1, desired.tag_type),
        )
        self._mutate(
            slot,
            Command.SET_SLOT_TAG_TYPE,
            struct.pack("!BH", slot - 1, desired.tag_type),
        )
        physical, total = PAGE_COUNTS[desired.tag_type]
        self._stage("write complete physical/private page state and configuration")
        self._mutate(
            slot,
            WRITE_PROFILE,
            struct.pack("!BBHHH", 1, slot - 1, desired.tag_type, physical, total)
            + profile.config.raw
            + profile.image,
        )
        self._stage("write anti-collision metadata")
        self._mutate(
            slot,
            Command.HF14A_SET_ANTI_COLL_DATA,
            bytes([len(profile.uid)])
            + profile.uid
            + profile.atqa
            + profile.sak
            + bytes([len(profile.ats)])
            + profile.ats,
        )
        self._quiet()
        self._stage("set target HF enable", changed=True)
        self._selected(slot)
        self._call(Command.SET_SLOT_ENABLE, bytes([slot - 1, 2, desired.enabled]))
        if not self._mode():
            raise NtagProfileError("device unexpectedly left Reader mode")
        # Disabling an HF-only slot may auto-select a different slot in firmware.
        self._select(slot)

    def apply(self, plan: SlotImportPlan, backup_path: Path) -> None:
        if self._used:
            raise NtagProfileError("create a new manager and plan instead of retrying")
        self._used = True
        if not isinstance(plan, SlotImportPlan):
            raise NtagProfileError("invalid slot import plan")
        validate_snapshot(plan.before)
        validate_state(plan.desired)
        validate_slot(plan.before.slot)
        inventory = plan.inventory
        validate_inventory(inventory)
        if self._inventory() != inventory:
            raise NtagProfileError("slot import plan is stale")
        self._target(inventory, plan.before.slot)
        if plan.before.device_sha256 != inventory.device_sha256:
            raise NtagProfileError("plan belongs to another device")
        check_new_path(backup_path)
        slot = plan.before.slot
        self._stage("recheck target")
        try:
            with self._window(inventory, slot):
                if self._snapshot(inventory, slot) != plan.before:
                    raise NtagProfileError("target no longer matches the preview")
                if plan.desired != plan.before.state:
                    self._stage("publish target backup")
                    save_snapshot(plan.before, backup_path)
                    self._stage(
                        "validate published backup", backup=backup_path.absolute()
                    )
                    if load_snapshot(backup_path) != plan.before:
                        raise NtagProfileError("published backup differs from preview")
                    self._install(slot, plan.desired)
                    self._stage("reload target from storage")
                    self._select(inventory.active_slot)
                    self._select(slot)
                self._stage("verify complete target state")
                if self._snapshot(inventory, slot).state != plan.desired:
                    raise NtagProfileError(
                        "reloaded target differs from desired profile"
                    )
                current = self._inventory()
                expected = list(inventory.slots)
                expected[slot - 1] = SlotStatus(
                    plan.desired.tag_type, 0, plan.desired.enabled, False
                )
                if (
                    current.device_sha256 != inventory.device_sha256
                    or current.slots != tuple(expected)
                ):
                    raise NtagProfileError("unrelated slot state changed")
                self._stage(
                    "restore context and save slot configuration", verified=True
                )
                self._persist_context = self.progress.possibly_changed
            current = self._inventory()
            if (
                current.active_slot != inventory.active_slot
                or current.reader_mode != inventory.reader_mode
                or current.slots != tuple(expected)
            ):
                raise NtagProfileError("host context did not restore exactly")
            self._stage("complete")
        except BaseException as error:
            error.__dict__["slot_progress"] = self.progress
            raise
