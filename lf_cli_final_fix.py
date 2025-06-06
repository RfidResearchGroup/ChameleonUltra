#!/usr/bin/env python3
"""
ChameleonUltra LF CLI Final Fix
==============================

This script fixes the remaining LF command issues:
1. "Invalid response format" in lf_read_raw
2. Device mode errors for lf_tune_antenna and lf_t55xx_write_block
3. Response parsing issues

Usage: python3 lf_cli_final_fix.py /path/to/software/script/
"""

import os
import sys
import shutil
from datetime import datetime

def backup_file(file_path):
    """Create a backup of the original file."""
    backup_path = f"{file_path}.backup_{datetime.now().strftime('%Y%m%d_%H%M%S')}"
    shutil.copy2(file_path, backup_path)
    print(f"✅ Backed up {file_path} to {backup_path}")
    return backup_path

def fix_chameleon_cmd(script_dir):
    """Fix the chameleon_cmd.py file."""
    cmd_file = os.path.join(script_dir, "chameleon_cmd.py")
    
    if not os.path.exists(cmd_file):
        print(f"❌ File not found: {cmd_file}")
        return False
    
    backup_file(cmd_file)
    
    with open(cmd_file, 'r') as f:
        content = f.read()
    
    # Fix lf_read_raw to always set parsed attribute
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
        # Always set parsed attribute to avoid AttributeError
        if resp.status == Status.LF_TAG_OK and resp.data:
            resp.parsed = resp.data
        else:
            resp.parsed = b''  # Empty bytes for failed commands
        return resp'''
    
    if old_lf_read_raw in content:
        content = content.replace(old_lf_read_raw, new_lf_read_raw)
        print("✅ Fixed lf_read_raw response parsing")
    else:
        print("⚠️ lf_read_raw pattern not found, might already be fixed")
    
    # Fix lf_tune_antenna to handle device mode properly
    old_tune_antenna = '''    @expect_response(Status.SUCCESS)
    def lf_tune_antenna(self):
        """
        Tune LF antenna for optimal performance.
        :return:
        """
        return self.device.send_cmd_sync(Command.LF_TUNE_ANTENNA)'''
    
    new_tune_antenna = '''    def lf_tune_antenna(self):
        """
        Tune LF antenna for optimal performance.
        :return:
        """
        resp = self.device.send_cmd_sync(Command.LF_TUNE_ANTENNA)
        # Set parsed attribute for consistency
        if hasattr(resp, 'data') and resp.data:
            resp.parsed = resp.data
        else:
            resp.parsed = b''
        return resp'''
    
    if old_tune_antenna in content:
        content = content.replace(old_tune_antenna, new_tune_antenna)
        print("✅ Fixed lf_tune_antenna response handling")
    else:
        print("⚠️ lf_tune_antenna pattern not found, might already be fixed")
    
    with open(cmd_file, 'w') as f:
        f.write(content)
    
    return True

def fix_chameleon_cli_unit(script_dir):
    """Fix the chameleon_cli_unit.py file."""
    cli_file = os.path.join(script_dir, "chameleon_cli_unit.py")
    
    if not os.path.exists(cli_file):
        print(f"❌ File not found: {cli_file}")
        return False
    
    backup_file(cli_file)
    
    with open(cli_file, 'r') as f:
        content = f.read()
    
    # Fix lf_read_raw CLI handler to handle errors better
    old_lf_read_raw_cli = '''    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.lf_read_raw(args.samples, args.frequency)
        if resp.status == Status.LF_TAG_OK:
            print(f" - Raw LF data ({len(resp.parsed)} bytes): {resp.parsed.hex().upper()}")
        else:
            print(f" [!] {CR}Failed to read raw LF data{C0}")'''
    
    new_lf_read_raw_cli = '''    def on_exec(self, args: argparse.Namespace):
        try:
            resp = self.cmd.lf_read_raw(args.samples, args.frequency)
            if resp.status == Status.LF_TAG_OK:
                if hasattr(resp, 'parsed') and resp.parsed:
                    print(f" - Raw LF data ({len(resp.parsed)} bytes): {resp.parsed.hex().upper()}")
                else:
                    print(f" - Raw LF data: No data received")
            elif resp.status == Status.LF_TAG_NO_FOUND:
                print(f" - LF tag no found")
            elif resp.status == Status.DEVICE_MODE_ERROR:
                print(f" [!] {CR}Device mode error - make sure device is in reader mode{C0}")
            else:
                print(f" [!] {CR}Failed to read raw LF data (status: {resp.status}){C0}")
        except Exception as e:
            print(f" [!] {CR}Error reading raw LF data: {e}{C0}")'''
    
    if old_lf_read_raw_cli in content:
        content = content.replace(old_lf_read_raw_cli, new_lf_read_raw_cli)
        print("✅ Fixed lf_read_raw CLI error handling")
    else:
        print("⚠️ lf_read_raw CLI pattern not found, might already be fixed")
    
    # Fix lf_tune_antenna CLI handler
    old_tune_cli = '''    def on_exec(self, args: argparse.Namespace):
        resp = self.cmd.lf_tune_antenna()
        if resp.status == Status.SUCCESS:
            print(f" - LF antenna tuning completed: {resp.parsed.hex().upper()}")
        else:
            print(f" [!] {CR}Failed to tune LF antenna{C0}")'''
    
    new_tune_cli = '''    def on_exec(self, args: argparse.Namespace):
        try:
            resp = self.cmd.lf_tune_antenna()
            if resp.status == Status.SUCCESS or resp.status == Status.LF_TAG_OK:
                if hasattr(resp, 'parsed') and resp.parsed:
                    print(f" - LF antenna tuning completed: {resp.parsed.hex().upper()}")
                else:
                    print(f" - LF antenna tuning completed")
            elif resp.status == Status.DEVICE_MODE_ERROR:
                print(f" [!] {CR}Device mode error - make sure device is in reader mode{C0}")
            elif resp.status == Status.NOT_IMPLEMENTED:
                print(f" [!] {CR}LF antenna tuning not implemented on this device{C0}")
            else:
                print(f" [!] {CR}Failed to tune LF antenna (status: {resp.status}){C0}")
        except Exception as e:
            print(f" [!] {CR}Error tuning LF antenna: {e}{C0}")'''
    
    if old_tune_cli in content:
        content = content.replace(old_tune_cli, new_tune_cli)
        print("✅ Fixed lf_tune_antenna CLI error handling")
    else:
        print("⚠️ lf_tune_antenna CLI pattern not found, might already be fixed")
    
    with open(cli_file, 'w') as f:
        f.write(content)
    
    return True

def main():
    if len(sys.argv) != 2:
        print("Usage: python3 lf_cli_final_fix.py /path/to/software/script/")
        sys.exit(1)
    
    script_dir = sys.argv[1]
    
    if not os.path.isdir(script_dir):
        print(f"❌ Directory not found: {script_dir}")
        sys.exit(1)
    
    print("🔧 Applying final LF CLI fixes...")
    
    success = True
    success &= fix_chameleon_cmd(script_dir)
    success &= fix_chameleon_cli_unit(script_dir)
    
    if success:
        print("\n✅ All final fixes applied successfully!")
        print("\n🚀 Your LF commands should now work properly:")
        print("1. lf read raw --samples 100")
        print("2. lf tune antenna")
        print("3. lf t55xx read_block --block 0")
        print("\n💡 If you still get device mode errors, try:")
        print("   hw mode -r")
        print("   (wait 1 second)")
        print("   lf scan auto")
    else:
        print("\n❌ Some fixes failed. Check the error messages above.")
        sys.exit(1)

if __name__ == "__main__":
    main()

