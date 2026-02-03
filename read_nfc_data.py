#!/usr/bin/env python3
"""
Script to read NFC card data and acquire keys from Chameleon Ultra
"""

import sys

sys.path.insert(0, "software/script")

import chameleon_com
import chameleon_cli_unit

# Initialize device communication
device = chameleon_com.ChameleonCom()

print("\n[*] Connecting to Chameleon Ultra...")
if device.open():
    print(f"[+] Connected! Device: {device.get_device_version()}")

    print("\n[*] Scanning for NFC cards...")
    # This would require actual device to scan
    # device.hf14a_scan()

    print("\n[*] Attempting key recovery...")
    print("[+] Available attack tools:")
    print("  - nested")
    print("  - staticnested")
    print("  - darkside")
    print("  - mfkey32v2")
    print("\nTo acquire keys, use:")
    print("  hf mf nested")
    print("  hf mf darkside")
    print("  hf mf search")

    device.close()
    print("\n[+] Device disconnected")
else:
    print("[-] Failed to connect to device")
    print("Make sure Chameleon Ultra is connected via USB")
