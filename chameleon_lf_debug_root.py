#!/usr/bin/env python3
"""
ChameleonUltra LF Debug Tool - Root Directory Version
====================================================

Fixed version that works from the ChameleonUltra root directory
and handles missing dependencies gracefully.

Usage:
    python3 chameleon_lf_debug_root.py [--port PORT] [--test TEST_NAME]
"""

import sys
import os
import time
import struct
import argparse
from typing import Optional, Dict, List, Any
from dataclasses import dataclass
from enum import IntEnum

# Check and install dependencies
def check_dependencies():
    """Check for required dependencies and provide installation instructions."""
    missing_deps = []
    
    try:
        import serial
    except ImportError:
        missing_deps.append("pyserial")
    
    if missing_deps:
        print("❌ Missing required dependencies:")
        for dep in missing_deps:
            print(f"   - {dep}")
        print("\n🔧 To install missing dependencies:")
        print("   pip3 install pyserial")
        print("   # or")
        print("   pip install pyserial")
        print("\n💡 If you're using conda:")
        print("   conda install pyserial")
        return False
    return True

if not check_dependencies():
    sys.exit(1)

# Add the ChameleonUltra script directory to path
script_dir = os.path.dirname(os.path.abspath(__file__))
chameleon_dir = os.path.join(script_dir, "software", "script")
if os.path.exists(chameleon_dir):
    sys.path.insert(0, chameleon_dir)
else:
    # Try current directory if we're already in the script directory
    if os.path.exists("chameleon_cmd.py"):
        sys.path.insert(0, ".")
    elif os.path.exists("software/script/chameleon_cmd.py"):
        sys.path.insert(0, "software/script")
    else:
        print("❌ Cannot find ChameleonUltra script directory")
        print("Make sure this script is in the ChameleonUltra root directory")
        sys.exit(1)

try:
    from chameleon_cmd import ChameleonCMD
    from chameleon_enum import Command, Status
    from chameleon_com import ChameleonCom as ChameleonDevice
except ImportError as e:
    print(f"❌ Failed to import ChameleonUltra modules: {e}")
    print("Make sure this script is in the ChameleonUltra root directory")
    print("and that all Python files are present in software/script/")
    sys.exit(1)

# Enhanced Status Codes (missing from CLI)
class LFStatus(IntEnum):
    LF_TAG_OK = 0x40
    EM410X_TAG_NO_FOUND = 0x41  
    LF_TAG_NO_FOUND = 0x42      # Missing from CLI!
    PAR_ERR = 0x60
    DEVICE_MODE_ERROR = 0x66
    INVALID_CMD = 0x67
    SUCCESS = 0x68
    NOT_IMPLEMENTED = 0x69

@dataclass
class TestResult:
    name: str
    success: bool
    message: str
    details: Optional[Dict[str, Any]] = None
    error: Optional[str] = None

class ChameleonLFDebugger:
    def __init__(self, port: Optional[str] = None):
        self.port = port
        self.device: Optional[ChameleonDevice] = None
        self.cmd: Optional[ChameleonCMD] = None
        self.results: List[TestResult] = []
        
    def log(self, message: str, level: str = "INFO"):
        """Log a message with timestamp and level."""
        timestamp = time.strftime("%H:%M:%S")
        symbols = {
            "INFO": "ℹ️",
            "SUCCESS": "✅", 
            "WARNING": "⚠️",
            "ERROR": "❌",
            "DEBUG": "🔍"
        }
        symbol = symbols.get(level, "📝")
        print(f"[{timestamp}] {symbol} {message}")
        
    def connect_device(self) -> TestResult:
        """Test device connection."""
        self.log("Testing device connection...")
        
        try:
            # Try to connect to device
            self.device = ChameleonDevice()
            
            if self.port:
                self.device.open(self.port)
            else:
                # Try to auto-detect common macOS ports
                import glob
                possible_ports = glob.glob('/dev/tty.usbmodem*')
                if possible_ports:
                    self.port = possible_ports[0]
                    self.log(f"Auto-detected port: {self.port}")
                    self.device.open(self.port)
                else:
                    raise Exception("No USB modem ports found. Please specify --port manually.")
                
            self.cmd = ChameleonCMD(self.device)
            
            # Test basic communication
            version = self.cmd.get_app_version()
            chip_id = self.cmd.get_device_chip_id()
            
            self.log(f"Connected successfully!", "SUCCESS")
            self.log(f"App Version: {version}")
            self.log(f"Chip ID: {chip_id}")
            
            return TestResult(
                name="connection",
                success=True,
                message="Device connected successfully",
                details={"version": version, "chip_id": chip_id, "port": self.port}
            )
            
        except Exception as e:
            self.log(f"Connection failed: {e}", "ERROR")
            return TestResult(
                name="connection", 
                success=False,
                message="Failed to connect to device",
                error=str(e)
            )
    
    def test_status_codes(self) -> TestResult:
        """Test status code recognition."""
        self.log("Testing status code recognition...")
        
        # Test known status codes
        known_codes = {
            0x40: "LF_TAG_OK",
            0x41: "EM410X_TAG_NO_FOUND", 
            0x42: "LF_TAG_NO_FOUND",
            0x60: "PAR_ERR",
            0x66: "DEVICE_MODE_ERROR",
            0x68: "SUCCESS",
            0x69: "NOT_IMPLEMENTED"
        }
        
        missing_codes = []
        recognized_codes = []
        
        for code, name in known_codes.items():
            try:
                status = Status(code)
                self.log(f"✅ Status {name} (0x{code:02X}): {status}", "DEBUG")
                recognized_codes.append((code, name))
            except ValueError:
                missing_codes.append((code, name))
                self.log(f"❌ Missing status code: {name} (0x{code:02X})", "WARNING")
        
        if missing_codes:
            return TestResult(
                name="status_codes",
                success=False, 
                message=f"Missing {len(missing_codes)} status codes in CLI",
                details={
                    "missing_codes": missing_codes,
                    "recognized_codes": recognized_codes
                }
            )
        else:
            return TestResult(
                name="status_codes",
                success=True,
                message="All status codes recognized",
                details={"recognized_codes": recognized_codes}
            )
    
    def test_device_mode(self) -> TestResult:
        """Test device mode switching."""
        self.log("Testing device mode switching...")
        
        if not self.cmd:
            return TestResult(
                name="device_mode",
                success=False,
                message="No device connection",
                error="Device not connected"
            )
        
        try:
            # Get current mode
            current_mode = self.cmd.get_device_mode()
            self.log(f"Current mode: {current_mode}")
            
            # Switch to reader mode for LF operations
            self.cmd.set_device_mode(1)  # Reader mode
            time.sleep(0.5)
            
            new_mode = self.cmd.get_device_mode()
            self.log(f"New mode: {new_mode}")
            
            return TestResult(
                name="device_mode",
                success=True,
                message="Device mode switching works",
                details={"original_mode": current_mode, "new_mode": new_mode}
            )
            
        except Exception as e:
            self.log(f"Device mode test failed: {e}", "ERROR")
            return TestResult(
                name="device_mode",
                success=False,
                message="Device mode switching failed",
                error=str(e)
            )
    
    def test_raw_command(self, cmd_id: int, data: bytes = b"", timeout: int = 5) -> TestResult:
        """Test a raw command and analyze the response."""
        self.log(f"Testing raw command {cmd_id} (0x{cmd_id:04X})...")
        
        if not self.device:
            return TestResult(
                name=f"raw_cmd_{cmd_id}",
                success=False,
                message="No device connection",
                error="Device not connected"
            )
        
        try:
            # Send raw command
            resp = self.device.send_cmd_sync(cmd_id, data, timeout)
            
            # Analyze response
            if hasattr(resp, 'status'):
                status_code = resp.status
                status_name = f"0x{status_code:02X}"
                try:
                    status_enum = Status(status_code)
                    status_name = f"{status_enum.name} (0x{status_code:02X})"
                except ValueError:
                    status_name = f"UNKNOWN (0x{status_code:02X})"
                
                self.log(f"Response status: {status_name}")
                
                if hasattr(resp, 'data') and resp.data:
                    self.log(f"Response data ({len(resp.data)} bytes): {resp.data.hex().upper()}")
                
                return TestResult(
                    name=f"raw_cmd_{cmd_id}",
                    success=True,
                    message=f"Command executed, status: {status_name}",
                    details={
                        "command_id": cmd_id,
                        "status_code": status_code,
                        "status_name": status_name,
                        "data_length": len(resp.data) if hasattr(resp, 'data') else 0
                    }
                )
            else:
                # Raw bytes response
                self.log(f"Raw response ({len(resp)} bytes): {resp.hex().upper()}")
                return TestResult(
                    name=f"raw_cmd_{cmd_id}",
                    success=False,
                    message="Received raw bytes instead of structured response",
                    details={"raw_response": resp.hex()}
                )
                
        except Exception as e:
            self.log(f"Raw command {cmd_id} failed: {e}", "ERROR")
            return TestResult(
                name=f"raw_cmd_{cmd_id}",
                success=False,
                message=f"Command failed: {e}",
                error=str(e)
            )
    
    def test_lf_commands(self) -> List[TestResult]:
        """Test all LF commands systematically."""
        self.log("Testing LF commands...")
        
        results = []
        
        # LF Command definitions
        lf_commands = {
            3000: ("EM410X_SCAN", b""),
            3001: ("EM410X_WRITE_TO_T55XX", struct.pack("!Q", 0x1234567890)),
            3002: ("T55XX_READ_BLOCK", struct.pack("!B", 0)),  # Block 0
            3003: ("T55XX_WRITE_BLOCK", struct.pack("!BI", 1, 0x12345678)),  # Block 1, data
            3004: ("HID_PROX_SCAN", b""),
            3005: ("HID_PROX_WRITE_TO_T55XX", struct.pack("!II", 123, 456)),  # Facility, card
            3006: ("INDALA_SCAN", b""),
            3007: ("LF_SCAN_AUTO", b""),
            3008: ("LF_READ_RAW", struct.pack("!I", 100)),  # 100 samples
            3009: ("LF_TUNE_ANTENNA", b"")
        }
        
        for cmd_id, (cmd_name, cmd_data) in lf_commands.items():
            self.log(f"Testing {cmd_name}...")
            result = self.test_raw_command(cmd_id, cmd_data)
            result.name = cmd_name.lower()
            results.append(result)
            time.sleep(0.5)  # Small delay between commands
            
        return results
    
    def run_test_suite(self, test_name: str = "all") -> List[TestResult]:
        """Run the specified test suite."""
        self.log(f"🚀 Starting ChameleonUltra LF Debug Suite: {test_name}")
        self.results = []
        
        # Status codes test (can run without device)
        if test_name in ["all", "status_codes"]:
            self.results.append(self.test_status_codes())
        
        # Connection test (required for device tests)
        if test_name in ["all", "connection", "device_mode", "lf_commands"]:
            connection_result = self.connect_device()
            self.results.append(connection_result)
            
            if not connection_result.success:
                self.log("Cannot proceed with device tests without connection", "ERROR")
                return self.results
        
        # Device-dependent tests
        if test_name in ["all", "device_mode"] and any(r.name == "connection" and r.success for r in self.results):
            self.results.append(self.test_device_mode())
            
        if test_name in ["all", "lf_commands"] and any(r.name == "connection" and r.success for r in self.results):
            self.results.extend(self.test_lf_commands())
        
        return self.results
    
    def generate_report(self) -> str:
        """Generate a comprehensive test report."""
        total_tests = len(self.results)
        passed_tests = sum(1 for r in self.results if r.success)
        failed_tests = total_tests - passed_tests
        
        report = f"""
🔍 ChameleonUltra LF Debug Report
================================

📊 Summary:
- Total Tests: {total_tests}
- Passed: {passed_tests} ✅
- Failed: {failed_tests} ❌
- Success Rate: {(passed_tests/total_tests*100):.1f}%

📋 Detailed Results:
"""
        
        for result in self.results:
            status_icon = "✅" if result.success else "❌"
            report += f"\n{status_icon} {result.name}: {result.message}"
            
            if result.details:
                for key, value in result.details.items():
                    if key == "missing_codes" and value:
                        report += f"\n   - Missing status codes:"
                        for code, name in value:
                            report += f"\n     * {name} (0x{code:02X})"
                    elif key == "recognized_codes" and value:
                        report += f"\n   - Recognized status codes: {len(value)}"
                    else:
                        report += f"\n   - {key}: {value}"
                    
            if result.error:
                report += f"\n   ❌ Error: {result.error}"
        
        # Recommendations
        report += f"\n\n💡 Recommendations:\n"
        
        connection_failed = any(r.name == "connection" and not r.success for r in self.results)
        if connection_failed:
            report += "- Check device connection and USB cable\n"
            report += "- Try specifying port manually: --port /dev/tty.usbmodem*\n"
            report += "- Make sure device is in bootloader or application mode\n"
            
        status_test = next((r for r in self.results if r.name == "status_codes"), None)
        if status_test and not status_test.success:
            report += "- Run the CLI fix script: python3 fix_lf_cli.py software/script/\n"
            
        device_mode_failed = any(r.name == "device_mode" and not r.success for r in self.results)
        if device_mode_failed:
            report += "- Switch to reader mode: hw mode -r\n"
        
        return report
    
    def disconnect(self):
        """Disconnect from device."""
        if self.device:
            try:
                self.device.close()
                self.log("Device disconnected", "SUCCESS")
            except:
                pass

def main():
    parser = argparse.ArgumentParser(description="ChameleonUltra LF Debug Tool")
    parser.add_argument("--port", "-p", help="Serial port (auto-detect if not specified)")
    parser.add_argument("--test", "-t", default="all", 
                       choices=["all", "connection", "status_codes", "device_mode", "lf_commands"],
                       help="Test to run (default: all)")
    parser.add_argument("--output", "-o", help="Save report to file")
    
    args = parser.parse_args()
    
    # Create debugger
    debugger = ChameleonLFDebugger(args.port)
    
    try:
        # Run tests
        results = debugger.run_test_suite(args.test)
        
        # Generate report
        report = debugger.generate_report()
        print(report)
        
        # Save report if requested
        if args.output:
            with open(args.output, 'w') as f:
                f.write(report)
            print(f"\n📄 Report saved to: {args.output}")
        
        # Exit with appropriate code
        failed_tests = sum(1 for r in results if not r.success)
        sys.exit(failed_tests)
        
    except KeyboardInterrupt:
        print("\n\n⚠️ Test interrupted by user")
        sys.exit(1)
    except Exception as e:
        print(f"\n❌ Unexpected error: {e}")
        sys.exit(1)
    finally:
        debugger.disconnect()

if __name__ == "__main__":
    main()

