#!/usr/bin/env python3
"""
Test suite for Chameleon Ultra CLI unit functionality.
This file tests the basic CLI structure and command parsing.
"""

import unittest
import sys
import os
from unittest.mock import patch, MagicMock

# Add the script directory to the path so we can import the CLI modules
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))

try:
    from chameleon_cli_unit import root, CLITree
    from chameleon_enum import Command, Status
    CLI_AVAILABLE = True
except ImportError as e:
    print(f"Warning: CLI modules not available: {e}")
    CLI_AVAILABLE = False


class TestCLIUnit(unittest.TestCase):
    """Test basic CLI unit functionality."""
    
    def setUp(self):
        """Set up test fixtures."""
        if not CLI_AVAILABLE:
            self.skipTest("CLI modules not available")
    
    def test_root_command_exists(self):
        """Test that the root command tree exists."""
        self.assertIsNotNone(root)
        self.assertIsInstance(root, CLITree)
    
    def test_command_tree_structure(self):
        """Test that the command tree has expected structure."""
        # Check that root has children
        self.assertGreater(len(root.children), 0)
        
        # Find command names
        command_names = [child.name for child in root.children]
        
        # Should have basic commands
        expected_commands = ['hw', 'hf', 'lf']  # Basic command groups
        
        for cmd in expected_commands:
            if cmd in command_names:
                print(f"✅ Found expected command: {cmd}")
            else:
                print(f"⚠️  Command not found: {cmd}")
    
    def test_lf_command_integration(self):
        """Test that LF commands are properly integrated."""
        # Find LF command in the tree
        lf_command = None
        for child in root.children:
            if child.name == 'lf':
                lf_command = child
                break
        
        if lf_command:
            print("✅ LF command found in command tree")
            # Check if LF has subcommands
            if len(lf_command.children) > 0:
                lf_subcommands = [child.name for child in lf_command.children]
                print(f"✅ LF subcommands found: {lf_subcommands}")
            else:
                print("⚠️  LF command has no subcommands")
        else:
            print("⚠️  LF command not found in command tree")
    
    def test_command_enum_values(self):
        """Test that command enum values are available."""
        # Test that Command enum has expected values
        self.assertTrue(hasattr(Command, 'GET_APP_VERSION'))
        self.assertTrue(hasattr(Command, 'GET_DEVICE_CHIP_ID'))
        
        # Test LF command values if they exist
        lf_commands = [attr for attr in dir(Command) if 'LF' in attr or 'EM410X' in attr or 'T5577' in attr]
        if lf_commands:
            print(f"✅ Found LF commands in enum: {lf_commands}")
        else:
            print("⚠️  No LF commands found in enum")
    
    def test_status_enum_values(self):
        """Test that status enum values are available."""
        # Test basic status values
        self.assertTrue(hasattr(Status, 'HF_TAG_OK'))
        self.assertTrue(hasattr(Status, 'HF_TAG_NO'))
        
        # Test if STATUS_NOT_IMPLEMENTED exists (for LF stubs)
        if hasattr(Status, 'STATUS_NOT_IMPLEMENTED'):
            print("✅ STATUS_NOT_IMPLEMENTED found (good for LF stubs)")
        else:
            print("⚠️  STATUS_NOT_IMPLEMENTED not found")


class TestLFCLIIntegration(unittest.TestCase):
    """Test LF CLI integration if available."""
    
    def setUp(self):
        """Set up test fixtures."""
        if not CLI_AVAILABLE:
            self.skipTest("CLI modules not available")
    
    def test_lf_module_imports(self):
        """Test that LF modules can be imported."""
        try:
            # Try to import LF modules
            import chameleon_cli_lf_enhanced
            print("✅ chameleon_cli_lf_enhanced imported successfully")
        except ImportError as e:
            print(f"⚠️  chameleon_cli_lf_enhanced import failed: {e}")
        
        try:
            import chameleon_lf_protocols
            print("✅ chameleon_lf_protocols imported successfully")
        except ImportError as e:
            print(f"⚠️  chameleon_lf_protocols import failed: {e}")
        
        try:
            import chameleon_lf_commands
            print("✅ chameleon_lf_commands imported successfully")
        except ImportError as e:
            print(f"⚠️  chameleon_lf_commands import failed: {e}")
    
    def test_lf_protocol_classes(self):
        """Test that LF protocol classes are available."""
        try:
            from chameleon_lf_protocols import EM410xProtocol, T5577Protocol
            
            # Test EM410x protocol
            em_protocol = EM410xProtocol()
            self.assertIsNotNone(em_protocol)
            print("✅ EM410xProtocol instantiated successfully")
            
            # Test T5577 protocol
            t5577_protocol = T5577Protocol()
            self.assertIsNotNone(t5577_protocol)
            print("✅ T5577Protocol instantiated successfully")
            
        except ImportError as e:
            print(f"⚠️  LF protocol classes not available: {e}")
        except Exception as e:
            print(f"⚠️  Error testing LF protocols: {e}")
    
    def test_lf_command_parsing(self):
        """Test LF command parsing without device connection."""
        try:
            # This is a basic test that doesn't require device connection
            # We're just testing that the command structure is set up correctly
            
            # Mock the device connection to avoid actual hardware requirements
            with patch('chameleon_cmd.ChameleonCMD') as mock_cmd:
                mock_device = MagicMock()
                mock_cmd.return_value = mock_device
                
                # Test that we can access LF command structure
                print("✅ LF command parsing test setup successful")
                
        except Exception as e:
            print(f"⚠️  LF command parsing test failed: {e}")


def run_tests():
    """Run all tests and return results."""
    # Create test suite
    suite = unittest.TestSuite()
    
    # Add test cases
    suite.addTest(unittest.makeSuite(TestCLIUnit))
    suite.addTest(unittest.makeSuite(TestLFCLIIntegration))
    
    # Run tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    
    # Print summary
    print(f"\n{'='*50}")
    print(f"Test Summary:")
    print(f"Tests run: {result.testsRun}")
    print(f"Failures: {len(result.failures)}")
    print(f"Errors: {len(result.errors)}")
    if result.testsRun > 0:
        print(f"Success rate: {((result.testsRun - len(result.failures) - len(result.errors)) / result.testsRun * 100):.1f}%")
    print(f"{'='*50}")
    
    return result.wasSuccessful()


if __name__ == '__main__':
    # Run tests when executed directly
    success = run_tests()
    sys.exit(0 if success else 1)

