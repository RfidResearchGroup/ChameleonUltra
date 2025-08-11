#!/usr/bin/env python3
import os
import sys
import unittest
from crypto1 import Crypto1

CURRENT_DIR = os.path.split(os.path.abspath(__file__))[0]
config_path = CURRENT_DIR.rsplit(os.sep, 1)[0]
sys.path.append(config_path)
print(config_path)

class TestCrypto1(unittest.TestCase):

    def test_key_getter_setter(self):
        state = Crypto1()
        state.key = 'a0a1a2a3a4a5'
        self.assertEqual(state.key, 'a0a1a2a3a4a5')

    def test_prng_next(self):
        self.assertEqual(Crypto1.prng_next(0x2C198BE4, 64), 0xCC14C013)

    def test_reader_three_pass_auth(self):
        uid, nt, nr, atEnc = 0x65535D33, 0xBE2B7B5D, 0x0B4271BA, 0x36081500
        reader = Crypto1()
        reader.key = '974C262B9278'
        ks0 = reader.lfsr48_u32(uid ^ nt, False)
        self.assertEqual(ks0, 0xAC93C1A4, 'ks0 assert failed')
        ks1 = reader.lfsr48_u32(nr, False)
        self.assertEqual(ks1, 0xBAA3C92B, 'ks1 assert failed')
        nrEnc = nr ^ ks1
        self.assertEqual(nrEnc, 0xB1E1B891, 'nrEnc assert failed')
        ar = Crypto1.prng_next(nt, 64)
        self.assertEqual(ar, 0xF0928568, 'ar assert failed')
        ks2 = reader.lfsr48_u32(0, False)
        self.assertEqual(ks2, 0xDC652720, 'ks2 assert failed')
        arEnc = ar ^ ks2
        self.assertEqual(arEnc, 0x2CF7A248, 'arEnc assert failed')
        ks3 = reader.lfsr48_u32(0, False)
        self.assertEqual(ks3, 0xC6F4A093, 'ks3 assert failed')
        at = atEnc ^ ks3
        nt96 = Crypto1.prng_next(nt, 96)
        self.assertEqual(at, nt96, 'at assert failed')

    def test_tag_three_pass_auth(self):
        uid, nt, nrEnc, arEnc = 0x65535D33, 0xBE2B7B5D, 0xB1E1B891, 0x2CF7A248
        tag = Crypto1()
        tag.key = '974C262B9278'
        ks0 = tag.lfsr48_u32(uid ^ nt, False)
        self.assertEqual(ks0, 0xAC93C1A4, 'ks0 assert failed')
        ks1 = tag.lfsr48_u32(nrEnc, True)
        self.assertEqual(ks1, 0xBAA3C92B, 'ks1 assert failed')
        nr = ks1 ^ nrEnc
        self.assertEqual(nr, 0x0B4271BA, 'nr assert failed')
        ks2 = tag.lfsr48_u32(0, False)
        self.assertEqual(ks2, 0xDC652720, 'ks2 assert failed')
        ar = ks2 ^ arEnc
        self.assertEqual(ar, 0xF0928568, 'ar assert failed')
        at = Crypto1.prng_next(nt, 96)
        self.assertEqual(at, 0xF0FCB593, 'at assert failed')
        ks3 = tag.lfsr48_u32(0, False)
        self.assertEqual(ks3, 0xC6F4A093, 'ks3 assert failed')
        atEnc = at ^ ks3
        self.assertEqual(atEnc, 0x36081500, 'atEnc assert failed')

    def test_mfkey32_is_reader_has_key_true(self):
        self.assertTrue(Crypto1.mfkey32_is_reader_has_key(
            uid = 0x65535D33, 
            nt = 0x2C198BE4, 
            nrEnc = 0xFEDAC6D2, 
            arEnc = 0xCF0A3C7E, 
            key = 'A9AC67832330'
        ))
    
    def test_mfkey32_is_reader_has_key_false(self):
        self.assertFalse(Crypto1.mfkey32_is_reader_has_key(
            uid = 0x65535D33, 
            nt = 0x2C198BE4, 
            nrEnc = 0xFEDAC6D2, 
            arEnc = 0xCF0A3C7E, 
            key = 'FFFFFFFFFFFF'
        ))

if __name__ == '__main__':
    unittest.main()