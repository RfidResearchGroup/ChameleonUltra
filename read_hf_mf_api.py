"""
read_hf_mf_api.py

API-only helper to interact with Chameleon Ultra for HF / Mifare Classic operations
(no interactive CLI). Use this module from Python code to programmatically scan tags,
check auth, and read blocks.

Example:
    from read_hf_mf_api import ChameleonHFReader, MfcKeyType

    reader = ChameleonHFReader()  # auto-detect and connect to first available device
    tags = reader.scan_tags()
    if tags:
        # try to detect mf1 support
        if reader.detect_mf1():
            ok = reader.auth_block(0, MfcKeyType.A, bytes.fromhex('FFFFFFFFFFFF'))
            if ok:
                data = reader.read_block(0, MfcKeyType.A, bytes.fromhex('FFFFFFFFFFFF'))
                print(data.hex())
    reader.close()

"""

from __future__ import annotations

import serial.tools.list_ports
from typing import Optional, List, Dict, Tuple

import chameleon_com
import chameleon_cmd
from chameleon_enum import MfcKeyType


class DeviceNotConnected(Exception):
    pass


class ChameleonHFReader:
    """High-level API wrapper around Chameleon device for HF/Mifare Classic tasks.

    - Auto-detects serial port if none supplied
    - Provides methods that return parsed results (no CLI printing)

    Methods raise exceptions on device errors; responses are returned as-is from
    chameleon_cmd wrappers (which attach `.parsed` when available).
    """

    def __init__(self, port: Optional[str] = None, auto_connect: bool = True):
        self.port = port
        self.device: Optional[chameleon_com.ChameleonCom] = None
        self.cmd: Optional[chameleon_cmd.ChameleonCMD] = None
        if auto_connect:
            self.connect(port)

    @staticmethod
    def list_ports() -> List[str]:
        """Return a list of available serial port device names."""
        return [p.device for p in serial.tools.list_ports.comports()]

    def connect(self, port: Optional[str] = None) -> chameleon_com.ChameleonCom:
        """Connect to the Chameleon device.

        If port is None, will try available ports and connect to the first working one.
        """
        if port is None:
            ports = self.list_ports()
            if not ports:
                raise OpenFailException("No serial ports available to connect")
            for p in ports:
                try:
                    dev = chameleon_com.ChameleonCom().open(p)
                    self.device = dev
                    self.cmd = chameleon_cmd.ChameleonCMD(dev)
                    self.port = p
                    return dev
                except Exception:
                    continue
            raise OpenFailException(
                "Could not open any serial port for Chameleon device"
            )
        else:
            dev = chameleon_com.ChameleonCom().open(port)
            self.device = dev
            self.cmd = chameleon_cmd.ChameleonCMD(dev)
            self.port = port
            return dev

    def ensure_connected(self):
        if not self.device or not self.device.isOpen():
            raise DeviceNotConnected("Chameleon device is not connected")

    def close(self):
        if self.device:
            try:
                self.device.close()
            finally:
                self.device = None
                self.cmd = None

    # --- High level operations ---
    def scan_tags(self):
        """Perform a 14a scan and return parsed list of tags (uid, atqa, sak, ats).

        Returns list of dicts, or raises on error.
        """
        self.ensure_connected()
        resp = self.cmd.hf14a_scan()
        return resp.parsed if hasattr(resp, "parsed") else None

    def detect_mf1(self) -> bool:
        """Return True if a Mifare Classic tag is present."""
        self.ensure_connected()
        return self.cmd.mf1_detect_support()

    def detect_prng(self):
        """Detect PRNG weakness (if supported). Returns parsed byte or raise."""
        self.ensure_connected()
        resp = self.cmd.mf1_detect_prng()
        return resp.parsed

    def auth_block(self, block: int, key_type: MfcKeyType, key: bytes) -> bool:
        """Attempt to auth using provided 6-byte key on a block. Returns True on success."""
        self.ensure_connected()
        if len(key) != 6:
            raise ValueError("Key must be 6 bytes long")
        resp = self.cmd.mf1_auth_one_key_block(block, key_type, key)
        return bool(resp.parsed)

    def read_block(self, block: int, key_type: MfcKeyType, key: bytes) -> bytes:
        """Read a single block (requires a working auth). Returns raw 16 bytes."""
        self.ensure_connected()
        if len(key) != 6:
            raise ValueError("Key must be 6 bytes long")
        resp = self.cmd.mf1_read_one_block(block, key_type, key)
        return resp.parsed

    def read_blocks(
        self, blocks: List[int], key_type: MfcKeyType, key: bytes
    ) -> Dict[int, bytes]:
        """Read multiple blocks, returning mapping block->data.

        If a block cannot be read an exception will be raised by lower layers.
        """
        self.ensure_connected()
        result = {}
        for b in blocks:
            result[b] = self.read_block(b, key_type, key)
        return result

    def check_keys_on_block(self, block: int, key_type: int, keys: List[bytes]):
        """Try a list of candidate keys on a block. Returns the found key (6 bytes) or None."""
        self.ensure_connected()
        resp = self.cmd.mf1_check_keys_on_block(block, key_type, keys)
        if hasattr(resp, "parsed") and resp.parsed:
            return resp.parsed
        return None

    def check_keys_of_sectors(self, mask: bytes, keys: List[bytes]):
        """Check many sector keys given a mask and a set of keys.

        Returns a dict with 'status' and possibly 'found' and 'sectorKeys' keys when successful.
        """
        self.ensure_connected()
        resp = self.cmd.mf1_check_keys_of_sectors(mask, keys)
        return resp.parsed

    def nested_acquire(
        self,
        block_known: int,
        type_known: int,
        key_known: bytes,
        block_target: int,
        type_target: int,
    ):
        """Collect NT parameters for nested attack (returns parsed structure)."""
        self.ensure_connected()
        resp = self.cmd.mf1_nested_acquire(
            block_known, type_known, key_known, block_target, type_target
        )
        return resp.parsed

    def darkside_acquire(
        self,
        block_target: int,
        type_target: int,
        first_recover: bool,
        sync_max: int = 10,
    ):
        """Collect Darkside parameters. Returns parsed result tuple or status."""
        self.ensure_connected()
        resp = self.cmd.mf1_darkside_acquire(
            block_target, type_target, first_recover, sync_max
        )
        return resp.parsed


# Simple aliases for convenience
OpenFailException = chameleon_com.OpenFailException
NotOpenException = chameleon_com.NotOpenException


# Keep the module small and import-friendly: no code executed on import.

__all__ = [
    "ChameleonHFReader",
    "DeviceNotConnected",
    "MfcKeyType",
]
