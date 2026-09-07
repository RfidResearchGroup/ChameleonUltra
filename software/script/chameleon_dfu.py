"""
Self-contained Nordic Secure DFU (serial/USB-CDC) controller.

Speaks the nRF5 SDK Secure DFU serial transport directly over pyserial, so
`hw flash` can push a DFU package without nrfutil or any external tool.

The protocol here mirrors the implementation the ChameleonUltraGUI / Sailfish
frontends use against this same bootloader (SLIP framing, PRN=0, Get Serial
MTU with a 2051-byte fallback, object select/create/write/checksum/execute,
running standard CRC-32). Transport only: the package is unpacked by the
caller and the bootloader validates the signature.
"""

import struct
import time
import zlib

import serial


# --- Secure DFU opcodes (nRF5 SDK nrf_dfu_serial / dfu_transport_serial) -----
class DfuOp:
    CREATE_OBJECT = 0x01
    SET_PRN = 0x02
    CALC_CHECKSUM = 0x03
    EXECUTE = 0x04
    READ_ERROR = 0x05
    SELECT_OBJECT = 0x06     # "read object" / select
    GET_SERIAL_MTU = 0x07
    WRITE_OBJECT = 0x08
    PING = 0x09
    RESPONSE = 0x60


# --- Secure DFU result codes -------------------------------------------------
DFU_RESULT = {
    0x00: "invalid code",
    0x01: "success",
    0x02: "opcode not supported",
    0x03: "invalid parameter",
    0x04: "insufficient resources",
    0x05: "invalid object",
    0x07: "unsupported object type",
    0x08: "operation not permitted",
    0x0A: "operation failed",
    0x0B: "extended error",
}

# Object types used by Secure DFU.
OBJ_TYPE_COMMAND = 0x01   # init packet (.dat)
OBJ_TYPE_DATA = 0x02      # firmware image (.bin)

# Nordic USB DFU VID/PID for the Chameleon bootloader.
DFU_VID = 0x1915
DFU_PID = 0x521F


class DFUError(Exception):
    pass


class DFUTransferError(DFUError):
    """Recoverable mid-transfer error (offset/CRC mismatch) -> retry object."""


# --- SLIP (RFC 1055) ---------------------------------------------------------
_SLIP_END = 0xC0
_SLIP_ESC = 0xDB
_SLIP_ESC_END = 0xDC
_SLIP_ESC_ESC = 0xDD


def slip_encode(data: bytes) -> bytes:
    out = bytearray()
    for b in data:
        if b == _SLIP_END:
            out += bytes((_SLIP_ESC, _SLIP_ESC_END))
        elif b == _SLIP_ESC:
            out += bytes((_SLIP_ESC, _SLIP_ESC_ESC))
        else:
            out.append(b)
    out.append(_SLIP_END)
    return bytes(out)


def slip_decode(data: bytes) -> bytes:
    out = bytearray()
    esc = False
    for b in data:
        if esc:
            if b == _SLIP_ESC_END:
                out.append(_SLIP_END)
            elif b == _SLIP_ESC_ESC:
                out.append(_SLIP_ESC)
            else:
                # protocol violation; drop the frame
                return b""
            esc = False
        elif b == _SLIP_ESC:
            esc = True
        elif b == _SLIP_END:
            break
        else:
            out.append(b)
    return bytes(out)


class DFUSerial:
    """Secure DFU over a raw serial port (the bootloader's USB CDC ACM)."""

    def __init__(self, port: str, timeout: float = 20.0):
        self.timeout = timeout
        self.mtu = 0
        self.prn = 0
        self.serial = serial.Serial(port=port, baudrate=115200, timeout=timeout)

    def close(self):
        try:
            self.serial.close()
        except Exception:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    # -- framed request/response --------------------------------------------
    def _read_packet(self) -> bytes:
        """Read one SLIP frame (up to the END byte) and decode it."""
        raw = bytearray()
        deadline = time.time() + self.timeout
        while time.time() < deadline:
            b = self.serial.read(1)
            if not b:
                continue
            raw.append(b[0])
            if b[0] == _SLIP_END and len(raw) > 1:
                return slip_decode(bytes(raw))
        raise DFUError("timeout waiting for DFU response")

    def send_cmd(self, opcode: int, data: bytes = b"") -> bytes:
        self.serial.reset_input_buffer()
        self.serial.write(slip_encode(bytes((opcode,)) + data))
        resp = self._read_packet()
        if len(resp) < 3 or resp[0] != DfuOp.RESPONSE:
            raise DFUError(f"malformed DFU response: {resp.hex()}")
        if resp[1] != opcode:
            raise DFUTransferError(
                f"unexpected DFU response opcode 0x{resp[1]:02x} (sent 0x{opcode:02x})")
        result = resp[2]
        if result == 0x01:  # success
            return resp[3:]
        if result == 0x0B and len(resp) > 3:  # extended error
            raise DFUError(f"DFU extended error 0x{resp[3]:02x}")
        raise DFUError(f"DFU error: {DFU_RESULT.get(result, hex(result))}")

    # -- primitives ---------------------------------------------------------
    def set_prn(self, prn: int = 0):
        self.prn = prn
        self.send_cmd(DfuOp.SET_PRN, struct.pack("<H", prn))

    def get_mtu(self) -> int:
        try:
            resp = self.send_cmd(DfuOp.GET_SERIAL_MTU)
            self.mtu = struct.unpack("<H", resp[:2])[0]
        except DFUError:
            self.mtu = 2051
        if self.mtu == 0:
            self.mtu = 2051
        return self.mtu

    def select_object(self, obj_type: int):
        resp = self.send_cmd(DfuOp.SELECT_OBJECT, bytes((obj_type,)))
        max_size, offset, crc = struct.unpack("<III", resp[:12])
        return max_size, offset, crc

    def create_object(self, obj_type: int, size: int):
        self.send_cmd(DfuOp.CREATE_OBJECT, bytes((obj_type,)) + struct.pack("<I", size))

    def calculate_checksum(self):
        resp = self.send_cmd(DfuOp.CALC_CHECKSUM)
        offset, crc = struct.unpack("<II", resp[:8])
        return offset, crc

    def execute(self):
        self.send_cmd(DfuOp.EXECUTE)

    # -- transfer -----------------------------------------------------------
    def _write_object(self, chunk: bytes, crc: int, offset: int):
        """Stream one create-object window; validate offset+CRC at the end.

        With PRN=0 there are no intermediate receipts, so we only checksum
        once per object window (matches the reference flasher).
        """
        # SLIP worst case doubles every byte, and one byte is the opcode:
        # keep each written frame's payload within (mtu-1)//2 - 1.
        step = max(1, (self.mtu - 1) // 2 - 1)
        for i in range(0, len(chunk), step):
            part = chunk[i:i + step]
            self.serial.write(slip_encode(bytes((DfuOp.WRITE_OBJECT,)) + part))
            offset += len(part)
            crc = zlib.crc32(part, crc) & 0xFFFFFFFF
        recv_offset, recv_crc = self.calculate_checksum()
        if recv_offset != offset:
            raise DFUTransferError(f"offset mismatch: expected {offset}, got {recv_offset}")
        if recv_crc != crc:
            raise DFUTransferError(
                f"CRC mismatch: expected 0x{crc:08x}, got 0x{recv_crc:08x}")
        return crc

    def flash_object(self, obj_type: int, data: bytes, progress=None, retries: int = 10):
        """Program one Secure DFU object (init packet or firmware image)."""
        max_size, _, _ = self.select_object(obj_type)
        if max_size == 0:
            max_size = len(data) or 1
        crc = 0
        sent = 0
        for offset in range(0, len(data), max_size):
            window = data[offset:offset + max_size]
            crc_backup = crc
            for attempt in range(retries):
                self.create_object(obj_type, len(window))
                try:
                    crc = self._write_object(window, crc, offset)
                except DFUTransferError:
                    # re-select to resync and retry this window
                    self.select_object(obj_type)
                    crc = crc_backup
                    continue
                self.execute()
                break
            else:
                raise DFUError(f"unable to program object at offset {offset}")
            sent += len(window)
            if progress:
                progress(min(100, round(sent * 100 / len(data))))


def find_dfu_port():
    """Return the serial device path of a Chameleon in DFU mode, or None."""
    import serial.tools.list_ports as list_ports
    for p in list_ports.comports():
        if p.vid == DFU_VID and p.pid == DFU_PID:
            return p.device
    return None


def wait_for_dfu_port(timeout: float = 30.0, poll: float = 0.25):
    """Block until the DFU USB device enumerates; return its port path."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        port = find_dfu_port()
        if port:
            # give the CDC endpoint a moment to be openable after enumeration
            time.sleep(0.3)
            return port
        time.sleep(poll)
    return None


def unpack_dfu_zip(path: str):
    """Extract (init_packet, firmware_image) from a Nordic DFU package .zip.

    Chameleon packages name the members application.dat / application.bin.
    Falls back to the manifest.json if the names ever differ.
    """
    import json
    import zipfile

    with zipfile.ZipFile(path) as zf:
        names = set(zf.namelist())
        dat_name, bin_name = "application.dat", "application.bin"
        if dat_name not in names or bin_name not in names:
            if "manifest.json" in names:
                manifest = json.loads(zf.read("manifest.json"))
                app = manifest.get("manifest", {}).get("application", {})
                dat_name = app.get("dat_file", dat_name)
                bin_name = app.get("bin_file", bin_name)
            else:
                raise DFUError(
                    "not a Chameleon DFU package (no application.dat/.bin or manifest.json)")
        return zf.read(dat_name), zf.read(bin_name)


def flash_package(dat: bytes, bin_: bytes, port: str, progress=None, timeout: float = 20.0):
    """Run the full Secure DFU sequence against a device already in DFU mode."""
    if not dat or not bin_:
        raise DFUError("empty init packet or firmware image")
    dfu = DFUSerial(port, timeout=timeout)
    try:
        dfu.set_prn(0)
        dfu.get_mtu()
        dfu.flash_object(OBJ_TYPE_COMMAND, dat, progress)   # init packet
        dfu.flash_object(OBJ_TYPE_DATA, bin_, progress)     # firmware
    finally:
        dfu.close()