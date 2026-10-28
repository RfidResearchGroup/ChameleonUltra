"""Actual CU MF1/AES/Crypto1 handlers against the independent synthetic reader.

This compiles the real default (non
NFC_MF1_FAST_SIM) card engine, Crypto1 helpers, PRNG, CRC/number helpers and
parity packer. Only SDK/field/RNG/TX/critical primitives are host
shims. No devices, real profiles, key search, firmware builds or downloads.

The bitstream assertions verify software parity construction, not reception by
NFCT hardware or RF timing. The alternate FAST_SIM algorithm is not exercised.
"""
import ctypes as C
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from Crypto.Cipher import AES

import test_cu_sl1_native_hooks as base


ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "firmware/application/src/rfid/nfctag/hf/sl1"
CORE = ADAPTER
AES_KEY = bytes(range(16))
KEY_A = bytes.fromhex("a0a1a2a3a4a5")
KEY_B = bytes.fromhex("b0b1b2b3b4b5")
UID4 = bytes.fromhex("10203040")
UID7 = bytes.fromhex("02aabb10203040")
SEED = 0x12345678

# Load only the independent reader recurrence.
reader_spec = importlib.util.spec_from_file_location(
    "cu_sl1_independent_reader", Path(__file__).with_name("reader_reference.py"))
reader_module = importlib.util.module_from_spec(reader_spec)
reader_spec.loader.exec_module(reader_module)
ReaderCrypto1 = reader_module.ReaderCrypto1
successor = reader_module.successor
frame = base.frame


BRIDGE = r'''
static nfc_tag_mf1_information_t host_profile;
void test_init(unsigned uid_len, unsigned weak) {
    mf1_sl1_unload(); memset(&host_profile, 0, sizeof(host_profile));
    host_slot = 1; host_type = TAG_TYPE_MIFARE_2048; host_enabled = true;
    host_rng_counter = host_rng_calls = 0; host_rng_ok = true;
    host_field_status = 0; m_nfct_field_present = m_sniff_passive = false;
    host_profile.res_coll.size = uid_len;
    const uint8_t uid[] = {2,0xaa,0xbb,0x10,0x20,0x30,0x40};
    memcpy(host_profile.res_coll.uid, uid + (uid_len == 4 ? 3 : 0), uid_len);
    host_profile.res_coll.atqa[0] = uid_len == 7 ? 0x44 : 4;
    host_profile.res_coll.ats.length = 2; host_profile.res_coll.ats.data[0] = 2;
    host_profile.config.prng_type = weak ? 1 : 0;
    for (unsigned block = 0; block < 128; ++block) {
        for (unsigned i = 0; i < 16; ++i) host_profile.memory[block][i] = block * 17U + i;
        if ((block & 3U) == 3U) {
            for (unsigned i = 0; i < 6; ++i) {
                host_profile.memory[block][i] = 0xa0 + i;
                host_profile.memory[block][i + 10] = 0xb0 + i;
            }
            /* Data blocks 000; trailer 101, so Key B may authenticate. */
            const uint8_t access[] = {0xf7,0x87,0x80,0x69};
            memcpy(host_profile.memory[block] + 6, access, sizeof(access));
        }
    }
    tag_data_buffer_t buffer = {.length = sizeof(host_profile), .buffer = (uint8_t *)&host_profile};
    nfc_tag_mf1_data_loadcb(TAG_TYPE_MIFARE_2048, &buffer);
    nfc_tag_mf1_prng_seed(0x12345678);
    nfc_tag_mf1_reset_handler(); m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
    host_bits = 0; host_crc = false;
}
unsigned test_feed(const uint8_t *data, unsigned size, const uint8_t *parity) {
    if (size > 35) return UINT32_MAX;
    uint8_t scratch[257] = {0}; unsigned wire = 0; host_bits = 0;
    for (unsigned i = 0; i < size; ++i) {
        for (unsigned bit = 0; bit < 8; ++bit, ++wire)
            scratch[wire/8] |= ((data[i] >> bit) & 1U) << (wire%8);
        unsigned par = parity ? parity[i] : oddparity8(data[i]);
        scratch[wire/8] |= (par & 1U) << (wire%8); ++wire;
    }
    NRF_NFCT->RXD.AMOUNT = wire; nfc_tag_14a_data_process(scratch);
    return host_bits;
}
const uint8_t *test_tx(void) { return host_tx; }
unsigned test_crc(void) { return host_crc; }
unsigned test_state(void) { return m_tag_state_14a; }
unsigned test_mf_state(void) { return m_mf1_state; }
unsigned test_verified(void) { return m_sl1.core.verified; }
unsigned test_enabled(void) { return cu_sl1_adapter_enabled(&m_sl1); }
void test_block(unsigned block, uint8_t *out) { memcpy(out, host_profile.memory[block], 16); }
'''


def encrypt(reader, plain, feedback=False):
    """Reader-side encoding plus encrypted parity, without C/card calls."""
    wire, parity = bytearray(), bytearray()
    for value in plain:
        wire.append(value ^ reader.byte(value if feedback else 0))
        parity.append(reader.filter() ^ (1 ^ (value.bit_count() & 1)))
    return bytes(wire), bytes(parity)


def unpack(wire, bits):
    if bits % 9:
        raise AssertionError("Expected a whole software-packed parity frame")
    data, parity = bytearray(), bytearray((bits // 9 + 7) // 8)
    for i in range(bits // 9):
        bit = lambda offset: (wire[offset // 8] >> (offset % 8)) & 1
        data.append(sum(bit(i * 9 + j) << j for j in range(8)))
        parity[i // 8] |= bit(i * 9 + 8) << (i % 8)
    return bytes(data), bytes(parity)


class NativeCrypto1Tests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checkout = ROOT
        hf = cls.checkout / "firmware/application/src/rfid/nfctag/hf"
        rfid = hf.parents[1]
        header_mf_text = (hf / "nfc_mf1.h").read_text()
        build_text = (cls.checkout / "firmware/application/Makefile").read_text()
        build_text += (cls.checkout / "firmware/Makefile.defs").read_text()
        if re.search(r"(?m)^\s*#\s*define\s+NFC_MF1_FAST_SIM\b", header_mf_text) or "-DNFC_MF1_FAST_SIM" in build_text:
            raise RuntimeError("Candidate uses FAST_SIM; this suite covers the default real Crypto1 route")
        native = (hf / "nfc_mf1.c").read_text().split("// Factory data for initialization of MF1")[0]
        native = re.sub(r'^#include[^\n]*', '', native, flags=re.M)
        header14 = re.sub(r'^#include[^\n]*', '', (hf / "nfc_14a.h").read_text(), flags=re.M)
        header_mf = re.sub(r'^#include[^\n]*', '', header_mf_text, flags=re.M)
        tag_base = (hf.parent / "tag_base_type.h").read_text()
        code14 = (hf / "nfc_14a.c").read_text()
        shim = base.SHIM
        for name in ("crypto1_deinit", "crypto1_init", "crypto1_word", "mf_crypto1_decryptEx",
                     "mf_crypto1_encrypt4bit", "mf_crypto1_encrypt", "mf_crypto1_encryptEx",
                     "prng_successor", "bytes_to_num", "num_to_bytes", "calc_14a_crc_lut",
                     "nfc_tag_14a_wrap_frame"):
            actual = base.function(shim, name)
            if shim.count(actual) != 1:
                raise RuntimeError("Unexpected inherited shim contract: " + name)
            shim = shim.replace(actual, "", 1)
        shim, count = re.subn(r"struct Crypto1State \{ uint32_t odd, even; \};", "", shim)
        if count != 1:
            raise RuntimeError("Inherited Crypto1 state shim was not removed exactly once")
        shim = '#include "crypto1_helper.h"\n#include "crc_utils.h"\n' + shim
        numbers = (rfid / "hex_utils.c").read_text()
        number_functions = '\n'.join(base.function(numbers, name) for name in ("bytes_to_num", "num_to_bytes"))
        pure14 = [base.function(code14, name) for name in (
            "nfc_tag_14a_set_state", "nfc_tag_14a_set_handler", "nfc_tag_14a_set_sniff_passive",
            "nfc_tag_14a_is_sniff_passive", "nfc_tag_14a_field_present", "nfc_tag_14a_create_bcc",
            "nfc_tag_14a_append_bcc", "nfc_tag_14a_append_crc", "nfc_tag_14a_checks_crc",
            "nfc_tag_14a_wrap_frame", "nfc_tag_14a_unwrap_frame", "nfc_tag_14a_extended_ats_fits",
            "nfc_tag_14a_data_process")]
        pure14 = [re.sub(r'^inline ', '', item) for item in pure14]
        preamble = ('#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n'
                    '#define PACKED\ntypedef struct { uint16_t length; uint8_t *buffer; uint16_t *crc; } tag_data_buffer_t;\n')
        source = '\n'.join([preamble, tag_base, header14, header_mf, shim, number_functions,
                            native, *pure14, BRIDGE])
        cls.directory = tempfile.TemporaryDirectory(prefix="cu-sl1-real-crypto1-")
        cls.addClassCleanup(cls.directory.cleanup)
        temporary = Path(cls.directory.name)
        # A host equivalent of the CMSIS byte-reverse intrinsic, not a PRNG shim.
        (temporary / "cmsis_gcc.h").write_text('#pragma once\n#include <stdint.h>\n#define __REV(value) __builtin_bswap32((uint32_t)(value))\n')
        (temporary / "native.c").write_text(source)
        command = ["cc", "-std=c99", "-O1", "-shared", "-fPIC", "-Wall", "-Wextra",
                   "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
                   "-I", str(temporary), "-I", str(ADAPTER), "-I", str(CORE),
                   "-I", str(hf), "-I", str(rfid), str(temporary / "native.c"),
                   str(ADAPTER / "sl1_framing.c"), str(CORE / "sl1_cardauth.c"),
                   str(CORE / "vendor/aes.c"), str(rfid / "byte_mirror.c"),
                   str(hf / "crypto1_helper.c"), str(rfid / "mf1_crapto1.c"),
                   str(rfid / "mf1_crypto1.c"), str(rfid / "parity.c"), str(rfid / "crc_utils.c"),
                   "-o", str(temporary / "native.dylib")]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Actual-C Crypto1 host build failed ({result.returncode}): {result.stderr}")
        cls.lib = C.CDLL(str(temporary / "native.dylib"))
        cls.lib.test_init.argtypes = [C.c_uint, C.c_uint]
        cls.lib.test_feed.argtypes = [C.c_void_p, C.c_uint, C.c_void_p]
        cls.lib.test_feed.restype = C.c_uint
        cls.lib.test_tx.restype = C.POINTER(C.c_uint8)
        cls.lib.test_block.argtypes = [C.c_uint, C.c_void_p]
        cls.lib.nfc_tag_mf1_sl1_configure.argtypes = [C.c_uint8, C.c_void_p, C.c_uint8, C.c_void_p]
        cls.lib.nfc_tag_mf1_sl1_configure.restype = C.c_bool

    def init(self, uid=UID7, fresh=False, enabled=True, weak=True):
        self.lib.test_init(len(uid), weak)
        self.uid = uid
        if enabled:
            self.assertTrue(self.lib.nfc_tag_mf1_sl1_configure(1, uid, len(uid), AES_KEY))
        self.lib.nfc_tag_14a_set_state(2)
        for _ in range(4):
            self.lib.nfc_tag_mf1_sl1_poll()
        self.expected_nonce = bytes(range(16, 20)) if enabled else (
            successor(SEED, 32).to_bytes(4, "big") if weak else bytes.fromhex("01020304"))

    def feed(self, data, parity=None):
        bits = self.lib.test_feed(data, len(data), parity)
        self.assertNotEqual(bits, 0xffffffff)
        return bits, bytes(self.lib.test_tx()[:(bits + 7) // 8])

    def aes(self):
        self.assertEqual(self.lib.test_verified(), 0)
        bits, reply = self.feed(frame(bytes.fromhex("760490")))
        self.assertEqual((bits, self.lib.test_crc()), (136, 1))
        self.assertEqual(reply[0], 0x90)
        rnd_b = AES.new(AES_KEY, AES.MODE_ECB).decrypt(reply[1:])
        self.assertEqual(rnd_b, bytes(range(16)))
        rnd_a = bytes(range(90, 106))
        token = b"\x72" + AES.new(AES_KEY, AES.MODE_CBC, bytes(16)).encrypt(rnd_a + rnd_b[1:] + rnd_b[:1])
        bits, proof = self.feed(frame(token))
        self.assertEqual((bits, self.lib.test_crc(), proof[0]), (136, 1, 0x90))
        self.assertEqual(AES.new(AES_KEY, AES.MODE_ECB).decrypt(proof[1:]), rnd_a[1:] + rnd_a[:1])
        self.assertEqual(self.lib.test_verified(), 1)
        self.assertEqual(self.lib.test_mf_state(), 0)

    def auth(self, key=KEY_A, opcode=0x60, corrupt_proof=False, expect_success=True):
        bits, nonce = self.feed(frame(bytes([opcode, 1])))
        self.assertEqual((bits, self.lib.test_crc()), (32, 0))
        self.assertEqual(nonce, self.expected_nonce)
        reader = ReaderCrypto1(key)
        nt = int.from_bytes(nonce, "big")
        reader.word(nt ^ int.from_bytes(self.uid[-4:], "big"))
        nr, parity_nr = encrypt(reader, bytes.fromhex("89abcdef"), feedback=True)
        answer = successor(nt, 64).to_bytes(4, "big")
        if corrupt_proof:
            answer = bytes([answer[0] ^ 1]) + answer[1:]
        ar, parity_ar = encrypt(reader, answer)
        bits, response = self.feed(nr + ar, parity_nr + parity_ar)
        if not expect_success:
            self.assertEqual(bits, 0)
            self.assertEqual((self.lib.test_mf_state(), self.lib.test_state()), (0, 0))
            return reader
        self.assertEqual(bits, 36)
        wire, parity = unpack(response, bits)
        self.assertEqual(reader.decrypt_with_parity(wire, parity), successor(nt, 96).to_bytes(4, "big"))
        self.assertEqual((self.lib.test_mf_state(), self.lib.test_state()), (2, 4))
        return reader

    def block(self, number):
        out = (C.c_uint8 * 16)()
        self.lib.test_block(number, out)
        return bytes(out)

    def read(self, reader):
        command, parity = encrypt(reader, frame(b"\x30\x01"))
        bits, response = self.feed(command, parity)
        self.assertEqual(bits, 162)
        wire, parity = unpack(response, bits)
        plain = reader.decrypt_with_parity(wire, parity)
        self.assertEqual(plain, frame(bytes(range(17, 33))))
        self.assertEqual(plain[:16], self.block(1))

    def chain(self, uid, fresh):
        self.init(uid, fresh)
        self.aes()
        reader = self.auth()
        self.read(reader)
        self.assertEqual(self.lib.test_verified(), 1)
        self.assertTrue(self.lib.test_enabled())

    def test_four_byte_uid_full_continuous_chain(self):
        self.chain(UID4, False)

    def test_seven_byte_uid_full_continuous_chain(self):
        self.chain(UID7, False)

    def test_four_byte_uid_fresh_nonce_full_continuous_chain(self):
        self.chain(UID4, True)

    def test_seven_byte_uid_fresh_nonce_full_continuous_chain(self):
        self.chain(UID7, True)

    def test_key_b_authentication_and_read_with_actual_acl(self):
        self.init(fresh=True)
        self.aes()
        self.read(self.auth(KEY_B, 0x61))

    def test_wrong_classic_key_rejected_after_aes(self):
        self.init(fresh=True)
        self.aes()
        before = self.block(1)
        self.auth(bytes(value ^ 0xff for value in KEY_A), expect_success=False)
        self.assertEqual(self.block(1), before)
        self.assertTrue(self.lib.test_enabled())

    def test_modified_reader_proof_rejected_after_aes(self):
        self.init(fresh=False)
        self.aes()
        self.auth(corrupt_proof=True, expect_success=False)
        self.assertTrue(self.lib.test_enabled())

    def test_encrypted_modifying_commands_denied_without_memory_change(self):
        for opcode in (0xa0, 0xc0, 0xc1, 0xc2, 0xb0):
            with self.subTest(opcode=opcode):
                self.init(fresh=True)
                self.aes()
                reader = self.auth()
                before = self.block(1)
                request, parity = encrypt(reader, frame(bytes([opcode, 1])))
                bits, response = self.feed(request, parity)
                self.assertEqual(bits, 4)
                nack = response[0] ^ sum(reader.clock() << bit for bit in range(4))
                self.assertEqual(nack, 4)
                self.assertEqual(self.block(1), before)
                self.assertEqual((self.lib.test_mf_state(), self.lib.test_state()), (0, 0))
                self.assertTrue(self.lib.test_enabled())

    def test_static_native_nonce_uses_real_prng_for_proofs(self):
        self.init(enabled=False, weak=False)
        self.read(self.auth())

    def test_default_off_legacy_write_remains_working(self):
        self.init(enabled=False)
        reader = self.auth()
        self.read(reader)
        write, parity = encrypt(reader, frame(b"\xa0\x01"))
        bits, response = self.feed(write, parity)
        self.assertEqual(bits, 4)
        self.assertEqual(response[0] ^ sum(reader.clock() << bit for bit in range(4)), 0x0a)
        replacement = bytes(range(0x80, 0x90))
        payload, parity = encrypt(reader, frame(replacement))
        bits, response = self.feed(payload, parity)
        self.assertEqual(bits, 4)
        self.assertEqual(response[0] ^ sum(reader.clock() << bit for bit in range(4)), 0x0a)
        self.assertEqual(self.block(1), replacement)
        self.assertFalse(self.lib.test_enabled())

    def test_host_bridge_length_sentinel_is_unsigned(self):
        self.init()
        self.assertEqual(self.lib.test_feed(bytes(36), 36, None), 0xffffffff)
