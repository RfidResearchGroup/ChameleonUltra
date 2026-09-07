"""
Self-contained Nordic Secure DFU controller with pluggable transports.

Speaks the nRF5 SDK Secure DFU object protocol directly, so `hw flash` can
push a DFU package without nrfutil or any external tool, over either:

  * Serial / USB-CDC  -> SerialTransport   (SLIP framing, Get-Serial-MTU)
  * BLE               -> BleTransport       (Nordic DFU service 0xFE59, bleak)

The object-level state machine (select/create/write/checksum/execute, running
CRC-32, PRN, retry) is identical on both and lives in SecureDFU; only framing
and the physical read/write differ per transport. This mirrors the flasher the
ChameleonUltraGUI / Sailfish frontends use against this same bootloader. The
package is unpacked by the caller and the bootloader validates the signature.
"""

import struct
import time
import zlib


# --- Secure DFU opcodes (nRF5 SDK nrf_dfu_req_handler) ------------------------
class DfuOp:
    CREATE_OBJECT = 0x01
    SET_PRN = 0x02
    CALC_CHECKSUM = 0x03
    EXECUTE = 0x04
    SELECT_OBJECT = 0x06
    GET_SERIAL_MTU = 0x07   # serial transport only
    WRITE_OBJECT = 0x08     # serial transport only (BLE writes the packet char)
    RESPONSE = 0x60


DFU_RESULT = {
    0x00: "invalid code",
    0x01: "success",
    0x02: "opcode not supported",
    0x03: "invalid parameter",
    0x04: "insufficient resources",
    0x05: "invalid object",
    0x06: "invalid signature",
    0x07: "unsupported object type",
    0x08: "operation not permitted",
    0x0A: "operation failed",
    0x0B: "extended error",
}

OBJ_TYPE_COMMAND = 0x01   # init packet (.dat)
OBJ_TYPE_DATA = 0x02      # firmware image (.bin)

# Nordic USB DFU (serial) VID/PID for the Chameleon bootloader.
DFU_VID = 0x1915
DFU_PID = 0x521F

# Nordic Secure DFU BLE service + characteristics.
DFU_SERVICE_UUID = "0000fe59-0000-1000-8000-00805f9b34fb"
DFU_CONTROL_UUID = "8ec90001-f315-4f60-9fb8-838830daea50"  # commands + notify
DFU_PACKET_UUID = "8ec90002-f315-4f60-9fb8-838830daea50"   # object data


class DFUError(Exception):
    pass


class DFUTransferError(DFUError):
    """Recoverable mid-transfer error (offset/CRC mismatch) -> retry object."""


def check_response(resp: bytes, opcode: int) -> bytes:
    """Validate a decoded control-point response, return its payload or raise."""
    if len(resp) < 3 or resp[0] != DfuOp.RESPONSE:
        raise DFUError(f"malformed DFU response: {resp.hex()}")
    if resp[1] != opcode:
        raise DFUTransferError(
            f"unexpected DFU response opcode 0x{resp[1]:02x} (sent 0x{opcode:02x})")
    result = resp[2]
    if result == 0x01:
        return resp[3:]
    if result == 0x0B and len(resp) > 3:  # extended error
        raise DFUError(f"DFU extended error 0x{resp[3]:02x}")
    raise DFUError(f"DFU error: {DFU_RESULT.get(result, hex(result))}")


# --- SLIP (RFC 1055), serial transport only ----------------------------------
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
                return b""  # protocol violation; drop frame
            esc = False
        elif b == _SLIP_ESC:
            esc = True
        elif b == _SLIP_END:
            break
        else:
            out.append(b)
    return bytes(out)


# --- transports --------------------------------------------------------------
class Transport:
    """Interface the SecureDFU controller drives."""

    def command(self, opcode: int, data: bytes = b"") -> bytes:
        """Send a control-point command, return its success payload or raise."""
        raise NotImplementedError

    def write_data(self, chunk: bytes) -> None:
        """Send one chunk of object data (no response expected)."""
        raise NotImplementedError

    def data_chunk_size(self) -> int:
        """Max bytes per write_data() call for this transport."""
        raise NotImplementedError

    def prepare(self) -> None:
        """Optional negotiation after connect / after SET_PRN."""

    def close(self) -> None:
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


class SerialTransport(Transport):
    """Secure DFU over the bootloader's USB CDC ACM port (SLIP framing)."""

    def __init__(self, port: str, timeout: float = 20.0):
        import serial
        self.timeout = timeout
        self._chunk = None
        self.serial = serial.Serial(port=port, baudrate=115200, timeout=timeout)

    def _read_packet(self) -> bytes:
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

    def command(self, opcode: int, data: bytes = b"") -> bytes:
        self.serial.reset_input_buffer()
        self.serial.write(slip_encode(bytes((opcode,)) + data))
        return check_response(self._read_packet(), opcode)

    def write_data(self, chunk: bytes) -> None:
        self.serial.write(slip_encode(bytes((DfuOp.WRITE_OBJECT,)) + chunk))

    def data_chunk_size(self) -> int:
        if self._chunk is None:
            try:
                mtu = struct.unpack("<H", self.command(DfuOp.GET_SERIAL_MTU)[:2])[0]
            except DFUError:
                mtu = 2051
            if mtu == 0:
                mtu = 2051
            # SLIP worst case doubles every byte and one byte is the opcode.
            self._chunk = max(1, (mtu - 1) // 2 - 1)
        return self._chunk

    def prepare(self) -> None:
        self.data_chunk_size()  # negotiate serial MTU up front

    def close(self) -> None:
        try:
            self.serial.close()
        except Exception:
            pass


class BleTransport(Transport):
    """Secure DFU over the Nordic DFU service (0xFE59) using bleak.

    Commands + response notifications ride the Control Point characteristic;
    object data is written to the Packet characteristic with no response and no
    SLIP framing. Chunk size follows the ATT MTU. PRN is kept at 0 (final CRC
    only), so a small per-write delay paces the write-without-response stream.
    """

    def __init__(self, address: str = None, name: str = None,
                 scan_timeout: float = 30.0, timeout: float = 20.0,
                 chunk_delay: float = 0.005):
        try:
            import bleak  # noqa: F401
        except ImportError:
            raise DFUError("BLE DFU needs the 'bleak' package (pip install bleak)")
        import asyncio
        import threading

        self._asyncio = asyncio
        self.timeout = timeout
        self.chunk_delay = chunk_delay
        self._client = None
        self._att_mtu = 23
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(target=self._loop.run_forever, daemon=True)
        self._thread.start()
        self._notif_q = None  # created on the loop
        self._connect(address, name, scan_timeout)

    def _run(self, coro, timeout=None):
        fut = self._asyncio.run_coroutine_threadsafe(coro, self._loop)
        return fut.result(timeout if timeout is not None else self.timeout + 5)

    def _connect(self, address, name, scan_timeout):
        from bleak import BleakScanner, BleakClient

        async def _do():
            self._notif_q = self._asyncio.Queue()
            dev = None
            if address:
                dev = await BleakScanner.find_device_by_address(
                    address, timeout=scan_timeout)
            else:
                def _match(d, adv):
                    if name and (d.name or "") != name:
                        return False
                    uuids = [u.lower() for u in (adv.service_uuids or [])]
                    return DFU_SERVICE_UUID.lower() in uuids or "fe59" in uuids
                dev = await BleakScanner.find_device_by_filter(
                    _match, timeout=scan_timeout)
            if dev is None:
                raise DFUError("no BLE device advertising the DFU service (0xFE59) found")
            client = BleakClient(dev)
            await client.connect()

            def _on_notify(_char, data):
                self._loop.call_soon_threadsafe(self._notif_q.put_nowait, bytes(data))

            await client.start_notify(DFU_CONTROL_UUID, _on_notify)
            self._client = client
            try:
                self._att_mtu = client.mtu_size or 23
            except Exception:
                self._att_mtu = 23

        self._run(_do(), timeout=scan_timeout + 15)

    def command(self, opcode: int, data: bytes = b"") -> bytes:
        async def _do():
            while not self._notif_q.empty():
                self._notif_q.get_nowait()
            await self._client.write_gatt_char(
                DFU_CONTROL_UUID, bytes((opcode,)) + data, response=True)
            resp = await self._asyncio.wait_for(self._notif_q.get(), self.timeout)
            return check_response(resp, opcode)
        return self._run(_do())

    def write_data(self, chunk: bytes) -> None:
        async def _do():
            await self._client.write_gatt_char(DFU_PACKET_UUID, chunk, response=False)
            if self.chunk_delay:
                await self._asyncio.sleep(self.chunk_delay)
        self._run(_do())

    def data_chunk_size(self) -> int:
        return max(1, self._att_mtu - 3)

    def close(self) -> None:
        try:
            if self._client is not None:
                self._run(self._client.disconnect(), timeout=10)
        except Exception:
            pass
        try:
            self._loop.call_soon_threadsafe(self._loop.stop)
        except Exception:
            pass


# --- controller --------------------------------------------------------------
class SecureDFU:
    """Transport-agnostic Nordic Secure DFU object state machine."""

    def __init__(self, transport: Transport, retries: int = 10):
        self.t = transport
        self.retries = retries

    def set_prn(self, prn: int = 0):
        self.t.command(DfuOp.SET_PRN, struct.pack("<H", prn))

    def select_object(self, obj_type: int):
        resp = self.t.command(DfuOp.SELECT_OBJECT, bytes((obj_type,)))
        max_size, offset, crc = struct.unpack("<III", resp[:12])
        return max_size, offset, crc

    def create_object(self, obj_type: int, size: int):
        self.t.command(DfuOp.CREATE_OBJECT, bytes((obj_type,)) + struct.pack("<I", size))

    def calculate_checksum(self):
        resp = self.t.command(DfuOp.CALC_CHECKSUM)
        offset, crc = struct.unpack("<II", resp[:8])
        return offset, crc

    def execute(self):
        self.t.command(DfuOp.EXECUTE)

    def _stream(self, chunk: bytes, crc: int, offset: int) -> int:
        step = self.t.data_chunk_size()
        for i in range(0, len(chunk), step):
            part = chunk[i:i + step]
            self.t.write_data(part)
            offset += len(part)
            crc = zlib.crc32(part, crc) & 0xFFFFFFFF
        recv_offset, recv_crc = self.calculate_checksum()
        if recv_offset != offset:
            raise DFUTransferError(f"offset mismatch: expected {offset}, got {recv_offset}")
        if recv_crc != crc:
            raise DFUTransferError(
                f"CRC mismatch: expected 0x{crc:08x}, got 0x{recv_crc:08x}")
        return crc

    def flash_object(self, obj_type: int, data: bytes, progress=None):
        max_size, _, _ = self.select_object(obj_type)
        if max_size == 0:
            max_size = len(data) or 1
        crc = 0
        sent = 0
        for offset in range(0, len(data), max_size):
            window = data[offset:offset + max_size]
            crc_backup = crc
            for _ in range(self.retries):
                self.create_object(obj_type, len(window))
                try:
                    crc = self._stream(window, crc, offset)
                except DFUTransferError:
                    self.select_object(obj_type)  # resync
                    crc = crc_backup
                    continue
                self.execute()
                break
            else:
                raise DFUError(f"unable to program object at offset {offset}")
            sent += len(window)
            if progress:
                progress(min(100, round(sent * 100 / len(data))))

    def flash(self, dat: bytes, bin_: bytes, progress=None):
        if not dat or not bin_:
            raise DFUError("empty init packet or firmware image")
        self.set_prn(0)
        self.t.prepare()
        self.flash_object(OBJ_TYPE_COMMAND, dat, progress)   # init packet
        self.flash_object(OBJ_TYPE_DATA, bin_, progress)     # firmware


# --- serial device discovery -------------------------------------------------
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
            time.sleep(0.3)  # let the CDC endpoint settle after enumeration
            return port
        time.sleep(poll)
    return None


# --- transport factories -----------------------------------------------------
def serial_transport(port: str, timeout: float = 20.0) -> SerialTransport:
    return SerialTransport(port, timeout=timeout)


def ble_transport(address: str = None, name: str = None,
                  scan_timeout: float = 30.0, timeout: float = 20.0) -> BleTransport:
    return BleTransport(address=address, name=name,
                        scan_timeout=scan_timeout, timeout=timeout)


# --- package handling --------------------------------------------------------
def unpack_dfu_zip(path: str):
    """Extract (init_packet, firmware_image) from a Nordic DFU package .zip."""
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


def flash_package(dat: bytes, bin_: bytes, transport: Transport, progress=None):
    """Run the full Secure DFU sequence over an already-connected transport."""
    SecureDFU(transport).flash(dat, bin_, progress)
