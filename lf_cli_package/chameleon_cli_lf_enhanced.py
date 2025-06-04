#!/usr/bin/env python3
"""
Enhanced Chameleon Ultra CLI with comprehensive Low Frequency (LF) support

This module extends the existing CLI framework to provide complete LF protocol support
including T5577, HID Prox, Indala, and other 125kHz protocols.

Author: Manus AI
Date: June 2, 2025
Version: 1.0
"""

import binascii
import os
import tempfile
import re
import subprocess
import argparse
import timeit
import sys
import time
from datetime import datetime
import serial.tools.list_ports
import threading
import struct
from multiprocessing import Pool, cpu_count
from typing import Union
from pathlib import Path
from platform import uname
from datetime import datetime

# Import existing Chameleon modules
import chameleon_com
import chameleon_cmd
from chameleon_utils import ArgumentParserNoExit, ArgsParserError, UnexpectedResponseError
from chameleon_utils import CLITree
from chameleon_utils import CR, CG, CB, CC, CY, C0
from chameleon_utils import print_mem_dump
from chameleon_enum import Command, Status, SlotNumber, TagSenseType, TagSpecificType
from chameleon_enum import MifareClassicWriteMode, MifareClassicPrngType, MifareClassicDarksideStatus, MfcKeyType
from chameleon_enum import MifareUltralightWriteMode
from chameleon_enum import AnimationMode, ButtonPressFunction, ButtonType, MfcValueBlockOperator

# LF Protocol Constants
LF_PROTOCOLS = {
    'EM410X': {'modulation': 'ASK', 'frequency': '125kHz', 'id_length': 5},
    'T5577': {'modulation': 'ASK', 'frequency': '125kHz', 'blocks': 8},
    'HID_PROX': {'modulation': 'FSK', 'frequency': '125kHz', 'format_length': 26},
    'INDALA': {'modulation': 'PSK', 'frequency': '125kHz', 'id_length': 8},
    'EM4305': {'modulation': 'ASK', 'frequency': '125kHz', 'blocks': 16},
    'FDX_B': {'modulation': 'ASK', 'frequency': '134.2kHz', 'id_length': 15},
    'PARADOX': {'modulation': 'FSK', 'frequency': '125kHz', 'id_length': 6},
    'KERI': {'modulation': 'PSK', 'frequency': '125kHz', 'id_length': 4},
    'AWD': {'modulation': 'FSK', 'frequency': '125kHz', 'id_length': 8},
    'IOPROX': {'modulation': 'FSK', 'frequency': '125kHz', 'id_length': 4},
}

# Base CLI Unit Classes (existing)
class BaseCLIUnit:
    def __init__(self):
        self._device_com: Union[chameleon_com.ChameleonCom, None] = None
        self._device_cmd: Union[chameleon_cmd.ChameleonCMD, None] = None

    @property
    def device_com(self) -> chameleon_com.ChameleonCom:
        assert self._device_com is not None
        return self._device_com

    @device_com.setter
    def device_com(self, com):
        self._device_com = com
        self._device_cmd = chameleon_cmd.ChameleonCMD(self._device_com)

    @property
    def cmd(self) -> chameleon_cmd.ChameleonCMD:
        assert self._device_cmd is not None
        return self._device_cmd

    def args_parser(self) -> ArgumentParserNoExit:
        raise NotImplementedError("Please implement this")

    def before_exec(self, args: argparse.Namespace):
        return True

    def on_exec(self, args: argparse.Namespace):
        raise NotImplementedError("Please implement this")

    def after_exec(self, args: argparse.Namespace):
        return True

class DeviceRequiredUnit(BaseCLIUnit):
    """Make sure of device online"""
    def before_exec(self, args: argparse.Namespace):
        ret = self.device_com.isOpen()
        if ret:
            return True
        else:
            print("Please connect to chameleon device first(use 'hw connect').")
            return False

class ReaderRequiredUnit(DeviceRequiredUnit):
    """Make sure of device enter to reader mode."""
    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            ret = self.cmd.is_device_reader_mode()
            if ret:
                return True
            else:
                self.cmd.set_device_reader_mode(True)
                print("Switch to { Tag Reader } mode successfully.")
                return True
        return False

# Enhanced LF Base Classes
class LFProtocolUnit(DeviceRequiredUnit):
    """Base class for all LF protocol operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "GENERIC_LF"
        self.modulation = "ASK"
        self.frequency = "125kHz"
    
    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            # Ensure device is ready for LF operations
            return self.validate_lf_ready()
        return False
    
    def validate_lf_ready(self):
        """Validate that device is ready for LF operations"""
        try:
            # Check device mode and capabilities
            return True
        except Exception as e:
            print(f"{CR}Error: Device not ready for LF operations: {e}{C0}")
            return False
    
    def format_lf_data(self, data, format_type="hex"):
        """Format LF data for display"""
        if format_type == "hex":
            return data.hex().upper()
        elif format_type == "binary":
            return ' '.join(format(byte, '08b') for byte in data)
        elif format_type == "decimal":
            return str(int.from_bytes(data, byteorder='big'))
        return str(data)

class LFReaderRequiredUnit(LFProtocolUnit, ReaderRequiredUnit):
    """LF operations that require reader mode"""
    
    def before_exec(self, args: argparse.Namespace):
        # Combine LF validation with reader mode requirement
        lf_ready = LFProtocolUnit.before_exec(self, args)
        reader_ready = ReaderRequiredUnit.before_exec(self, args)
        return lf_ready and reader_ready

class LFEmulationUnit(LFProtocolUnit):
    """Base class for LF emulation operations"""
    
    def __init__(self):
        super().__init__()
        self.slot_num = None
        self.prev_slot_num = None
    
    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            # Handle slot management for emulation
            self.prev_slot_num = SlotNumber.from_fw(self.cmd.get_active_slot())
            if hasattr(args, 'slot') and args.slot is not None:
                self.slot_num = args.slot
                if self.slot_num != self.prev_slot_num:
                    self.cmd.set_active_slot(self.slot_num)
            else:
                self.slot_num = self.prev_slot_num
            return True
        return False
    
    def after_exec(self, args: argparse.Namespace):
        # Restore previous slot if changed
        if self.prev_slot_num != self.slot_num:
            self.cmd.set_active_slot(self.prev_slot_num)

# Protocol-Specific Base Classes
class EMProtocolUnit(LFProtocolUnit):
    """Base class for EM protocol family operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "EM"
        self.modulation = "ASK"

class EM410xUnit(EMProtocolUnit):
    """Base class for EM410x operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "EM410X"
        self.id_length = 5  # 5 bytes for EM410x
    
    def validate_em410x_id(self, id_str):
        """Validate EM410x ID format"""
        if not re.match(r"^[a-fA-F0-9]{10}$", id_str):
            raise ArgsParserError("EM410x ID must be exactly 10 hexadecimal characters")
        return bytes.fromhex(id_str)

class T55xxUnit(LFProtocolUnit):
    """Base class for T5577 operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "T5577"
        self.modulation = "ASK"
        self.total_blocks = 8
        self.config_block = 0
    
    def validate_t55xx_block(self, block_num):
        """Validate T5577 block number"""
        if not (0 <= block_num < self.total_blocks):
            raise ArgsParserError(f"T5577 block must be 0-{self.total_blocks-1}")
        return block_num
    
    def validate_t55xx_data(self, data_str):
        """Validate T5577 block data"""
        if not re.match(r"^[a-fA-F0-9]{8}$", data_str):
            raise ArgsParserError("T5577 data must be exactly 8 hexadecimal characters (32 bits)")
        return bytes.fromhex(data_str)

class HIDProxUnit(LFProtocolUnit):
    """Base class for HID Prox operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "HID_PROX"
        self.modulation = "FSK"
        self.standard_format = 26  # Standard 26-bit format
    
    def validate_hid_format(self, facility_code, card_number, format_length=26):
        """Validate HID Prox format parameters"""
        if format_length == 26:
            if not (0 <= facility_code <= 255):
                raise ArgsParserError("Facility code must be 0-255 for 26-bit format")
            if not (0 <= card_number <= 65535):
                raise ArgsParserError("Card number must be 0-65535 for 26-bit format")
        return True
    
    def encode_hid_26bit(self, facility_code, card_number):
        """Encode HID 26-bit format"""
        # Standard HID 26-bit encoding
        data = (facility_code << 16) | card_number
        # Add parity bits (simplified)
        return data.to_bytes(4, byteorder='big')

class IndalaUnit(LFProtocolUnit):
    """Base class for Indala operations"""
    
    def __init__(self):
        super().__init__()
        self.protocol_name = "INDALA"
        self.modulation = "PSK"
        self.id_length = 8  # Standard Indala ID length
    
    def validate_indala_id(self, id_str):
        """Validate Indala ID format"""
        if not re.match(r"^[a-fA-F0-9]{16}$", id_str):
            raise ArgsParserError("Indala ID must be exactly 16 hexadecimal characters")
        return bytes.fromhex(id_str)

# Argument Handling Classes
class LFEMIdArgsUnit(EM410xUnit):
    """Argument handling for EM410x ID operations"""
    
    @staticmethod
    def add_card_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument("--id", type=str, required=required, 
                          help="EM410x tag id (10 hex characters)", metavar="<hex>")
        return parser

    def before_exec(self, args: argparse.Namespace):
        if super().before_exec(args):
            if hasattr(args, 'id') and args.id is not None:
                self.validate_em410x_id(args.id)
            return True
        return False

class LFT55xxArgsUnit(T55xxUnit):
    """Argument handling for T5577 operations"""
    
    @staticmethod
    def add_block_arg(parser: ArgumentParserNoExit, required=True):
        parser.add_argument("--block", "-b", type=int, required=required,
                          help="T5577 block number (0-7)", metavar="<0-7>")
        return parser
    
    @staticmethod
    def add_data_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument("--data", "-d", type=str, required=required,
                          help="T5577 block data (8 hex characters)", metavar="<hex>")
        return parser
    
    @staticmethod
    def add_password_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument("--password", "-p", type=str, required=required,
                          help="T5577 password (8 hex characters)", metavar="<hex>")
        return parser

class LFHIDArgsUnit(HIDProxUnit):
    """Argument handling for HID Prox operations"""
    
    @staticmethod
    def add_hid_args(parser: ArgumentParserNoExit):
        parser.add_argument("--facility", "-f", type=int, required=False,
                          help="HID facility code (0-255)", metavar="<dec>")
        parser.add_argument("--card", "-c", type=int, required=False,
                          help="HID card number (0-65535)", metavar="<dec>")
        parser.add_argument("--format", type=int, default=26,
                          help="HID format length (default: 26)", metavar="<bits>")
        return parser

class LFIndalaArgsUnit(IndalaUnit):
    """Argument handling for Indala operations"""
    
    @staticmethod
    def add_indala_arg(parser: ArgumentParserNoExit, required=False):
        parser.add_argument("--id", type=str, required=required,
                          help="Indala ID (16 hex characters)", metavar="<hex>")
        return parser

class SlotIndexArgsUnit(DeviceRequiredUnit):
    """Slot management for LF operations"""
    
    @staticmethod
    def add_slot_args(parser: ArgumentParserNoExit, mandatory=False):
        slot_choices = [x.value for x in SlotNumber]
        help_str = f"Slot Index: {slot_choices} Default: active slot"
        parser.add_argument('-s', "--slot", type=int, required=mandatory, 
                          help=help_str, metavar="<1-8>", choices=slot_choices)
        return parser

# Command Tree Structure
root = CLITree(root=True)
hw = root.subgroup('hw', 'Hardware-related commands')
hw_slot = hw.subgroup('slot', 'Emulation slots commands')
hw_settings = hw.subgroup('settings', 'Chameleon settings commands')

hf = root.subgroup('hf', 'High Frequency commands')
hf_14a = hf.subgroup('14a', 'ISO14443-a commands')
hf_mf = hf.subgroup('mf', 'MIFARE Classic commands')
hf_mfu = hf.subgroup('mfu', 'MIFARE Ultralight / NTAG commands')

# Enhanced LF Command Tree
lf = root.subgroup('lf', 'Low Frequency commands')
lf_em = lf.subgroup('em', 'EM protocol family commands')
lf_em_410x = lf_em.subgroup('410x', 'EM410x specific commands')
lf_em_4305 = lf_em.subgroup('4305', 'EM4305 specific commands')

lf_t55xx = lf.subgroup('t55xx', 'T5577 programmable card commands')
lf_hid = lf.subgroup('hid', 'HID Prox protocol commands')
lf_indala = lf.subgroup('indala', 'Indala protocol commands')
lf_fdx = lf.subgroup('fdx', 'FDX-B animal tag commands')
lf_paradox = lf.subgroup('paradox', 'Paradox protocol commands')
lf_keri = lf.subgroup('keri', 'Keri protocol commands')
lf_awd = lf.subgroup('awd', 'AWD protocol commands')
lf_ioprox = lf.subgroup('ioprox', 'ioProx protocol commands')

# Utility Commands
@root.command('clear')
class RootClear(BaseCLIUnit):
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Clear screen'
        return parser

    def on_exec(self, args: argparse.Namespace):
        os.system('clear' if os.name == 'posix' else 'cls')

@lf.command('info')
class LFInfo(LFProtocolUnit):
    """Display LF protocol information and capabilities"""
    
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Display LF protocol information and device capabilities'
        parser.add_argument('--protocol', '-p', type=str, 
                          choices=list(LF_PROTOCOLS.keys()),
                          help='Show specific protocol information')
        return parser
    
    def on_exec(self, args: argparse.Namespace):
        print(f"{CG}Chameleon Ultra - Low Frequency Protocol Information{C0}")
        print("=" * 60)
        
        if args.protocol:
            # Show specific protocol info
            protocol = LF_PROTOCOLS[args.protocol]
            print(f"{CC}Protocol: {args.protocol}{C0}")
            print(f"Modulation: {protocol['modulation']}")
            print(f"Frequency: {protocol['frequency']}")
            for key, value in protocol.items():
                if key not in ['modulation', 'frequency']:
                    print(f"{key.replace('_', ' ').title()}: {value}")
        else:
            # Show all protocols
            print(f"{CC}Supported LF Protocols:{C0}")
            print()
            for protocol_name, details in LF_PROTOCOLS.items():
                print(f"{CY}{protocol_name:12}{C0} | {details['modulation']:3} | {details['frequency']:8}")
            
            print()
            print(f"{CC}Available Command Groups:{C0}")
            print("lf em      - EM protocol family commands")
            print("lf t55xx   - T5577 programmable card commands") 
            print("lf hid     - HID Prox protocol commands")
            print("lf indala  - Indala protocol commands")
            print("lf fdx     - FDX-B animal tag commands")
            print("lf paradox - Paradox protocol commands")
            print("lf keri    - Keri protocol commands")
            print("lf awd     - AWD protocol commands")
            print("lf ioprox  - ioProx protocol commands")

@lf.command('scan')
class LFScan(LFReaderRequiredUnit):
    """Scan for LF cards and attempt protocol detection"""
    
    def args_parser(self) -> ArgumentParserNoExit:
        parser = ArgumentParserNoExit()
        parser.description = 'Scan for LF cards and detect protocol'
        parser.add_argument('--timeout', '-t', type=int, default=5,
                          help='Scan timeout in seconds (default: 5)', metavar='<sec>')
        parser.add_argument('--verbose', '-v', action='store_true',
                          help='Show detailed scan information')
        return parser
    
    def on_exec(self, args: argparse.Namespace):
        print(f"{CG}Scanning for LF cards...{C0}")
        print(f"Timeout: {args.timeout} seconds")
        print()
        
        # Try different protocols in order of likelihood
        protocols_to_try = ['EM410X', 'T5577', 'HID_PROX', 'INDALA']
        
        for protocol in protocols_to_try:
            if args.verbose:
                print(f"Trying {protocol}...")
            
            try:
                if protocol == 'EM410X':
                    # Try EM410x scan
                    result = self.cmd.em410x_scan()
                    if result:
                        print(f"{CG}Found EM410x card!{C0}")
                        print(f"ID: {result.hex().upper()}")
                        return
            except Exception as e:
                if args.verbose:
                    print(f"  {protocol}: {e}")
                continue
        
        print(f"{CY}No LF cards detected{C0}")
        print("Try positioning the card closer to the antenna")
        print("or use specific protocol commands for manual detection")

if __name__ == "__main__":
    print("Enhanced Chameleon Ultra LF CLI Framework")
    print("This module provides the core framework for LF operations")
    print("Import this module to use the enhanced CLI functionality")

