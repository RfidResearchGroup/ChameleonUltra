"""
hf14a_trace_io.py — trace capture I/O for chameleon_cli_unit.py

Three additions to integrate into the existing CLI:

  1. Patch HF14ATrace.args_parser() to add --save / --no-decode
  2. Patch HF14ATrace.on_exec()  to write native file when --save given
  3. New class HF14ATraceLoad     (@hf_14a.command('trace-load'))
  4. New class HF14ATraceExportPm3 (@hf_14a.command('trace-export-pm3'))

Native file format ("CUSN" — Chameleon Ultra Sniff Native, v1):

    Offset  Size  Field          Notes
    0       4     magic          b'CUSN'
    4       1     version        0x01
    5       1     flags          bit0=directions present  bit1=parity stripped
    6       2     reserved       0x00 0x00
    8       4     buffer_len     little-endian uint32, length of buffer
    12      4     reserved2      0x00 0x00 0x00 0x00 (future: timestamp/clock)
    16      N     buffer         raw resp.data from hf14a_trace() (packed format
                                 already used internally: [2B bits+dir][N data]…)

Total minimum size = 16 bytes header + N bytes raw buffer. Trivially extendable
by bumping the version byte.

PM3 trace format (Iceman fork, observed via `trace save -f` output on a current
build, c.2025):

    Header (varies slightly by version; we write a 16-byte header that mirrors
    what `trace load -f` is willing to consume):

      4B  magic         b'PM3T'   (varies; Iceman accepts traces without this)
      4B  version       uint32 LE, currently 0x00000001
      4B  num_frames    uint32 LE
      4B  flags         uint32 LE, bit0 = HF (else LF)

    Per frame:
      4B  timestamp     uint32 LE, in 13.56 MHz / 16 = 847.5 kHz ticks (~1.18us)
      2B  duration      uint16 LE, also in ticks
      2B  length_resp   uint16 LE, bit15 = isResponse, bits14..0 = data length
      NB  data
      MB  parity        ceil(N/8) bytes, packed bits

If your target pm3 version expects a different header (Iceman vs RRG vs stock),
this is the function to adjust — search for write_pm3_header below.
"""

import os
import struct
from typing import List, Tuple


CUSN_MAGIC = b'CUSN'
CUSN_VERSION = 0x01
CUSN_HEADER_LEN = 16

# PM3 timing assumptions (no real timestamps from Chameleon — synthesized)
PM3_TICKS_PER_US = 13.56 / 16  # ticks per microsecond (~0.8475 ticks/us)
INTERFRAME_GAP_US = 200        # plausible ISO14443A inter-frame gap
DEFAULT_FRAME_DURATION_US = 100  # arbitrary, scaled by frame bit count


def write_cusn(path: str, buffer: bytes, has_directions: bool = True,
               parity_stripped: bool = False) -> int:
    """Write Chameleon Ultra Sniff Native v1 file. Returns total bytes."""
    flags = 0
    if has_directions:
        flags |= 0x01
    if parity_stripped:
        flags |= 0x02
    header = (CUSN_MAGIC
              + bytes([CUSN_VERSION, flags, 0, 0])
              + struct.pack('<I', len(buffer))
              + b'\x00\x00\x00\x00')
    with open(path, 'wb') as f:
        f.write(header)
        f.write(buffer)
    return len(header) + len(buffer)


def read_cusn(path: str) -> Tuple[bytes, bool, bool]:
    """Read CUSN file. Returns (buffer, has_directions, parity_stripped)."""
    with open(path, 'rb') as f:
        header = f.read(CUSN_HEADER_LEN)
        if len(header) < CUSN_HEADER_LEN:
            raise ValueError("File too short to be CUSN")
        if header[:4] != CUSN_MAGIC:
            raise ValueError(f"Bad magic: {header[:4]!r} (expected {CUSN_MAGIC!r})")
        version = header[4]
        if version != CUSN_VERSION:
            raise ValueError(f"Unsupported CUSN version: 0x{version:02X}")
        flags = header[5]
        has_directions = bool(flags & 0x01)
        parity_stripped = bool(flags & 0x02)
        (buffer_len,) = struct.unpack('<I', header[8:12])
        buffer = f.read(buffer_len)
        if len(buffer) != buffer_len:
            raise ValueError(f"Buffer truncated: got {len(buffer)}, "
                             f"expected {buffer_len}")
        return buffer, has_directions, parity_stripped


def parse_cusn_buffer(buffer: bytes, has_directions: bool = True):
    """Parse the packed sniff buffer into frames.

    Same logic as HF14ATrace.on_exec but factored out for reuse by load/export.

    Returns: list of (szBits, data, is_tx) tuples, parity stripped if present.
    """
    frames = []
    i = 0
    while i + 2 <= len(buffer):
        hdr = (buffer[i] << 8) | buffer[i + 1]
        i += 2
        if has_directions:
            is_tx = bool(hdr & 0x8000)
            szBits = hdr & 0x7FFF
        else:
            is_tx = False
            szBits = hdr
        if szBits == 0:
            break
        szBytes = (szBits + 7) // 8
        if i + szBytes > len(buffer):
            break
        raw = buffer[i:i + szBytes]
        i += szBytes

        # Strip parity bits if present (same condition as original parser)
        if szBits >= 8 and szBits % 9 == 0:
            n_bytes = szBits // 9
            all_bits = []
            for byte in raw:
                for b in range(8):
                    all_bits.append((byte >> b) & 1)
            stripped = []
            for nb in range(n_bytes):
                val = 0
                for b in range(8):
                    val |= all_bits[nb * 9 + b] << b
                stripped.append(val)
            data = bytes(stripped)
            szBits = n_bytes * 8
        else:
            data = raw

        frames.append((szBits, data, is_tx))
    return frames


def compute_parity_bytes(data: bytes) -> bytes:
    """Compute ISO14443A odd parity (one bit per data byte), packed LSB-first."""
    parity_bits = []
    for byte in data:
        # Odd parity: bit is set iff data byte has even number of 1s
        ones = bin(byte).count('1')
        parity_bits.append(0 if (ones & 1) else 1)
    out = bytearray((len(parity_bits) + 7) // 8)
    for i, bit in enumerate(parity_bits):
        out[i // 8] |= (bit & 1) << (i % 8)
    return bytes(out)


def write_pm3_trace(path: str, frames: List[Tuple[int, bytes, bool]],
                    magic: bytes = b'PM3T') -> int:
    """Write a pm3-compatible trace file from a list of (szBits, data, is_tx).

    Timestamps are synthesized (no real timing data from Chameleon).
    """
    out_frames = []
    cur_ts = 1000  # arbitrary starting offset in ticks

    for (szBits, data, is_tx) in frames:
        # Duration scaled roughly to frame length (very rough)
        duration_us = max(50, szBits * 10)
        duration_ticks = int(duration_us * PM3_TICKS_PER_US)
        ts_ticks = int(cur_ts)

        length = len(data)
        length_resp = length & 0x7FFF
        if is_tx:
            length_resp |= 0x8000  # bit15 = isResponse (card -> reader)

        parity = compute_parity_bytes(data)

        frame_bytes = (struct.pack('<I', ts_ticks)
                       + struct.pack('<H', duration_ticks)
                       + struct.pack('<H', length_resp)
                       + data
                       + parity)
        out_frames.append(frame_bytes)

        # Advance timestamp for next frame: this frame's duration + gap
        cur_ts += duration_ticks + int(INTERFRAME_GAP_US * PM3_TICKS_PER_US)

    flags = 0x00000001  # bit0 = HF trace
    header = (magic
              + struct.pack('<I', 1)              # version
              + struct.pack('<I', len(out_frames))  # num_frames
              + struct.pack('<I', flags))

    with open(path, 'wb') as f:
        f.write(header)
        for frame in out_frames:
            f.write(frame)

    return os.path.getsize(path)
