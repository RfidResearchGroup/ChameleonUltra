"""Independent stateful emulator for the existing slot command surface."""

from __future__ import annotations

import struct
from dataclasses import replace
from typing import Callable, Optional, Union

from chameleon_com import ChameleonCom, Response
from chameleon_enum import Command, Status
from slot_profile import REQUIRED_COMMANDS
from slot_profile_data import ClassicProfile, EmulatorConfig

DATA_SETTERS = {
    Command.SET_SLOT_TAG_TYPE,
    Command.SET_SLOT_DATA_DEFAULT,
    Command.DELETE_SLOT_SENSE_TYPE,
    Command.SET_SLOT_ENABLE,
    Command.MF1_WRITE_EMU_BLOCK_DATA,
    Command.HF14A_SET_ANTI_COLL_DATA,
    Command.MF1_SET_DETECTION_ENABLE,
    Command.MF1_SET_GEN1A_MODE,
    Command.MF1_SET_GEN2_MODE,
    Command.MF1_SET_BLOCK_ANTI_COLL_MODE,
    Command.MF1_SET_WRITE_MODE,
    Command.MF1_SET_FIELD_OFF_DO_RESET,
    Command.MF1_SET_PRNG_TYPE,
    Command.SLOT_DATA_CONFIG_SAVE,
}


def profile(seed: int = 1, config: Optional[EmulatorConfig] = None) -> ClassicProfile:
    uid = bytes((0x11, 0x22, 0x33, seed))
    bcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3]
    image = bytearray((i * 13 + seed) & 255 for i in range(1024))
    image[:8] = uid + bytes((bcc, 8, 4, 0))
    return ClassicProfile(
        bytes(image),
        uid,
        b"\x04\x00",
        b"\x08",
        config=config or EmulatorConfig(),
    )


class FakeSlots(ChameleonCom):
    def __init__(self):
        super().__init__()
        self.commands = list(REQUIRED_COMMANDS)
        self.opened = True
        self.model = 0
        self.identity = b"unit0001"
        self.active = 2
        self.reader = False
        self.types = [1001] * 4 + [0] * 4
        self.lf_types = [0] * 8
        self.enabled = [True] * 4 + [False] * 4
        self.lf_enabled = [False] * 8
        self.flash = {i: profile(i + 1, EmulatorConfig(write_mode=0)) for i in range(4)}
        self.ram = self.flash[self.active]
        self.calls = []
        self.fault: Optional[
            Callable[
                ["FakeSlots", int, Optional[bytes]],
                Optional[Union[BaseException, Response]],
            ]
        ] = None
        self.drop_persist = False
        self.logs = 0
        self.saved_active = self.active
        self.saved_types = tuple(self.types)
        self.saved_enabled = tuple(self.enabled)
        self.saves = []

    def isOpen(self):
        return self.opened

    def _persist(self):
        if self.types[self.active] == 1001 and self.ram is not None:
            if self.ram.config.write_mode not in (3, 4) and not self.drop_persist:
                self.flash[self.active] = self.ram

    def _switch(self, slot):
        self._persist()
        self.active = slot
        if self.types[slot] == 1001 and slot in self.flash:
            self.ram = self.flash[slot]

    def send_cmd_sync(
        self, cmd: int, data: Optional[bytes] = None, status: int = 0, timeout: int = 3
    ) -> Response:
        self.calls.append((cmd, data))
        if self.fault is not None:
            action = self.fault(self, cmd, data)
            if isinstance(action, BaseException):
                raise action
            if isinstance(action, Response):
                return action
        data = b"" if data is None else data
        result = b""
        if cmd == Command.GET_DEVICE_MODEL:
            result = bytes([self.model])
        elif cmd == Command.GET_DEVICE_CHIP_ID:
            result = self.identity
        elif cmd == Command.GET_DEVICE_MODE:
            result = bytes([self.reader])
        elif cmd == Command.CHANGE_DEVICE_MODE:
            self.reader = bool(data[0])
        elif cmd == Command.GET_ACTIVE_SLOT:
            result = bytes([self.active])
        elif cmd == Command.SET_ACTIVE_SLOT:
            self._switch(data[0])
        elif cmd == Command.GET_SLOT_INFO:
            result = b"".join(
                struct.pack("!HH", a, b) for a, b in zip(self.types, self.lf_types)
            )
        elif cmd == Command.GET_ENABLED_SLOTS:
            result = bytes(
                value for pair in zip(self.enabled, self.lf_enabled) for value in pair
            )
        elif cmd == Command.MF1_GET_DETECTION_COUNT:
            result = struct.pack("!I", self.logs)
        elif cmd == Command.MF1_GET_EMULATOR_CONFIG:
            c = self.ram.config
            result = bytes((c.detection, c.gen1a, c.gen2, c.block0, c.write_mode))
        elif cmd == Command.MF1_GET_FIELD_OFF_DO_RESET:
            result = bytes([self.ram.config.field_reset])
        elif cmd == Command.MF1_GET_PRNG_TYPE:
            result = bytes([self.ram.config.prng])
        elif cmd == Command.HF14A_GET_ANTI_COLL_DATA:
            p = self.ram
            # This getter exposes saved metadata, even when Block 0 supplies
            # the effective RF identity in the real firmware.
            result = (
                bytes([len(p.uid)])
                + p.uid
                + p.atqa
                + p.sak
                + bytes([len(p.ats)])
                + p.ats
            )
        elif cmd == Command.MF1_READ_EMU_BLOCK_DATA:
            start, count = data
            result = self.ram.image[start * 16 : (start + count) * 16]
        elif cmd == Command.SET_SLOT_DATA_DEFAULT:
            slot, kind = struct.unpack("!BH", data)
            assert kind == 1001
            self.flash[slot] = profile(0xEF, EmulatorConfig(write_mode=0))
            if slot == self.active:
                self.ram = self.flash[slot]
        elif cmd == Command.SET_SLOT_TAG_TYPE:
            slot, kind = struct.unpack("!BH", data)
            self.types[slot] = kind
            # Firmware also loads this slot's buffer when it is inactive.
            # The manager must select its target before issuing this command.
            self.ram = self.flash.get(slot, self.ram)
        elif cmd == Command.DELETE_SLOT_SENSE_TYPE:
            slot, sense = data
            assert sense == 2
            self.flash.pop(slot, None)
            self.types[slot] = 0
            self.enabled[slot] = False
        elif cmd == Command.SET_SLOT_ENABLE:
            slot, sense, enabled = data
            assert sense == 2
            self.enabled[slot] = bool(enabled)
            if not enabled and not self.lf_enabled[slot]:
                for step in range(1, 9):
                    candidate = (slot + step) % 8
                    if self.enabled[candidate] or self.lf_enabled[candidate]:
                        self._switch(candidate)
                        break
        elif cmd == Command.MF1_WRITE_EMU_BLOCK_DATA:
            block, payload = data[0], data[1:]
            start = block * 16
            self.ram = replace(
                self.ram,
                image=self.ram.image[:start]
                + payload
                + self.ram.image[start + len(payload) :],
            )
        elif cmd == Command.HF14A_SET_ANTI_COLL_DATA:
            size = data[0]
            self.ram = replace(
                self.ram,
                uid=data[1 : size + 1],
                atqa=data[size + 1 : size + 3],
                sak=data[size + 3 : size + 4],
                ats=data[size + 5 :],
            )
        elif cmd == Command.SLOT_DATA_CONFIG_SAVE:
            self._persist()
            self.saved_active = self.active
            self.saved_types = tuple(self.types)
            self.saved_enabled = tuple(self.enabled)
            self.saves.append(self.active)
        else:
            names: dict[int, str] = {
                Command.MF1_SET_DETECTION_ENABLE: "detection",
                Command.MF1_SET_GEN1A_MODE: "gen1a",
                Command.MF1_SET_GEN2_MODE: "gen2",
                Command.MF1_SET_BLOCK_ANTI_COLL_MODE: "block0",
                Command.MF1_SET_WRITE_MODE: "write_mode",
                Command.MF1_SET_FIELD_OFF_DO_RESET: "field_reset",
                Command.MF1_SET_PRNG_TYPE: "prng",
            }
            assert cmd in names, f"unexpected command {cmd}"
            name = names[cmd]
            value = data[0] if name in ("prng", "write_mode") else bool(data[0])
            if name == "detection":
                self.logs = 0
            self.ram = replace(
                self.ram, config=replace(self.ram.config, **{name: value})
            )
            if name == "prng":
                result = bytes([data[0]])
        return Response(cmd, Status.SUCCESS, result)

    def setters(self):
        return [(cmd, data) for cmd, data in self.calls if cmd in DATA_SETTERS]
