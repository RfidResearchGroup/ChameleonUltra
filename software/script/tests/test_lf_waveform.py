import copy
import csv
import hashlib
import io
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch
from typing import Optional, cast

from chameleon_com import Response
from chameleon_enum import Command, Status
from lf_waveform import (
    CAPTURE_COMMAND,
    WaveformError,
    capture_waveform,
    export_waveform,
    load_waveform,
    make_waveform,
    save_waveform,
    summarize,
    validate_waveform,
)


class FakeDevice:
    def __init__(self, versioned=True, mode=0):
        self.commands: list[int] = [
            Command.GET_DEVICE_MODE,
            Command.CHANGE_DEVICE_MODE,
            Command.ADC_GENERIC_READ,
        ]
        if versioned:
            self.commands.append(CAPTURE_COMMAND)
        self.mode = mode
        self.calls = []
        self.override: Optional[bytes] = None
        self.capture_error: Optional[BaseException] = None
        self.restore_error = False
        self.capture_status = Status.LF_TAG_OK

    def send_cmd_sync(self, cmd, data=None, status=0, timeout=3):
        self.calls.append((cmd, data))
        if cmd == Command.GET_DEVICE_MODE:
            return Response(cmd, Status.SUCCESS, bytes([self.mode]))
        if cmd == Command.CHANGE_DEVICE_MODE:
            assert data is not None
            if data == b"\x00" and self.restore_error:
                raise OSError("restore unavailable")
            self.mode = data[0]
            return Response(cmd, Status.SUCCESS)
        if self.capture_error:
            raise self.capture_error
        if self.override is not None:
            output = self.override
        elif cmd == CAPTURE_COMMAND:
            assert data is not None
            count = struct.unpack("!H", data)[0]
            output = struct.pack("!BBHIHH", 1, 8, count, 125000, 3, count) + bytes(
                i % 256 for i in range(count)
            )
        else:
            output = bytes(range(200)) * 4
        return Response(cmd, self.capture_status, output)


class WaveformTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.wave = make_waveform(bytes(range(200)) * 4, command=3009, contiguous=False)

    def test_json_roundtrip(self):
        target = self.folder / "capture.json"
        save_waveform(target, self.wave)
        self.assertEqual(load_waveform(target), self.wave)

    def test_new_protocol_capture_restores_tag_mode(self):
        device = FakeDevice()
        wave = capture_waveform(device, 4000)
        self.assertEqual(wave["sample_count"], 4000)
        self.assertEqual(wave["continuity"], "dma-complete")
        self.assertEqual(device.mode, 0)
        self.assertEqual(device.calls[-1], (Command.GET_DEVICE_MODE, None))

    def test_existing_reader_mode_is_not_changed(self):
        device = FakeDevice(mode=1)
        capture_waveform(device)
        self.assertEqual(device.mode, 1)
        self.assertFalse(any(c == Command.CHANGE_DEVICE_MODE for c, _ in device.calls))

    def test_legacy_has_no_continuity_claim(self):
        device = FakeDevice(versioned=False)
        wave = capture_waveform(device)
        self.assertEqual(wave["continuity"], "legacy-unverified")
        self.assertEqual(wave["command"], 3009)

    def test_legacy_non800_request_is_rejected_before_mode_change(self):
        device = FakeDevice(versioned=False)
        with self.assertRaises(WaveformError):
            capture_waveform(device, 4000)
        self.assertEqual(device.calls, [])

    def test_invalid_requests_do_not_touch_device(self):
        for count in (0, 4001, True, -1, 1.5):
            device = FakeDevice()
            with self.subTest(count=count), self.assertRaises(WaveformError):
                capture_waveform(device, cast(int, count))
            self.assertEqual(device.calls, [])

    def test_capture_failure_restores_mode(self):
        device = FakeDevice()
        device.capture_error = WaveformError("capture failed")
        with self.assertRaisesRegex(WaveformError, "capture failed"):
            capture_waveform(device)
        self.assertEqual(device.mode, 0)

    def test_cancellation_survives_cleanup_failure(self):
        device = FakeDevice()
        cancellation = KeyboardInterrupt()
        device.capture_error = cancellation
        device.restore_error = True
        with self.assertRaises(KeyboardInterrupt) as raised:
            capture_waveform(device)
        self.assertIs(raised.exception, cancellation)
        self.assertIn("restore", getattr(raised.exception, "restore_error"))

    def test_success_cannot_hide_mode_cleanup_failure(self):
        device = FakeDevice()
        device.restore_error = True
        with self.assertRaisesRegex(WaveformError, "restore"):
            capture_waveform(device)

    def test_bad_status_cannot_be_saved_as_samples(self):
        device = FakeDevice()
        device.capture_status = Status.PAR_ERR
        with self.assertRaises(WaveformError):
            capture_waveform(device)
        self.assertEqual(device.mode, 0)

    def test_wrong_headers_and_truncated_payloads(self):
        valid = struct.pack("!BBHIHH", 1, 8, 800, 125000, 3, 800) + bytes(800)
        variants = [valid[:-1], valid + b"\x00"]
        for offset, replacement in ((0, 2), (1, 4), (9, 1), (11, 1), (7, 0)):
            modified = bytearray(valid)
            modified[offset] = replacement
            variants.append(bytes(modified))
        for payload in variants:
            device = FakeDevice()
            device.override = payload
            with self.subTest(payload=payload[:12]), self.assertRaises(WaveformError):
                capture_waveform(device)
            self.assertEqual(device.mode, 0)

    def test_sample_corruption_is_detected(self):
        changed = copy.deepcopy(self.wave)
        changed["samples_hex"] = "ff" + changed["samples_hex"][2:]
        with self.assertRaisesRegex(WaveformError, "sample checksum"):
            validate_waveform(changed)

    def test_metadata_corruption_is_detected(self):
        changed = copy.deepcopy(self.wave)
        changed["created_utc"] = "2026-01-01T00:00:00+00:00"
        with self.assertRaisesRegex(WaveformError, "document checksum"):
            validate_waveform(changed)

    def test_duplicate_members_are_rejected(self):
        target = self.folder / "duplicate.json"
        target.write_text('{"schema":"a","schema":"b"}', encoding="utf-8")
        with self.assertRaisesRegex(WaveformError, "duplicate"):
            load_waveform(target)

    def test_oversized_and_malformed_input(self):
        target = self.folder / "bad.json"
        for payload in (b"[" * 65537, b"\xff", b"[]", b"null", b"{"):
            target.write_bytes(payload)
            with self.subTest(payload=payload[:8]), self.assertRaises(WaveformError):
                load_waveform(target)

    def test_extra_fields_and_wrong_types_are_rejected(self):
        for key, value in (
            ("extra", 0),
            ("sample_count", True),
            ("samples_hex", []),
            ("nominal_sample_rate_hz", 0),
            ("command", True),
            ("created_utc", 123),
        ):
            changed = copy.deepcopy(self.wave)
            changed[key] = value
            with self.subTest(key=key), self.assertRaises(WaveformError):
                validate_waveform(changed)

    def test_legacy_continuity_cannot_be_upgraded_by_editing_metadata(self):
        changed = copy.deepcopy(self.wave)
        changed["continuity"] = "dma-complete"
        with self.assertRaisesRegex(WaveformError, "continuity"):
            validate_waveform(changed)

    def test_output_is_no_clobber(self):
        target = self.folder / "exists.json"
        target.write_bytes(b"keep")
        with self.assertRaises(WaveformError):
            save_waveform(target, self.wave)
        self.assertEqual(target.read_bytes(), b"keep")

    def test_missing_output_directory_is_not_created(self):
        with self.assertRaises(WaveformError):
            save_waveform(self.folder / "missing" / "x.json", self.wave)
        self.assertFalse((self.folder / "missing").exists())

    def test_fsync_error_does_not_report_success(self):
        with patch("lf_waveform.os.fsync", side_effect=OSError("disk unavailable")):
            with self.assertRaisesRegex(WaveformError, "publication"):
                save_waveform(self.folder / "partial.json", self.wave)

    def test_binary_export_exactly_matches_samples(self):
        target = self.folder / "samples.bin"
        export_waveform(self.wave, target, "bin")
        self.assertEqual(
            hashlib.sha256(target.read_bytes()).hexdigest(), self.wave["samples_sha256"]
        )

    def test_csv_has_nominal_coordinates(self):
        target = self.folder / "samples.csv"
        export_waveform(self.wave, target, "csv")
        rows = list(csv.reader(io.StringIO(target.read_text())))
        self.assertEqual(rows[0], ["sample", "nominal_time_us", "adc_u8"])
        self.assertEqual(rows[2], ["1", "8.000000", "1"])
        self.assertEqual(len(rows), 801)

    def test_svg_is_static_numeric_output(self):
        target = self.folder / "samples.svg"
        export_waveform(self.wave, target, "svg")
        text = target.read_text()
        self.assertIn("legacy-unverified", text)
        self.assertIn("<polyline", text)
        self.assertNotIn("<script", text)

    def test_flat_and_alternating_statistics(self):
        flat = make_waveform(bytes([32]) * 800, command=3009, contiguous=False)
        stats = summarize(flat)
        self.assertEqual(
            (stats["mean"], stats["ac_rms"], stats["midpoint_transitions"]), (32, 0, 0)
        )
        alternating = make_waveform(b"\x00\xff" * 400, command=3009, contiguous=False)
        self.assertEqual(summarize(alternating)["midpoint_transitions"], 799)

    def test_invalid_export_is_rejected_without_creating_file(self):
        target = self.folder / "bad"
        with self.assertRaises(WaveformError):
            export_waveform(self.wave, target, "unknown")
        self.assertFalse(target.exists())

    def test_unknown_mode_is_rejected(self):
        device = FakeDevice(mode=2)
        with self.assertRaisesRegex(WaveformError, "mode"):
            capture_waveform(device)
