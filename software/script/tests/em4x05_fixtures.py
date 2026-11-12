"""Synthetic protocol fixtures, never captured card/password material."""

import struct
from typing import Optional

from chameleon_com import Response
from chameleon_enum import Command, Status
from em4x05_backup import READ_CAPTURE_COMMAND
from em4x05_decode import SAMPLES, SUCCESS

CONFIG = 0x0001805F  # Manchester RF/64, public synthetic test value.


def frame_bits(word):
    # Manufacturer matrix: four LSB-first bytes, row parity, column XOR, stop.
    result = list(SUCCESS)
    columns = 0
    for byte_index in range(4):
        byte = (word >> (byte_index * 8)) & 255
        row = [(byte >> bit) & 1 for bit in range(8)]
        result.extend(row + [sum(row) % 2])
        columns ^= byte
    result.extend([(columns >> bit) & 1 for bit in range(8)] + [0])
    return result


def waveform(bits, encoding="manchester", rate=64, phase=17, inverted=False):
    samples = [1100] * phase
    level = False
    for bit in bits:
        if encoding == "manchester":
            levels = [bool(bit), not bool(bit)]
        else:
            level = not level
            first = level
            if bit == 0:
                level = not level
            levels = [first, level]
        for high in levels:
            samples.extend([1900 if high ^ inverted else 300] * (rate // 2))
    if len(samples) > SAMPLES:
        raise ValueError("fixture exceeds capture window")
    return tuple(samples + [1100] * (SAMPLES - len(samples)))


def packed_capture():
    return struct.pack("!BBHhh", 1, 4, SAMPLES, 300, 1900) + bytes([0x88]) * (
        SAMPLES // 2
    )


class CaptureDevice:
    def __init__(self):
        self.commands = [
            int(Command.GET_DEVICE_MODE),
            int(Command.CHANGE_DEVICE_MODE),
            READ_CAPTURE_COMMAND,
        ]
        self.mode = 0
        self.words = [i * 0x10203 for i in range(16)]
        self.words[0] = 9 << 1
        self.words[4] = CONFIG
        self.calls = []
        self.last_word = 0
        self.counts = {}
        self.unstable: Optional[int] = None
        self.fail_word: Optional[int] = None
        self.restore_fail = False
        self.cancellation = False

    def send_cmd_sync(self, cmd, data=None, status=0, timeout=3):
        self.calls.append((cmd, data))
        if cmd == Command.GET_DEVICE_MODE:
            return Response(cmd, Status.SUCCESS, bytes([self.mode]))
        if cmd == Command.CHANGE_DEVICE_MODE:
            assert data is not None
            if data == b"\x00" and self.restore_fail:
                raise OSError("cannot restore mode")
            self.mode = data[0]
            return Response(cmd, Status.SUCCESS)
        assert cmd == READ_CAPTURE_COMMAND and data is not None
        address, use_password, password = struct.unpack("!BBI", data)
        assert address != 2 and use_password in (0, 1)
        assert use_password or password == 0
        self.last_word = address
        self.counts[address] = self.counts.get(address, 0) + 1
        if self.cancellation:
            raise KeyboardInterrupt()
        if address == self.fail_word:
            return Response(cmd, Status.PAR_ERR)
        return Response(cmd, Status.LF_TAG_OK, packed_capture())

    def decoded(self, samples, **kwargs):
        word = self.words[self.last_word]
        if self.last_word == self.unstable and self.counts[self.last_word] % 2 == 0:
            word ^= 1
        return {"word": word}
