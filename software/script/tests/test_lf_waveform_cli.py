import contextlib
import io
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from chameleon_cli_unit import DataWaveformExport, DataWaveformInfo, LFGenericCapture
from chameleon_utils import ArgsParserError
from lf_waveform import load_waveform, make_waveform, save_waveform
from tests.test_lf_waveform import FakeDevice


class WaveformCLITest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)

    def run_unit(self, unit, *arguments):
        args = unit.args_parser().parse_args(list(arguments))
        with contextlib.redirect_stdout(io.StringIO()) as output:
            unit.on_exec(args)
        return output.getvalue()

    def test_capture_checks_path_before_any_device_command(self):
        unit = LFGenericCapture()
        device = FakeDevice()
        unit.device_com = device
        target = self.folder / "exists"
        target.write_bytes(b"keep")
        with self.assertRaises(ArgsParserError):
            self.run_unit(unit, "-o", str(target))
        self.assertEqual(device.calls, [])

    def test_capture_publishes_and_restores_mode(self):
        unit = LFGenericCapture()
        device = FakeDevice()
        unit.device_com = device
        target = self.folder / "capture.json"
        text = self.run_unit(unit, "-o", str(target), "--samples", "1000")
        self.assertIn("1000 samples", text)
        self.assertEqual(load_waveform(target)["sample_count"], 1000)
        self.assertEqual(device.mode, 0)

    def test_offline_info_needs_no_device(self):
        target = self.folder / "capture.json"
        save_waveform(target, make_waveform(bytes(800), command=3009, contiguous=False))
        text = self.run_unit(DataWaveformInfo(), "-f", str(target))
        self.assertIn("legacy-unverified", text)
        self.assertIn("ac_rms: 0.0", text)

    def test_offline_export_needs_no_device(self):
        source = self.folder / "capture.json"
        target = self.folder / "samples.bin"
        save_waveform(source, make_waveform(bytes(800), command=3009, contiguous=False))
        self.run_unit(
            DataWaveformExport(),
            "-f",
            str(source),
            "-o",
            str(target),
            "--format",
            "bin",
        )
        self.assertEqual(target.read_bytes(), bytes(800))

    def test_storage_failure_is_not_reported_as_saved(self):
        unit = LFGenericCapture()
        device = FakeDevice()
        unit.device_com = device
        with patch("lf_waveform.os.fsync", side_effect=OSError("storage failure")):
            with self.assertRaises(ArgsParserError):
                self.run_unit(unit, "-o", str(self.folder / "incomplete.json"))
        self.assertEqual(device.mode, 0)
