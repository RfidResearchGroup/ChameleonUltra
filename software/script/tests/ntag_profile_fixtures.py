"""Stateful save-on-switch firmware fixture with synthetic private memory."""

from __future__ import annotations

import copy
import struct
from dataclasses import replace
from typing import Optional

from chameleon_com import Response
from chameleon_enum import Command, Status
from ntag_profile import REQUIRED_COMMANDS
from ntag_profile_data import (
    NtagConfig,
    NtagProfile,
    PAGE_COUNTS,
    READ_PROFILE,
    WRITE_PROFILE,
)


def profile(tag_type=1100, seed=19, raw_config=b"\xf3\xab\xcd"):
    total = PAGE_COUNTS[tag_type][1]
    return NtagProfile(
        tag_type,
        bytes((i + seed) % 256 for i in range(total * 4)),
        b"\x04\x11\x22\x33\x44\x55\x66",
        b"\x44\x00",
        b"\x00",
        b"",
        NtagConfig(raw_config),
    )


class FakeNtagDevice:
    def __init__(self):
        self.commands = list(REQUIRED_COMMANDS)
        self.active = 0
        self.mode = 0
        self.fingerprint = b"\x00\x11\x22\x33\x44\x55\x66\x77"
        self.types = [[1001, 0]] + [[0, 0] for _ in range(7)]
        self.enabled = [[True, False]] + [[False, False] for _ in range(7)]
        self.flash: dict[int, NtagProfile] = {}
        self.ram: Optional[NtagProfile] = None
        self.classic_flags = b"\x00\x00\x00\x00\x00"
        self.classic_prng = 3
        self.classic_log = 0
        self.ntag_log = 0
        self.calls = []
        self.global_saves = []
        self.fail_command: Optional[int] = None
        self.fail_nth = 1
        self.failure: Optional[BaseException] = None
        self.command_counts: dict[int, int] = {}
        self.corrupt_persistence = False
        self.corrupt_descriptor = False
        self.switch_after_write = False
        self.fail_origin_selection = False

    def populate(self, slot, tag_type=1100, enabled=True):
        value = profile(tag_type)
        self.types[slot][0] = tag_type
        self.enabled[slot][0] = enabled
        self.flash[slot] = value
        if slot == self.active:
            self.ram = copy.deepcopy(value)

    def _save_ram(self):
        if self.ram is not None and self.ram.config.write_mode < 3:
            value = copy.deepcopy(self.ram)
            if self.corrupt_persistence:
                # Corrupt a private page to catch verification that checks only card pages.
                value = replace(
                    value, image=value.image[:-1] + bytes([value.image[-1] ^ 1])
                )
            self.flash[self.active] = value

    def _select(self, slot):
        if slot == self.active:
            return
        if slot == 0 and self.fail_origin_selection:
            raise OSError("origin selection unavailable")
        self._save_ram()
        self.active = slot
        self.ram = copy.deepcopy(self.flash.get(slot))

    def send_cmd_sync(self, cmd, data=None, status=0, timeout=3):
        self.calls.append((cmd, data))
        self.command_counts[cmd] = self.command_counts.get(cmd, 0) + 1
        if cmd == self.fail_command and self.command_counts[cmd] == self.fail_nth:
            raise (
                self.failure
                if self.failure is not None
                else OSError("injected command failure")
            )
        payload = b""
        if cmd == Command.GET_DEVICE_MODEL:
            payload = b"\x00"
        elif cmd == Command.GET_DEVICE_CHIP_ID:
            payload = self.fingerprint
        elif cmd == Command.GET_DEVICE_MODE:
            payload = bytes([self.mode])
        elif cmd == Command.CHANGE_DEVICE_MODE:
            assert data is not None
            self.mode = data[0]
        elif cmd == Command.GET_ACTIVE_SLOT:
            payload = bytes([self.active])
        elif cmd == Command.SET_ACTIVE_SLOT:
            assert data is not None
            self._select(data[0])
        elif cmd == Command.GET_SLOT_INFO:
            payload = b"".join(struct.pack("!HH", *types) for types in self.types)
        elif cmd == Command.GET_ENABLED_SLOTS:
            payload = bytes(int(value) for flags in self.enabled for value in flags)
        elif cmd == Command.MF1_GET_EMULATOR_CONFIG:
            payload = self.classic_flags
        elif cmd == Command.MF0_NTAG_GET_EMULATOR_CONFIG:
            assert self.ram is not None
            flags = self.ram.config.raw[0]
            payload = bytes([(flags >> 4) & 1, flags & 1, (flags >> 1) & 7])
        elif cmd == Command.MF1_GET_DETECTION_COUNT:
            payload = struct.pack("!I", self.classic_log)
        elif cmd == Command.MF0_NTAG_GET_DETECTION_COUNT:
            payload = struct.pack("!I", self.ntag_log)
        elif cmd == READ_PROFILE:
            assert data is not None and self.mode == 1 and data[0] == self.active
            assert self.ram is not None
            kind = self.types[self.active][0]
            physical, total = PAGE_COUNTS[kind]
            payload = struct.pack("!BBHHH", 1, self.active, kind, physical, total)
            if self.corrupt_descriptor:
                payload = b"\x02" + payload[1:]
            payload += self.ram.config.raw + self.ram.image
        elif cmd == WRITE_PROFILE:
            assert data is not None and self.mode == 1 and self.ram is not None
            version, slot, kind, physical, total = struct.unpack("!BBHHH", data[:8])
            assert (version, slot, kind, physical, total) == (
                1,
                self.active,
                self.types[self.active][0],
                *PAGE_COUNTS[kind],
            )
            assert len(data) == 11 + total * 4
            self.ram = replace(self.ram, image=data[11:], config=NtagConfig(data[8:11]))
            if self.switch_after_write:
                self._select(0)
        elif cmd == Command.HF14A_GET_ANTI_COLL_DATA:
            assert self.ram is not None
            value = self.ram
            payload = (
                bytes([len(value.uid)])
                + value.uid
                + value.atqa
                + value.sak
                + bytes([len(value.ats)])
                + value.ats
            )
        elif cmd == Command.HF14A_SET_ANTI_COLL_DATA:
            assert self.ram is not None and data is not None
            size = data[0]
            self.ram = replace(
                self.ram,
                uid=data[1 : size + 1],
                atqa=data[size + 1 : size + 3],
                sak=data[size + 3 : size + 4],
                ats=data[size + 5 :],
            )
        elif cmd == Command.SET_SLOT_DATA_DEFAULT:
            assert data is not None
            slot, kind = struct.unpack("!BH", data)
            self.flash[slot] = profile(kind, seed=0, raw_config=b"\x00\x00\x00")
        elif cmd == Command.SET_SLOT_TAG_TYPE:
            assert data is not None
            slot, kind = struct.unpack("!BH", data)
            self.types[slot][0] = kind
            if slot == self.active:
                self.ram = copy.deepcopy(self.flash[slot])
        elif cmd == Command.SET_SLOT_ENABLE:
            assert data is not None
            slot, sense, enabled = data
            # Inventory order is HF, LF; protocol sense values are LF=1, HF=2.
            self.enabled[slot][0 if sense == 2 else 1] = bool(enabled)
            if slot == self.active and not any(self.enabled[slot]):
                self._select(
                    next(i for i, flags in enumerate(self.enabled) if any(flags))
                )
        elif cmd == Command.DELETE_SLOT_SENSE_TYPE:
            assert data is not None
            slot, sense = data
            assert sense == 2
            self.types[slot][0] = 0
            self.enabled[slot][0] = False
            self.flash.pop(slot, None)
            if self.active == slot:
                self.ram = None
        elif cmd == Command.SLOT_DATA_CONFIG_SAVE:
            self._save_ram()
            self.global_saves.append(self.active)
        else:
            raise AssertionError(f"unexpected command {cmd}")
        return Response(cmd, Status.SUCCESS, payload)
