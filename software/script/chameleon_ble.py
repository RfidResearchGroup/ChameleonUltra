"""
Cross-platform BLE transport for the ChameleonUltra/Lite CLI.

The device exposes its command channel as a Nordic UART Service (NUS) over BLE.
This module wraps `bleak` (Windows -> WinRT, Linux -> BlueZ, macOS -> CoreBluetooth)
in a small object that presents the *same* minimal interface the CLI's serial
path already uses -- `read()`, `write()`, `close()`, `is_open`, `timeout` -- so
`ChameleonCom`'s existing receiver/transmitter threads drive it unchanged.

bleak is asyncio; the CLI is synchronous+threaded. We run one asyncio loop in a
dedicated background thread and bridge:
  RX (device->host): NUS TX-characteristic notifications -> a byte queue that
                     read() drains (pyserial semantics: up to `size` bytes,
                     blocking at most `timeout` seconds, b'' on timeout).
  TX (host->device): write() chunks to (ATT MTU - 3) and calls write_gatt_char.

bleak is imported lazily by the CLI only when a `ble:` port is requested, so it
stays an optional dependency.
"""

import asyncio
import queue
import threading
import time
import warnings
from typing import Optional

from bleak import BleakClient, BleakScanner

# Standard Nordic UART Service UUIDs (the firmware uses BLE_NUS_DEF / ble_nus).
NUS_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
NUS_RX_CHAR_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"  # host -> device (write)
NUS_TX_CHAR_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"  # device -> host (notify)

DEFAULT_NAME_PREFIX = "Chameleon"   # matches ChameleonUltra / ChameleonLite
SCAN_TIMEOUT = 8.0
CONNECT_TIMEOUT = 20.0

# BlueZ warns when it hasn't acquired the negotiated MTU yet; we acquire it
# in _negotiate_mtu(), but keep this as a cosmetic fallback.
warnings.filterwarnings("ignore", message="Using default MTU value")


class BLEConnectException(Exception):
    """BLE scan/connect failure."""


class BLESerialShim:
    """A pyserial-shaped facade over a BLE NUS link."""

    def __init__(self):
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        self._loop_thread: Optional[threading.Thread] = None
        self._client: Optional[BleakClient] = None
        self._rx_q: "queue.Queue[bytes]" = queue.Queue()
        self._leftover = bytearray()
        self._connected = threading.Event()
        self._mtu = 20                      # payload = ATT MTU - 3; refined after connect
        # pyserial-compatible attributes the CLI touches:
        self.timeout = 0.1
        self.dtr = True                     # no-op for BLE; present so serial code paths don't fail

    # ---- background asyncio loop ------------------------------------------
    def _start_loop(self):
        self._loop = asyncio.new_event_loop()
        self._loop_thread = threading.Thread(target=self._run_loop, daemon=True)
        self._loop_thread.start()

    def _run_loop(self):
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    def _submit(self, coro, timeout):
        """Run a coroutine on the loop thread and block for its result."""
        fut = asyncio.run_coroutine_threadsafe(coro, self._loop)
        return fut.result(timeout=timeout)

    # ---- connection -------------------------------------------------------
    def connect(self, target: str = ""):
        """
        target: "" -> scan and pick the first Chameleon
                "AA:BB:CC:DD:EE:FF" (or platform address) -> connect directly
                any other string -> case-insensitive device-name filter
        Raises BLEConnectException on failure. Blocks until connected.
        """
        self._start_loop()
        try:
            self._submit(self._connect(target.strip()), timeout=CONNECT_TIMEOUT + SCAN_TIMEOUT)
        except Exception as e:
            self.close()
            raise BLEConnectException(str(e))

    async def _resolve_address(self, target: str) -> str:
        looks_like_addr = target.count(":") >= 5 or "-" in target
        if target and looks_like_addr:
            return target
        found = await BleakScanner.discover(timeout=SCAN_TIMEOUT, return_adv=True)
        want = target.lower() if target else DEFAULT_NAME_PREFIX.lower()
        # Prefer name match; fall back to any advertiser exposing the NUS UUID.
        by_nus = None
        for _addr, (dev, adv) in found.items():
            name = (adv.local_name or dev.name or "")
            if want in name.lower():
                return dev.address
            uuids = [u.lower() for u in (adv.service_uuids or [])]
            if NUS_SERVICE_UUID in uuids and by_nus is None:
                by_nus = dev.address
        if by_nus is not None:
            return by_nus
        raise BLEConnectException(
            f"no BLE device matching '{target or DEFAULT_NAME_PREFIX}' found "
            f"(is it advertising / is BLE pairing set as you expect?)")

    async def _connect(self, target: str):
        address = await self._resolve_address(target)
        self._client = BleakClient(address, disconnected_callback=self._on_disconnect,
                                   timeout=CONNECT_TIMEOUT)
        await self._client.connect()
        await self._client.start_notify(NUS_TX_CHAR_UUID, self._on_notify)
        await self._negotiate_mtu()
        self._connected.set()

    async def _negotiate_mtu(self):
        # Set TX chunk size from the negotiated ATT MTU (payload = MTU - 3).
        # On the BlueZ backend the MTU isn't known until a characteristic is
        # acquired; _acquire_mtu() forces that (BlueZ-only, private, so guard
        # it). WinRT / CoreBluetooth expose mtu_size directly after connect.
        acquire = getattr(self._client, "_acquire_mtu", None)
        if acquire is not None:
            try:
                await acquire()
            except Exception:
                pass
        try:
            mtu = self._client.mtu_size
            if mtu and mtu > 3:
                self._mtu = mtu - 3
        except Exception:
            pass

    def _on_disconnect(self, _client):
        self._connected.clear()

    def _on_notify(self, _char, data: bytearray):
        # bleak delivers each NUS TX notification here on the loop thread.
        self._rx_q.put(bytes(data))

    # ---- pyserial-like surface -------------------------------------------
    @property
    def is_open(self) -> bool:
        return self._connected.is_set()

    def read(self, size: int = 1) -> bytes:
        """Return up to `size` bytes, blocking at most `self.timeout`; b'' on timeout."""
        out = bytearray()
        if self._leftover:
            take = min(size, len(self._leftover))
            out += self._leftover[:take]
            del self._leftover[:take]
        deadline = time.time() + (self.timeout or 0)
        while len(out) < size:
            remaining = deadline - time.time()
            if remaining <= 0:
                break
            try:
                chunk = self._rx_q.get(timeout=remaining)
            except queue.Empty:
                break
            need = size - len(out)
            out += chunk[:need]
            if len(chunk) > need:
                self._leftover += chunk[need:]
        return bytes(out)

    def write(self, data: bytes) -> int:
        if not self.is_open:
            raise BLEConnectException("BLE link is not open")
        self._submit(self._write(bytes(data)), timeout=max(5.0, self.timeout))
        return len(data)

    async def _write(self, data: bytes):
        for i in range(0, len(data), self._mtu):
            await self._client.write_gatt_char(NUS_RX_CHAR_UUID, data[i:i + self._mtu],
                                               response=False)

    def close(self):
        self._connected.clear()
        try:
            if self._client is not None and self._loop is not None and self._loop.is_running():
                try:
                    self._submit(self._disconnect(), timeout=5.0)
                except Exception:
                    pass
        finally:
            if self._loop is not None and self._loop.is_running():
                self._loop.call_soon_threadsafe(self._loop.stop)
            self._client = None

    async def _disconnect(self):
        try:
            await self._client.stop_notify(NUS_TX_CHAR_UUID)
        except Exception:
            pass
        await self._client.disconnect()
