#!/usr/bin/env python3
"""
ChameleonUltra LF CLI Fix
========================

This script fixes the CLI issues with LF commands by:
1. Adding missing status codes
2. Fixing response parsing
3. Updating command implementations

Usage:
    python3 fix_lf_cli.py /path/to/ChameleonUltra/software/script/
"""

import os
import sys
import shutil
from pathlib import Path

def backup_file(file_path):
    """Create a backup of the original file."""
    backup_path = f"{file_path}.backup"
    shutil.copy2(file_path, backup_path)
    print(f"✅ Backed up {file_path} to {backup_path}")

def fix_status_enum(script_dir):
    """Fix the status enum to include missing LF status codes."""
    enum_file = os.path.join(script_dir, "chameleon_enum.py")
    
    if not os.path.exists(enum_file):
        print(f"❌ File not found: {enum_file}")
        return False
    
    backup_file(enum_file)
    
    with open(enum_file, 'r') as f:
        content = f.read()
    
    # Add missing status code
    if "LF_TAG_NO_FOUND" not in content:
        # Find the line with EM410X_TAG_NO_FOUND and add after it
        lines = content.split('\n')
        for i, line in enumerate(lines):
            if "EM410X_TAG_NO_FOUND = 0x41" in line:
                lines.insert(i + 1, "    LF_TAG_NO_FOUND = 0x42")
                break
        
        # Update the __str__ method
        for i, line in enumerate(lines):
            if "elif self == Status.EM410X_TAG_NO_FOUND:" in line:
                # Add the new status handling after EM410X
                lines.insert(i + 2, "        elif self == Status.LF_TAG_NO_FOUND:")
                lines.insert(i + 3, '            return "LF tag no found"')
                break
        
        content = '\n'.join(lines)
    
    with open(enum_file, 'w') as f:
        f.write(content)
    
    print("✅ Fixed status enum with missing LF_TAG_NO_FOUND")
    return True

def fix_lf_commands(script_dir):
    """Fix LF command implementations to handle responses properly."""
    cmd_file = os.path.join(script_dir, "chameleon_cmd.py")
    
    if not os.path.exists(cmd_file):
        print(f"❌ File not found: {cmd_file}")
        return False
    
    backup_file(cmd_file)
    
    with open(cmd_file, 'r') as f:
        content = f.read()
    
    # Fix lf_read_raw to handle different response types
    old_lf_read_raw = '''    def lf_read_raw(self, samples: int, frequency: int = 125000):
        """
        Read raw LF signal data.

        :param samples: Number of samples to capture
        :param frequency: Sampling frequency in Hz
        :return:
        """
        data = struct.pack('!II', samples, frequency)
        resp = self.device.send_cmd_sync(Command.LF_READ_RAW, data)
        if resp.status == Status.LF_TAG_OK:
            resp.parsed = resp.data
        return resp'''
    
    new_lf_read_raw = '''    def lf_read_raw(self, samples: int, frequency: int = 125000):
        """
        Read raw LF signal data.

        :param samples: Number of samples to capture
        :param frequency: Sampling frequency in Hz
        :return:
        """
        data = struct.pack('!II', samples, frequency)
        resp = self.device.send_cmd_sync(Command.LF_READ_RAW, data)
        
        # Handle different response types
        if hasattr(resp, 'status'):
            if resp.status == Status.LF_TAG_OK:
                resp.parsed = resp.data if hasattr(resp, 'data') else b''
            elif resp.status == getattr(Status, 'LF_TAG_NO_FOUND', 0x42):
                resp.parsed = b''
        else:
            # Handle raw bytes response
            class MockResponse:
                def __init__(self, data):
                    self.status = Status.LF_TAG_OK if data else getattr(Status, 'LF_TAG_NO_FOUND', 0x42)
                    self.data = data
                    self.parsed = data
            resp = MockResponse(resp if isinstance(resp, bytes) else b'')
        
        return resp'''
    
    if old_lf_read_raw in content:
        content = content.replace(old_lf_read_raw, new_lf_read_raw)
        print("✅ Fixed lf_read_raw command")
    
    # Fix lf_scan_auto to handle different status codes
    old_lf_scan_auto = '''    def lf_scan_auto(self, timeout: int = 2000, verbose: bool = False):
        """
        Auto-scan for LF tags.

        :param timeout: Scan timeout in milliseconds
        :param verbose: Enable verbose output
        :return:
        """
        data = struct.pack('!IB', timeout, verbose)
        return self.device.send_cmd_sync(Command.LF_SCAN_AUTO, data)'''
    
    new_lf_scan_auto = '''    def lf_scan_auto(self, timeout: int = 2000, verbose: bool = False):
        """
        Auto-scan for LF tags.

        :param timeout: Scan timeout in milliseconds
        :param verbose: Enable verbose output
        :return:
        """
        data = struct.pack('!IB', timeout, verbose)
        resp = self.device.send_cmd_sync(Command.LF_SCAN_AUTO, data)
        
        # Handle different response types
        if hasattr(resp, 'status'):
            # Check for various LF status codes
            if resp.status in [Status.LF_TAG_OK, getattr(Status, 'LF_TAG_NO_FOUND', 0x42), Status.EM410X_TAG_NO_FOUND]:
                if hasattr(resp, 'data'):
                    resp.parsed = resp.data
        else:
            # Handle raw bytes response
            class MockResponse:
                def __init__(self, data):
                    self.status = Status.LF_TAG_OK if data else getattr(Status, 'LF_TAG_NO_FOUND', 0x42)
                    self.data = data
                    self.parsed = data
            resp = MockResponse(resp if isinstance(resp, bytes) else b'')
        
        return resp'''
    
    if old_lf_scan_auto in content:
        content = content.replace(old_lf_scan_auto, new_lf_scan_auto)
        print("✅ Fixed lf_scan_auto command")
    
    with open(cmd_file, 'w') as f:
        f.write(content)
    
    return True

def fix_cli_unit(script_dir):
    """Fix CLI unit to handle new status codes properly."""
    cli_file = os.path.join(script_dir, "chameleon_cli_unit.py")
    
    if not os.path.exists(cli_file):
        print(f"❌ File not found: {cli_file}")
        return False
    
    backup_file(cli_file)
    
    with open(cli_file, 'r') as f:
        content = f.read()
    
    # Fix lf_scan_auto CLI handler
    old_scan_handler = '''        resp = self.cmd.lf_scan_auto(args.timeout, args.verbose)
        if resp.status == Status.LF_TAG_OK:
            print(f" - LF tag detected: {resp.parsed.hex().upper()}")
        else:
            print(f" [!] {CR}No LF tag found{C0}")'''
    
    new_scan_handler = '''        resp = self.cmd.lf_scan_auto(args.timeout, args.verbose)
        if hasattr(resp, 'status'):
            if resp.status == Status.LF_TAG_OK:
                data_hex = resp.parsed.hex().upper() if hasattr(resp, 'parsed') and resp.parsed else "No data"
                print(f" - LF tag detected: {data_hex}")
            elif resp.status == getattr(Status, 'LF_TAG_NO_FOUND', 0x42):
                print(f" [!] {CR}No LF tag found{C0}")
            elif resp.status == Status.EM410X_TAG_NO_FOUND:
                print(f" [!] {CR}No EM410x tag found{C0}")
            elif resp.status == Status.DEVICE_MODE_ERROR:
                print(f" [!] {CR}Device mode error - switch to reader mode first{C0}")
            else:
                print(f" [!] {CR}Unexpected status: 0x{resp.status:02X}{C0}")
        else:
            print(f" [!] {CR}Invalid response format{C0}")'''
    
    if old_scan_handler in content:
        content = content.replace(old_scan_handler, new_scan_handler)
        print("✅ Fixed lf_scan_auto CLI handler")
    
    # Fix lf_read_raw CLI handler
    old_raw_handler = '''        resp = self.cmd.lf_read_raw(args.samples, args.frequency)
        if resp.status == Status.LF_TAG_OK:
            print(f" - Raw LF data ({len(resp.parsed)} bytes): {resp.parsed.hex().upper()}")
        else:
            print(f" [!] {CR}Failed to read raw LF data{C0}")'''
    
    new_raw_handler = '''        resp = self.cmd.lf_read_raw(args.samples, args.frequency)
        if hasattr(resp, 'status'):
            if resp.status == Status.LF_TAG_OK:
                data_len = len(resp.parsed) if hasattr(resp, 'parsed') and resp.parsed else 0
                data_hex = resp.parsed.hex().upper() if hasattr(resp, 'parsed') and resp.parsed else "No data"
                print(f" - Raw LF data ({data_len} bytes): {data_hex}")
            elif resp.status == Status.DEVICE_MODE_ERROR:
                print(f" [!] {CR}Device mode error - switch to reader mode first{C0}")
            else:
                print(f" [!] {CR}Failed to read raw LF data (status: 0x{resp.status:02X}){C0}")
        else:
            print(f" [!] {CR}Invalid response format{C0}")'''
    
    if old_raw_handler in content:
        content = content.replace(old_raw_handler, new_raw_handler)
        print("✅ Fixed lf_read_raw CLI handler")
    
    with open(cli_file, 'w') as f:
        f.write(content)
    
    return True

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 fix_lf_cli.py /path/to/ChameleonUltra/software/script/")
        sys.exit(1)
    
    script_dir = sys.argv[1]
    
    if not os.path.isdir(script_dir):
        print(f"❌ Directory not found: {script_dir}")
        sys.exit(1)
    
    print("🔧 Fixing ChameleonUltra LF CLI issues...")
    
    success = True
    success &= fix_status_enum(script_dir)
    success &= fix_lf_commands(script_dir)
    success &= fix_cli_unit(script_dir)
    
    if success:
        print("\n✅ All fixes applied successfully!")
        print("🚀 Your ChameleonUltra CLI should now handle LF commands properly.")
        print("\nTo test:")
        print("1. hw connect")
        print("2. hw mode -r  (switch to reader mode)")
        print("3. lf scan auto")
        print("4. lf read raw --samples 100")
    else:
        print("\n❌ Some fixes failed. Check the error messages above.")
        sys.exit(1)

if __name__ == "__main__":
    main()

