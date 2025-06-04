#!/usr/bin/env python3
"""
Chameleon Ultra LF Command Extensions

This module extends the chameleon_cmd.ChameleonCMD class to add comprehensive
LF protocol support that maps to the firmware's LF command set.

Author: Manus AI
Date: June 2, 2025
Version: 1.0
"""

import struct
import time
from typing import Optional, Union
from chameleon_enum import Command, Status

class ChameleonLFCommands:
    """
    Extension class for LF protocol commands
    This class should be mixed into the existing ChameleonCMD class
    """

    # ===== EM410x Protocol Commands =====

    def em410x_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for EM410x cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            5-byte EM410x ID if found, None otherwise
        """
        try:
            # Command 3000: EM410X_SCAN
            response = self.device.send_cmd_sync(
                Command.EM410X_SCAN,
                data=struct.pack('<I', timeout * 1000),  # Convert to milliseconds
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) == 5:
                return response.data
            return None

        except Exception:
            return None

    def em410x_write_to_t55xx(self, em_id: bytes, old_key: bytes = b'\x00' * 4, new_key: bytes = b'\x00' * 4) -> bool:
        """
        Write EM410x ID to T55xx card

        Args:
            em_id: 5-byte EM410x ID
            old_key: Current T55xx password (4 bytes)
            new_key: New T55xx password (4 bytes)

        Returns:
            True if successful, False otherwise
        """
        if len(em_id) != 5:
            raise ValueError("EM410x ID must be 5 bytes")
        if len(old_key) != 4 or len(new_key) != 4:
            raise ValueError("T55xx keys must be 4 bytes")

        try:
            # Command 3001: EM410X_WRITE_TO_T55XX
            data = em_id + old_key + new_key
            response = self.device.send_cmd_sync(
                Command.EM410X_WRITE_TO_T55XX,
                data=data,
                status=Status.SUCCESS,
                timeout=10
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    def em410x_get_emu_id(self) -> bytes:
        """
        Get current EM410x emulation ID

        Returns:
            5-byte EM410x ID
        """
        # Command 5000: EM410X_GET_EMU_ID
        response = self.device.send_cmd_sync(
            Command.EM410X_GET_EMU_ID,
            status=Status.SUCCESS
        )

        if response.status != Status.SUCCESS or len(response.data) != 5:
            raise Exception("Failed to get EM410x emulation ID")

        return response.data

    def em410x_set_emu_id(self, em_id: bytes) -> bool:
        """
        Set EM410x emulation ID

        Args:
            em_id: 5-byte EM410x ID

        Returns:
            True if successful, False otherwise
        """
        if len(em_id) != 5:
            raise ValueError("EM410x ID must be 5 bytes")

        try:
            # Command 5001: EM410X_SET_EMU_ID
            response = self.device.send_cmd_sync(
                Command.EM410X_SET_EMU_ID,
                data=em_id,
                status=Status.SUCCESS
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    # ===== T5577 Protocol Commands =====

    def t55xx_read_block(self, block: int, password: Optional[str] = None) -> bytes:
        """
        Read T5577 block

        Args:
            block: Block number (0-7)
            password: Optional password (8 hex characters)

        Returns:
            4-byte block data
        """
        if not (0 <= block <= 7):
            raise ValueError("Block must be 0-7")

        # Prepare command data
        data = struct.pack('<B', block)
        if password:
            if len(password) != 8:
                raise ValueError("Password must be 8 hex characters")
            pwd_bytes = bytes.fromhex(password)
            data += pwd_bytes

        try:
            # Command 3010: T55XX_READ_BLOCK (hypothetical command number)
            response = self.device.send_cmd_sync(
                3010,  # T55XX_READ_BLOCK
                data=data,
                status=Status.SUCCESS,
                timeout=5
            )

            if response.status != Status.SUCCESS or len(response.data) != 4:
                raise Exception(f"Failed to read T5577 block {block}")

            return response.data

        except Exception as e:
            raise Exception(f"Error reading T5577 block {block}: {e}")

    def t55xx_write_block(self, block: int, data: bytes, password: Optional[str] = None) -> bool:
        """
        Write T5577 block

        Args:
            block: Block number (0-7)
            data: 4-byte block data
            password: Optional password (8 hex characters)

        Returns:
            True if successful, False otherwise
        """
        if not (0 <= block <= 7):
            raise ValueError("Block must be 0-7")
        if len(data) != 4:
            raise ValueError("Data must be 4 bytes")

        # Prepare command data
        cmd_data = struct.pack('<B', block) + data
        if password:
            if len(password) != 8:
                raise ValueError("Password must be 8 hex characters")
            pwd_bytes = bytes.fromhex(password)
            cmd_data += pwd_bytes

        try:
            # Command 3011: T55XX_WRITE_BLOCK (hypothetical command number)
            response = self.device.send_cmd_sync(
                3011,  # T55XX_WRITE_BLOCK
                data=cmd_data,
                status=Status.SUCCESS,
                timeout=10
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    def t55xx_detect(self) -> bool:
        """
        Detect T5577 card presence

        Returns:
            True if T5577 detected, False otherwise
        """
        try:
            # Try to read configuration block
            self.t55xx_read_block(0)
            return True
        except Exception:
            return False

    def t55xx_wipe(self, password: Optional[str] = None) -> bool:
        """
        Wipe T5577 card (reset to default configuration)

        Args:
            password: Optional password (8 hex characters)

        Returns:
            True if successful, False otherwise
        """
        try:
            # Default T5577 configuration
            default_config = b'\x00\x08\x80\x00'  # Basic ASK configuration

            # Write default config to block 0
            success = self.t55xx_write_block(0, default_config, password)
            if not success:
                return False

            # Clear data blocks 1-7
            empty_data = b'\x00\x00\x00\x00'
            for block in range(1, 8):
                if not self.t55xx_write_block(block, empty_data, password):
                    return False

            return True

        except Exception:
            return False

    # ===== HID Prox Protocol Commands =====

    def hid_prox_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for HID Prox cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            HID card data if found, None otherwise
        """
        try:
            # Command 3020: HID_PROX_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3020,  # HID_PROX_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 4:
                return response.data
            return None

        except Exception:
            return None

    def hid_prox_write_to_t55xx(self, hid_data: bytes) -> bool:
        """
        Write HID Prox data to T55xx card

        Args:
            hid_data: HID card data

        Returns:
            True if successful, False otherwise
        """
        try:
            # Command 3021: HID_PROX_WRITE_TO_T55XX (hypothetical command number)
            response = self.device.send_cmd_sync(
                3021,  # HID_PROX_WRITE_TO_T55XX
                data=hid_data,
                status=Status.SUCCESS,
                timeout=10
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    def hid_prox_get_emu_data(self) -> bytes:
        """
        Get current HID Prox emulation data

        Returns:
            HID emulation data
        """
        # Command 5020: HID_PROX_GET_EMU_DATA (hypothetical command number)
        response = self.device.send_cmd_sync(
            5020,  # HID_PROX_GET_EMU_DATA
            status=Status.SUCCESS
        )

        if response.status != Status.SUCCESS:
            raise Exception("Failed to get HID Prox emulation data")

        return response.data

    def hid_prox_set_emu_data(self, hid_data: bytes) -> bool:
        """
        Set HID Prox emulation data

        Args:
            hid_data: HID card data

        Returns:
            True if successful, False otherwise
        """
        try:
            # Command 5021: HID_PROX_SET_EMU_DATA (hypothetical command number)
            response = self.device.send_cmd_sync(
                5021,  # HID_PROX_SET_EMU_DATA
                data=hid_data,
                status=Status.SUCCESS
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    # ===== Indala Protocol Commands =====

    def indala_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for Indala cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            Indala card ID if found, None otherwise
        """
        try:
            # Command 3030: INDALA_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3030,  # INDALA_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 8:
                return response.data
            return None

        except Exception:
            return None

    def indala_write_to_t55xx(self, indala_id: bytes) -> bool:
        """
        Write Indala ID to T55xx card

        Args:
            indala_id: Indala card ID

        Returns:
            True if successful, False otherwise
        """
        try:
            # Command 3031: INDALA_WRITE_TO_T55XX (hypothetical command number)
            response = self.device.send_cmd_sync(
                3031,  # INDALA_WRITE_TO_T55XX
                data=indala_id,
                status=Status.SUCCESS,
                timeout=10
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    def indala_get_emu_id(self) -> bytes:
        """
        Get current Indala emulation ID

        Returns:
            Indala emulation ID
        """
        # Command 5030: INDALA_GET_EMU_ID (hypothetical command number)
        response = self.device.send_cmd_sync(
            5030,  # INDALA_GET_EMU_ID
            status=Status.SUCCESS
        )

        if response.status != Status.SUCCESS:
            raise Exception("Failed to get Indala emulation ID")

        return response.data

    def indala_set_emu_id(self, indala_id: bytes) -> bool:
        """
        Set Indala emulation ID

        Args:
            indala_id: Indala card ID

        Returns:
            True if successful, False otherwise
        """
        try:
            # Command 5031: INDALA_SET_EMU_ID (hypothetical command number)
            response = self.device.send_cmd_sync(
                5031,  # INDALA_SET_EMU_ID
                data=indala_id,
                status=Status.SUCCESS
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    # ===== Additional LF Protocol Commands =====

    def fdx_b_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for FDX-B animal tags

        Args:
            timeout: Scan timeout in seconds

        Returns:
            FDX-B tag data if found, None otherwise
        """
        try:
            # Command 3040: FDX_B_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3040,  # FDX_B_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 15:
                return response.data
            return None

        except Exception:
            return None

    def paradox_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for Paradox cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            Paradox card data if found, None otherwise
        """
        try:
            # Command 3050: PARADOX_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3050,  # PARADOX_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 6:
                return response.data
            return None

        except Exception:
            return None

    def keri_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for Keri cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            Keri card data if found, None otherwise
        """
        try:
            # Command 3060: KERI_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3060,  # KERI_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 4:
                return response.data
            return None

        except Exception:
            return None

    def awd_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for AWD cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            AWD card data if found, None otherwise
        """
        try:
            # Command 3070: AWD_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3070,  # AWD_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 8:
                return response.data
            return None

        except Exception:
            return None

    def ioprox_scan(self, timeout: int = 5) -> Optional[bytes]:
        """
        Scan for ioProx cards

        Args:
            timeout: Scan timeout in seconds

        Returns:
            ioProx card data if found, None otherwise
        """
        try:
            # Command 3080: IOPROX_SCAN (hypothetical command number)
            response = self.device.send_cmd_sync(
                3080,  # IOPROX_SCAN
                data=struct.pack('<I', timeout * 1000),
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS and len(response.data) >= 4:
                return response.data
            return None

        except Exception:
            return None

    # ===== Generic LF Utility Commands =====

    def lf_read_raw(self, frequency: int = 125000, timeout: int = 5) -> Optional[bytes]:
        """
        Read raw LF data

        Args:
            frequency: LF frequency in Hz (default: 125000)
            timeout: Read timeout in seconds

        Returns:
            Raw LF data if successful, None otherwise
        """
        try:
            # Command 3100: LF_READ_RAW (hypothetical command number)
            data = struct.pack('<II', frequency, timeout * 1000)
            response = self.device.send_cmd_sync(
                3100,  # LF_READ_RAW
                data=data,
                status=Status.SUCCESS,
                timeout=timeout + 2
            )

            if response.status == Status.SUCCESS:
                return response.data
            return None

        except Exception:
            return None

    def lf_write_raw(self, data: bytes, frequency: int = 125000) -> bool:
        """
        Write raw LF data

        Args:
            data: Raw data to write
            frequency: LF frequency in Hz (default: 125000)

        Returns:
            True if successful, False otherwise
        """
        try:
            # Command 3101: LF_WRITE_RAW (hypothetical command number)
            cmd_data = struct.pack('<I', frequency) + data
            response = self.device.send_cmd_sync(
                3101,  # LF_WRITE_RAW
                data=cmd_data,
                status=Status.SUCCESS,
                timeout=10
            )

            return response.status == Status.SUCCESS

        except Exception:
            return False

    def lf_tune_antenna(self) -> dict:
        """
        Tune LF antenna and get measurements

        Returns:
            Dictionary with tuning measurements
        """
        try:
            # Command 3110: LF_TUNE_ANTENNA (hypothetical command number)
            response = self.device.send_cmd_sync(
                3110,  # LF_TUNE_ANTENNA
                status=Status.SUCCESS,
                timeout=5
            )

            if response.status == Status.SUCCESS and len(response.data) >= 8:
                # Parse tuning data (example format)
                voltage = struct.unpack('<I', response.data[0:4])[0]
                current = struct.unpack('<I', response.data[4:8])[0]

                return {
                    'voltage': voltage,
                    'current': current,
                    'impedance': voltage / current if current > 0 else 0
                }

            return {}

        except Exception:
            return {}

# Monkey patch the ChameleonCMD class to add LF commands
def extend_chameleon_cmd():
    """
    Extend the existing ChameleonCMD class with LF commands
    This function should be called after importing chameleon_cmd
    """
    import chameleon_cmd

    # Add all LF command methods to ChameleonCMD
    for attr_name in dir(ChameleonLFCommands):
        if not attr_name.startswith('_'):
            attr = getattr(ChameleonLFCommands, attr_name)
            if callable(attr):
                setattr(chameleon_cmd.ChameleonCMD, attr_name, attr)

if __name__ == "__main__":
    print("Chameleon Ultra LF Command Extensions")
    print("This module extends the ChameleonCMD class with comprehensive LF support")
    print("Call extend_chameleon_cmd() to add LF commands to the existing ChameleonCMD class")
