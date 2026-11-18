import unittest

from lf_raw_manchester import import_t55x7


class ImportTest(unittest.TestCase):
    def dump(self, config="00088048"):
        return {"FileType": "t55x7", "blocks": {
            "0": config, "1": "01234567", "2": "89ABCDEF", "3": "FFFFFFFF"}}

    def test_transmitted_blocks_only(self):
        frame = import_t55x7(self.dump())
        self.assertEqual(frame.data.hex(), "0123456789abcdef")
        self.assertEqual(frame.clock, 32)
        self.assertTrue(frame.terminator)

    def test_rf64_without_terminator(self):
        frame = import_t55x7(self.dump("00148040"))
        self.assertEqual(frame.clock, 64)
        self.assertFalse(frame.terminator)

    def test_reject_unsupported_config_bits(self):
        allowed = 0x001C0000 | 0x0001F000 | 0xE0 | 8
        for bit in range(32):
            if not allowed & (1 << bit):
                with self.subTest(bit=bit), self.assertRaises(ValueError):
                    import_t55x7(self.dump(f"{0x88048 | (1 << bit):08X}"))

    def test_invalid_and_missing_data(self):
        for value in (None, "00", "GGGGGGGG", 123):
            doc = self.dump()
            doc["blocks"]["2"] = value
            with self.assertRaises(ValueError):
                import_t55x7(doc)

    def test_metadata_conflict(self):
        doc = self.dump()
        doc["Card"] = {"ConfigBlock": "00000000"}
        with self.assertRaises(ValueError):
            import_t55x7(doc)

    def test_metadata_hex_case(self):
        doc = self.dump("000880e8")
        doc["blocks"].update({str(i): "01234567" for i in range(3, 8)})
        doc["Card"] = {"ConfigBlock": "000880E8"}
        self.assertEqual(len(import_t55x7(doc).data), 28)

    def test_unused_block_still_validated(self):
        doc = self.dump()
        doc["blocks"]["3"] = "malformed"
        with self.assertRaises(ValueError):
            import_t55x7(doc)

    def test_malformed_metadata(self):
        for metadata in (None, [], {"ConfigBlock": 123}, {"ConfigBlock": "zzzzzzzz"}):
            doc = self.dump()
            doc["Card"] = metadata
            with self.assertRaises(ValueError):
                import_t55x7(doc)
