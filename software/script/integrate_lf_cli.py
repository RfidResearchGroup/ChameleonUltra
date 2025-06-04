#!/usr/bin/env python3
"""
Chameleon Ultra LF CLI Integration Guide

This script demonstrates how to integrate the enhanced LF CLI components
with the existing Chameleon Ultra CLI system.

Author: Manus AI
Date: June 2, 2025
Version: 1.0
"""

import sys
import os
from pathlib import Path

def integrate_lf_cli():
    """
    Integration function that modifies the existing CLI to include LF support
    """

    print("Chameleon Ultra LF CLI Integration")
    print("=" * 50)
    print()

    # Step 1: Verify existing installation
    print("Step 1: Verifying existing Chameleon Ultra installation...")

    required_files = [
        'chameleon_cli_main.py',
        'chameleon_cli_unit.py',
        'chameleon_cmd.py',
        'chameleon_com.py',
        'chameleon_enum.py',
        'chameleon_utils.py'
    ]

    missing_files = []
    for file in required_files:
        if not os.path.exists(file):
            missing_files.append(file)

    if missing_files:
        print(f"ERROR: Missing required files: {', '.join(missing_files)}")
        print("Please ensure the standard Chameleon Ultra CLI is properly installed.")
        return False

    print("✓ All required files found")

    # Step 2: Install LF CLI components
    print("\nStep 2: Installing LF CLI components...")

    lf_components = [
        'chameleon_cli_lf_enhanced.py',
        'chameleon_lf_protocols.py',
        'chameleon_lf_commands.py'
    ]

    for component in lf_components:
        if os.path.exists(component):
            print(f"✓ {component} found")
        else:
            print(f"✗ {component} missing")
            return False

    # Step 3: Create integration module
    print("\nStep 3: Creating integration module...")

    integration_code = '''#!/usr/bin/env python3
"""
Chameleon Ultra Enhanced CLI with LF Support

This is the main entry point for the enhanced CLI that includes
comprehensive Low Frequency (LF) protocol support.

Usage: python3 chameleon_cli_enhanced.py
"""

import sys
import os

# Import existing Chameleon modules
from chameleon_cli_main import *
from chameleon_cli_unit import *

# Import LF enhancements
from chameleon_cli_lf_enhanced import *
from chameleon_lf_protocols import *
from chameleon_lf_commands import extend_chameleon_cmd

def main():
    """Enhanced main function with LF support"""

    # Extend the ChameleonCMD class with LF commands
    extend_chameleon_cmd()

    # Initialize the enhanced CLI
    print("Chameleon Ultra Enhanced CLI with LF Support")
    print("Version 1.0 - Enhanced by Manus AI")
    print()

    # Check for LF support
    try:
        # Test LF command availability
        test_cmd = ChameleonCMD(None)
        if hasattr(test_cmd, 'em410x_scan'):
            print("✓ LF protocol support enabled")
        else:
            print("✗ LF protocol support not available")
    except:
        print("! LF support status unknown")

    print()

    # Start the standard CLI with LF enhancements
    cli = ChameleonCLI()
    cli.startCLI()

if __name__ == "__main__":
    main()
'''

    with open('chameleon_cli_enhanced.py', 'w') as f:
        f.write(integration_code)

    print("✓ Integration module created: chameleon_cli_enhanced.py")

    # Step 4: Create command reference
    print("\nStep 4: Creating command reference...")

    command_ref = '''# Chameleon Ultra LF CLI Command Reference

## Quick Reference

### General LF Commands
- `lf info` - Display LF protocol information
- `lf scan` - Scan for LF cards (auto-detect protocol)

### EM410x Commands
- `lf em 410x read` - Read EM410x card
- `lf em 410x write --id <hex>` - Write EM410x to T55xx
- `lf em 410x econfig --id <hex>` - Configure EM410x emulation

### T5577 Commands
- `lf t55xx info` - Display T5577 card information
- `lf t55xx read --block <0-7>` - Read T5577 block
- `lf t55xx write --block <0-7> --data <hex>` - Write T5577 block
- `lf t55xx config --modulation <type>` - Configure T5577

### HID Prox Commands
- `lf hid read` - Read HID Prox card
- `lf hid write --facility <num> --card <num>` - Write HID to T55xx
- `lf hid econfig --facility <num> --card <num>` - Configure HID emulation

### Indala Commands
- `lf indala read` - Read Indala card
- `lf indala write --id <hex>` - Write Indala to T55xx
- `lf indala econfig --id <hex>` - Configure Indala emulation

## Examples

### Clone an EM410x card:
```bash
lf em 410x read --verbose
lf em 410x write --id 1234567890 --verify
```

### Configure T5577 for HID emulation:
```bash
lf t55xx config --modulation fsk1 --bitrate 2
lf t55xx write --block 1 --data 2006EC8C
```

### Set up HID Prox emulation:
```bash
lf hid econfig --slot 1 --facility 123 --card 4567
hw mode emulation
hw slot select --slot 1
```
'''

    with open('LF_COMMAND_REFERENCE.md', 'w') as f:
        f.write(command_ref)

    print("✓ Command reference created: LF_COMMAND_REFERENCE.md")

    # Step 5: Create installation script
    print("\nStep 5: Creating installation script...")

    install_script = '''#!/bin/bash
# Chameleon Ultra LF CLI Installation Script

echo "Installing Chameleon Ultra LF CLI Enhancement..."

# Check Python version
python_version=$(python3 -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
echo "Python version: $python_version"

if [[ $(echo "$python_version >= 3.9" | bc -l) -eq 0 ]]; then
    echo "ERROR: Python 3.9 or later required"
    exit 1
fi

# Check for existing installation
if [ ! -f "chameleon_cli_main.py" ]; then
    echo "ERROR: Standard Chameleon Ultra CLI not found"
    echo "Please install the standard CLI first"
    exit 1
fi

# Install LF components
echo "Installing LF CLI components..."
chmod +x chameleon_cli_enhanced.py
chmod +x chameleon_cli_lf_enhanced.py
chmod +x chameleon_lf_protocols.py
chmod +x chameleon_lf_commands.py

echo "Installation completed successfully!"
echo ""
echo "Usage:"
echo "  python3 chameleon_cli_enhanced.py"
echo ""
echo "For help:"
echo "  python3 chameleon_cli_enhanced.py"
echo "  > help"
echo "  > lf info"
'''

    with open('install_lf_cli.sh', 'w') as f:
        f.write(install_script)

    os.chmod('install_lf_cli.sh', 0o755)
    print("✓ Installation script created: install_lf_cli.sh")

    # Step 6: Create test script
    print("\nStep 6: Creating test script...")

    test_script = '''#!/usr/bin/env python3
"""
Test script for Chameleon Ultra LF CLI
"""

import sys
import subprocess
import time

def run_test_command(command, description):
    """Run a test command and report results"""
    print(f"Testing: {description}")
    print(f"Command: {command}")

    try:
        result = subprocess.run(
            command.split(),
            capture_output=True,
            text=True,
            timeout=10
        )

        if result.returncode == 0:
            print("✓ PASS")
        else:
            print("✗ FAIL")
            print(f"Error: {result.stderr}")

    except subprocess.TimeoutExpired:
        print("✗ TIMEOUT")
    except Exception as e:
        print(f"✗ ERROR: {e}")

    print("-" * 40)

def main():
    """Run LF CLI tests"""
    print("Chameleon Ultra LF CLI Test Suite")
    print("=" * 50)

    # Test basic CLI functionality
    tests = [
        ("python3 chameleon_cli_enhanced.py --help", "CLI help"),
        ("python3 -c 'from chameleon_cli_lf_enhanced import *; print(\"LF framework loaded\")'", "LF framework import"),
        ("python3 -c 'from chameleon_lf_protocols import *; print(\"LF protocols loaded\")'", "LF protocols import"),
        ("python3 -c 'from chameleon_lf_commands import *; print(\"LF commands loaded\")'", "LF commands import"),
    ]

    for command, description in tests:
        run_test_command(command, description)

    print("Test suite completed")

if __name__ == "__main__":
    main()
'''

    with open('test_lf_cli.py', 'w') as f:
        f.write(test_script)

    os.chmod('test_lf_cli.py', 0o755)
    print("✓ Test script created: test_lf_cli.py")

    print("\nIntegration completed successfully!")
    print()
    print("Next steps:")
    print("1. Run: ./install_lf_cli.sh")
    print("2. Test: python3 test_lf_cli.py")
    print("3. Start: python3 chameleon_cli_enhanced.py")
    print()
    print("For documentation, see:")
    print("- LF_COMMAND_REFERENCE.md")
    print("- lf_cli_user_guide.md")

    return True

if __name__ == "__main__":
    integrate_lf_cli()
