"""Decode HF14A capture records without confusing RF bits with DMA bytes.

TX metadata describes hardware generation, not an independent RF measurement.
The original payload and register value are retained for diagnostic use.
"""

from dataclasses import dataclass
from typing import List, Optional, Tuple

TRACE_MAGIC = b"CU14\x01"
PARITY = 0x01
DISCARD_START = 0x02
CRC = 0x10


def _bits(data: bytes, count: int, start: int = 0) -> List[int]:
    return [(data[bit // 8] >> (bit % 8)) & 1
            for bit in range(start, start + count)]


def _pack(bits: List[int]) -> bytes:
    result = bytearray((len(bits) + 7) // 8)
    for index, bit in enumerate(bits):
        result[index // 8] |= bit << (index % 8)
    return bytes(result)


def _strip_parity(data: bytes, count: int) -> Tuple[int, bytes, List[int]]:
    bits = _bits(data, count)
    payload = [bit for index, bit in enumerate(bits) if index % 9 != 8]
    return len(payload), _pack(payload), bits[8::9]


def crc_a(data: bytes) -> bytes:
    crc = 0x6363
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc.to_bytes(2, "little")


def anticollision_response(request_bits: int, request: bytes,
                           reply_bits: int) -> Optional[Tuple[str, bool]]:
    """Use the preceding selection request to identify short UID replies."""
    if len(request) < 2 or request[0] not in (0x93, 0x95, 0x97):
        return None
    nvb = request[1]
    if not 0x20 <= nvb <= 0x60 or nvb & 0x0f > 7:
        return None
    known = ((nvb >> 4) - 2) * 8 + (nvb & 0x0f)
    if request_bits != 16 + known:
        return None
    expected = 40 - known
    level = (request[0] - 0x93) // 2 + 1
    if reply_bits != expected:
        return (f"ANTICOLL CL{level} response: {reply_bits} bits "
                f"(expected {expected})", False)
    kind = "BCC" if expected == 8 else "UID/BCC"
    return f"ANTICOLL CL{level}: {kind} response ({reply_bits} bits)", True


@dataclass(frozen=True)
class HF14ATraceFrame:
    bits: int
    raw: bytes
    is_tx: bool
    frame_config: Optional[int]

    def decoded(self) -> Tuple[int, bytes, List[int]]:
        """Return data bits/bytes and captured parity, where unambiguous.

        Legacy TX configuration is unknown, so its payload stays untouched.
        For TX with hardware CRC, return the calculated CRC with the logical
        data, matching what the assembler is configured to transmit.
        """
        config = self.frame_config
        if not self.is_tx:
            if config is None or not config & PARITY:
                return _strip_parity(self.raw, self.bits)
            return self.bits, _pack(_bits(self.raw, self.bits)), []
        if config is None:
            return self.bits, self.raw, []
        if not config & PARITY:
            if self.bits % 9 == 0:
                return _strip_parity(self.raw, self.bits)
            return self.bits, _pack(_bits(self.raw, self.bits)), []
        start = (-self.bits % 8) if config & DISCARD_START else 0
        data = _pack(_bits(self.raw, self.bits, start))
        count = self.bits
        if config & CRC:
            if count % 8:
                raise ValueError("Cannot decode hardware CRC for a partial data byte")
            data += crc_a(data)
            count += 16
        return count, data, []


def parse_hf14a_trace(data: bytes) -> List[HF14ATraceFrame]:
    metadata = data.startswith(TRACE_MAGIC)
    if data.startswith(b"CU14") and not metadata:
        raise ValueError("Unsupported HF14A capture version")
    offset = len(TRACE_MAGIC) if metadata else 0
    frames = []
    header_size = 3 if metadata else 2
    while offset < len(data):
        if offset + header_size > len(data):
            raise ValueError("Truncated HF14A capture header")
        header = int.from_bytes(data[offset:offset + 2], "big")
        count = header & 0x7fff
        config = data[offset + 2] if metadata else None
        offset += header_size
        size = (count + 7) // 8
        if count == 0 or offset + size > len(data):
            raise ValueError("Invalid or truncated HF14A capture payload")
        frames.append(HF14ATraceFrame(count, data[offset:offset + size],
                                    bool(header & 0x8000), config))
        offset += size
    return frames
