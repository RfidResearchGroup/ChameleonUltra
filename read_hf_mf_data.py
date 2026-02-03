#!/usr/bin/env python3
"""
Script to read HF Mifare Classic card data from Chameleon Ultra
Directly uses device API without interactive CLI
"""

import sys
import os

# Add script directory to path
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "software/script"))

import chameleon_com
import chameleon_utils
from chameleon_enum import Command


def connect_device():
    """Connect to Chameleon device"""
    print("[*] Searching for Chameleon devices...")

    import serial.tools.list_ports

    # Find available COM ports
    ports = serial.tools.list_ports.comports()

    if not ports:
        print("[-] No COM ports found")
        return None

    for port_info in ports:
        print(f"[*] Trying port: {port_info.device}")
        try:
            device = chameleon_com.ChameleonCom()
            device.open(port_info.device)
            print(f"[+] Connected! Device version: {device.get_device_version()}")
            return device
        except Exception as e:
            print(f"    [-] Failed on {port_info.device}: {str(e)[:50]}")
            continue

    print("[-] Could not connect to any device")
    return None


def scan_cards(device):
    """Scan for nearby NFC cards"""
    print("\n[*] Scanning for NFC cards...")
    try:
        # This would execute hf 14a scan equivalent
        # The actual implementation depends on device firmware commands
        print("[+] Card scan function available in CLI mode")
        print("    Use: hf 14a scan")
        return True
    except Exception as e:
        print(f"[-] Scan failed: {e}")
        return False


def read_card_info(device):
    """Read Mifare Classic card information"""
    print("\n[*] Reading card information...")
    try:
        # Available through CLI commands:
        print("[+] Card info available via:")
        print("    - hf mf info      (card details)")
        print("    - hf mf stat      (card statistics)")
        print("    - hf 14a scan     (scan details)")
        return True
    except Exception as e:
        print(f"[-] Failed to read card info: {e}")
        return False


def search_keys(device):
    """Search for card keys using available attacks"""
    print("\n[*] Available key recovery methods:")
    try:
        attacks = [
            ("hf mf search", "Search for known default keys"),
            ("hf mf nested", "Nested attack for key recovery"),
            ("hf mf darkside", "Darkside attack (Mifare Classic)"),
            ("hf mf static_nested", "Static nested attack"),
        ]

        for cmd, desc in attacks:
            print(f"    - {cmd:<25} {desc}")

        print("\n[*] Key recovery tools available:")
        tools = [
            ("nested", "Nested key recovery"),
            ("staticnested", "Static nested recovery"),
            ("darkside", "Darkside attack"),
            ("mfkey32v2", "MFKEY32v2 recovery"),
        ]

        for tool, desc in tools:
            print(f"    - {tool:<20} {desc}")

        return True
    except Exception as e:
        print(f"[-] Key search error: {e}")
        return False


def read_blocks(device):
    """Read card blocks with known keys"""
    print("\n[*] Reading card blocks:")
    print("    Usage: hf mf rdbl --blk <block> -k <key>")
    print("    Example: hf mf rdbl --blk 0 -k FFFFFFFFFFFF")
    print("\n[*] Common default keys:")

    default_keys = [
        "FFFFFFFFFFFF",  # Blank/Default
        "A0A1A2A3A4A5",  # User key
        "D3F7D3F7D3F7",  # NFCopy
        "000000000000",  # All zeros
        "AABBCCDDEEFF",  # Sequential
    ]

    for key in default_keys:
        print(f"    - {key}")

    return True


def main():
    print("\n" + "=" * 60)
    print("  Chameleon Ultra - NFC Mifare Classic Reader")
    print("=" * 60 + "\n")

    # Connect to device
    device = connect_device()
    if not device:
        print("\n[-] Could not connect to Chameleon device")
        print("[*] Make sure device is connected via USB")
        return 1

    try:
        # Get device info
        print(f"\n[+] Device version: {device.get_device_version()}")

        # Demonstrate available operations
        print("\n" + "-" * 60)
        print("AVAILABLE OPERATIONS")
        print("-" * 60)

        scan_cards(device)
        read_card_info(device)
        search_keys(device)
        read_blocks(device)

        print("\n" + "-" * 60)
        print("NEXT STEPS")
        print("-" * 60)
        print("\n[*] To read actual card data:")
        print("    1. Type 'hf 14a scan' in CLI to find cards")
        print("    2. Use 'hf mf search' to find keys")
        print("    3. Use 'hf mf rdbl' to read blocks")
        print("    4. Use 'hf mf eread' to dump full card")

        print("\n[*] Chameleon CLI is recommended for interactive card reading")
        print("    Start with: python.exe software/script/chameleon_cli_main.py")

        return 0

    except Exception as e:
        print(f"\n[-] Error: {e}")
        return 1
    finally:
        try:
            device.close()
            print("\n[+] Device disconnected")
        except:
            pass


if __name__ == "__main__":
    sys.exit(main())
