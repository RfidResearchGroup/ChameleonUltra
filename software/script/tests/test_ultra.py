#!/usr/bin/env python3

import sys
import re
import unittest
sys.path.append('..')

from chameleon_cli_main import ChameleonCLI        # noqa: E402
import chameleon_cli_unit                          # noqa: E402
from tests.output_grabber import OutputGrabber     # noqa: E402


class TestCLI(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        chameleon_cli_unit.check_tools()
        cls.cli = ChameleonCLI()

    @classmethod
    def tearDownClass(cls):
        try:
            print()
            cls.cli.exec_cmd('exit')
        except SystemExit:
            pass

    def eval(self, cmd_str):
        out = OutputGrabber()
        with out:
            self.cli.exec_cmd(cmd_str)
        return out.captured_text

    def r(self, cmd, pattern):
        result = self.eval(cmd)
        self.assertIsNotNone(re.search(pattern, result, re.DOTALL), f"\nPattern '{pattern}' not found in \n{result}")

    def test_rem(self):
        self.r('rem foo bar',
               r'[0-9T:\.-]{26}Z.*remark: foo bar')

    def test_000_info(self):
        self.r('hw version',
               r'Please connect')
        self.r('hw connect',
               r'Chameleon Ultra connected: v[0-9]+\.[0-9]+')
        self.r('hw chipid',
               r'Device chip ID: [0-9a-f]{16}')
        self.r('hw address',
               r'Device address: [0-9a-f]{12}')
        self.r('hw version',
               r'Chameleon Ultra, Version: v[0-9]+\.[0-9]+ \(v[0-9\.a-g-]+\)')
        self.r('hw battery',
               r'voltage.*[0-9]+ mV\n.*percentage.*[0-9]+%')
        self.r('hw raw -c GET_APP_VERSION',
               r'0x68 SUCCESS: Device operation succeeded.*: [0-9a-fA-F]{4}')
        self.eval('hw disconnect')

    def test_010_mode(self):
        self.eval('hw connect')
        self.r('hw mode',
               r'Tag Emulator')
        self.r('hw mode -r',
               r'Tag Reader.*successfully')
        self.r('hw mode',
               r'Tag Reader')
        self.r('hw mode -e',
               r'Tag Emulator.*successfully')
        self.eval('hw disconnect')

    @unittest.skip("factory reset skipped")
    def test_020_factory(self):
        self.eval('hw connect')
        self.r('hw connect',
               r'Chameleon Ultra connected: v[0-9]+\.[0-9]+')
        self.r('hw factory_reset',
               r'really sure')
        self.r('hw factory_reset --force',
               r'Reset successful')

    def test_030_settings(self):
        self.eval('hw connect')
        self.r('hw settings reset',
               r'really sure')
        self.r('hw settings reset --force',
               r'Reset success')
        self.r('hw settings animation',
               r'Full animation')
        self.r('hw settings animation -m NONE',
               r'Animation mode change success')
        self.r('hw settings animation',
               r'No animation')
        self.r('hw settings btnpress',
               r'B long.*Show Battery Level')
        self.r('hw settings btnpress -a -l -f NONE',
               r'Successfully')
        self.r('hw settings btnpress',
               r'A long.*No Function')
        self.r('hw settings btnpress -a',
               r'A long.*No Function')
        self.r('hw settings btnpress -l',
               r'A long.*No Function')
        self.r('hw settings btnpress -a -l',
               r'A long.*No Function')
        self.r('hw settings blekey',
               r'123456')
        self.r('hw settings blekey -k 654321',
               r'Successfully')
        self.r('hw settings blekey',
               r'654321')
        self.r('hw settings blepair',
               r'Disabled')
        self.r('hw settings blepair -e',
               r'Successfully')
        self.r('hw settings blepair',
               r'Enabled')
        self.r('hw settings store',
               r'Store success')
        self.r('hw settings reset --force',
               r'Reset success')
        self.eval('hw disconnect')

    def test_040_slots(self):
        self.eval('hw connect')
        self.r('hw slot list',
               r' Slot 1:.*active'
               r'.*Mifare Classic 1k'
               r'.*DEADBEEF'
               r'.*EM410X'
               r'.*DEADBEEF88')
        self.eval('hw disconnect')


if __name__ == '__main__':
    unittest.main()
