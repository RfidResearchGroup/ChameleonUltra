import unittest

from hf14a_trace import TRACE_MAGIC, anticollision_response, crc_a, parse_hf14a_trace


def record(bits, data, tx=False, config=None):
    header = (bits | (0x8000 if tx else 0)).to_bytes(2, "big")
    return header + (bytes([config]) if config is not None else b"") + data


def rf_encode(data, bits):
    stream = []
    for index in range(bits):
        stream.append((data[index // 8] >> (index % 8)) & 1)
        if index % 8 == 7:
            stream.append(1 ^ (bin(data[index // 8]).count("1") % 2))
    result = bytearray((len(stream) + 7) // 8)
    for index, bit in enumerate(stream):
        result[index // 8] |= bit << (index % 8)
    return bytes(result), len(stream)


class TraceTests(unittest.TestCase):
    def test_legacy_rx_full_bytes(self):
        raw, count = rf_encode(b"\x44\x03", 16)
        frame = parse_hf14a_trace(record(count, raw))[0]
        self.assertEqual(frame.decoded(), (16, b"\x44\x03", [1, 1]))

    def test_rx_partial_byte_includes_only_complete_byte_parity(self):
        raw, count = rf_encode(b"\x93\x21\x00", 17)
        frame = parse_hf14a_trace(TRACE_MAGIC + record(count, raw, config=4))[0]
        bits, data, parity = frame.decoded()
        self.assertEqual((bits, data), (17, b"\x93\x21\x00"))
        self.assertEqual(len(parity), 2)

    def test_short_request_has_no_parity(self):
        frame = parse_hf14a_trace(TRACE_MAGIC + record(7, b"\x26", config=4))[0]
        self.assertEqual(frame.decoded(), (7, b"\x26", []))

    def test_hardware_partial_first_byte(self):
        uid = bytes.fromhex("8804a1b29f")
        for known in range(1, 33):
            count = 40 - known
            raw = uid[known // 8:]
            frame = parse_hf14a_trace(
                TRACE_MAGIC + record(count, raw, tx=True, config=7))[0]
            expected = (int.from_bytes(uid, "little") >> known).to_bytes(
                (count + 7) // 8, "little")
            self.assertEqual(frame.decoded(), (count, expected, []))
            self.assertEqual(frame.raw, raw)

    def test_hardware_crc_is_reconstructed_but_metadata_retained(self):
        frame = parse_hf14a_trace(
            TRACE_MAGIC + record(8, b"\x04", tx=True, config=0x17))[0]
        self.assertEqual(frame.decoded(), (24, bytes.fromhex("04da17"), []))
        self.assertEqual(frame.bits, 8)
        self.assertEqual(frame.raw, b"\x04")
        self.assertEqual(frame.frame_config, 0x17)

    def test_nine_dma_bytes_are_not_eight_radio_bytes(self):
        data = bytes(range(9))
        frame = parse_hf14a_trace(
            TRACE_MAGIC + record(72, data, tx=True, config=7))[0]
        self.assertEqual(frame.decoded(), (72, data, []))

    def test_legacy_tx_is_not_guessed_from_bit_count(self):
        data = bytes(range(9))
        frame = parse_hf14a_trace(record(72, data, tx=True))[0]
        self.assertIsNone(frame.frame_config)
        self.assertEqual(frame.decoded(), (72, data, []))

    def test_raw_tx_with_complete_parity(self):
        raw, count = rf_encode(b"\x04\x91", 16)
        frame = parse_hf14a_trace(
            TRACE_MAGIC + record(count, raw, tx=True, config=4))[0]
        self.assertEqual(frame.decoded()[:2], (16, b"\x04\x91"))

    def test_rx_with_hardware_parity_removal(self):
        frame = parse_hf14a_trace(
            TRACE_MAGIC + record(16, b"\x44\x03", config=5))[0]
        self.assertEqual(frame.decoded(), (16, b"\x44\x03", []))

    def test_bad_records_are_rejected(self):
        cases = (b"\x00", b"\x00\x00", b"\x00\x10\xff", b"CU14\x02",
                 TRACE_MAGIC + b"\x80\x08", TRACE_MAGIC + b"\x80\x10\x07\xff")
        for data in cases:
            with self.subTest(data=data), self.assertRaises(ValueError):
                parse_hf14a_trace(data)

    def test_empty_and_mixed_capture(self):
        self.assertEqual(parse_hf14a_trace(b""), [])
        self.assertEqual(parse_hf14a_trace(TRACE_MAGIC), [])
        stream = TRACE_MAGIC + record(7, b"\x26", config=4)
        stream += record(16, b"\x44\x03", tx=True, config=7)
        self.assertEqual([frame.is_tx for frame in parse_hf14a_trace(stream)],
                         [False, True])

    def test_crc_standard_vectors(self):
        self.assertEqual(crc_a(b"\x04"), bytes.fromhex("da17"))
        self.assertEqual(crc_a(bytes.fromhex("5000")), bytes.fromhex("57cd"))

    def test_short_uid_suffix_is_not_mislabeled_as_sak_or_atqa(self):
        for selector, level in ((0x93, 1), (0x95, 2), (0x97, 3)):
            request = bytes([selector, 0x60, 0x88, 0x04, 0xa1, 0xb2])
            reply = anticollision_response(48, request, 8)
            assert reply is not None
            label, valid = reply
            self.assertTrue(valid)
            self.assertEqual(label, f"ANTICOLL CL{level}: BCC response (8 bits)")
        reply = anticollision_response(40, bytes.fromhex("93508804a1"), 16)
        assert reply is not None
        label, valid = reply
        self.assertTrue(valid)
        self.assertIn("UID/BCC response (16 bits)", label)

    def test_anticollision_context_rejects_wrong_lengths_and_other_requests(self):
        reply = anticollision_response(48, bytes.fromhex("93608804a1b2"), 24)
        assert reply is not None
        label, valid = reply
        self.assertFalse(valid)
        self.assertIn("expected 8", label)
        self.assertIsNone(anticollision_response(72, bytes.fromhex("9370"), 24))
        self.assertIsNone(anticollision_response(16, bytes.fromhex("9328"), 40))
        self.assertIsNone(anticollision_response(16, bytes.fromhex("9360"), 8))

    def test_client_metadata_request_preserves_legacy_default(self):
        from unittest.mock import Mock
        from chameleon_cmd import ChameleonCMD
        from chameleon_enum import Command

        device = Mock()
        command = ChameleonCMD(device)
        command.hf14a_sniff()
        device.send_cmd_sync.assert_called_once_with(
            Command.HF14A_SNIFF, bytes.fromhex("1388"), timeout=10)
        device.reset_mock()
        command.hf14a_sniff(70000, with_metadata=True)
        device.send_cmd_sync.assert_called_once_with(
            Command.HF14A_SNIFF, bytes.fromhex("753001"), timeout=35)


if __name__ == "__main__":
    unittest.TestProgram()
