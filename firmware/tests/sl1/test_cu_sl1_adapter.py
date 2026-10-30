"""Actual portable CU adapter/core/AES against an independent synthetic reader.

No devices, profiles, SDK/bootstrap, or repository build artifacts are used.
"""
import ctypes as C
from pathlib import Path
import subprocess
import tempfile
import unittest

from Crypto.Cipher import AES


ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "firmware/application/src/rfid/nfctag/hf/sl1"
CORE = ADAPTER
KEY = bytes((i * 11 + 7) & 255 for i in range(16))
OTHER_KEY = bytes(reversed(KEY))
RND_B = bytes((i * 17 + 3) & 255 for i in range(16))
RND_A = bytes((i * 19 + 5) & 255 for i in range(16))
START = bytes.fromhex("760490")


def crc_a(data):
    """Bit-oriented CRC implementation independent of the adapter C routine."""
    value = 0x6363
    for byte in data:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return value.to_bytes(2, "little")


def frame(payload):
    return payload + crc_a(payload)


def token(key=KEY, rnd_b=RND_B, rnd_a=RND_A, wrong_rotation=False):
    rotated = rnd_b[1:] + rnd_b[:1]
    if wrong_rotation:
        rotated = bytes([rotated[0] ^ 1]) + rotated[1:]
    return frame(b"\x72" + AES.new(key, AES.MODE_CBC, bytes(16)).encrypt(rnd_a + rotated))


class Reply(C.Structure):
    _fields_ = [("data", C.c_uint8 * 17), ("size", C.c_size_t),
                ("bits", C.c_size_t), ("append_crc", C.c_bool)]


BRIDGE = r'''
#include <stdlib.h>
#include <string.h>
#include "sl1_framing.h"
void* test_make(void) {
    CuSl1Adapter* a = malloc(sizeof(*a));
    if(a) cu_sl1_adapter_init(a);
    return a;
}
void test_drop(void* p) {
    cu_sl1_adapter_disable(p);
    free(p);
}
int test_pending(const CuSl1Adapter* a) { return a->core.pending; }
int test_session_empty(const CuSl1Adapter* a) {
    unsigned value = 0;
    for(size_t i = 0; i < 16; ++i) value |= a->core.rnd_b[i] | a->core.challenge[i];
    return !a->core.pending && value == 0;
}
int test_key_matches(const CuSl1Adapter* a, const unsigned char* key) {
    return a->core.key_set && !memcmp(a->core.aes_key, key, 16);
}
int test_secrets_empty(const CuSl1Adapter* a) {
    unsigned value = 0;
    for(size_t i = 0; i < 16; ++i)
        value |= a->core.aes_key[i] | a->core.rnd_b[i] | a->core.challenge[i];
    return !a->enabled && !a->core.key_set && !a->core.pending && value == 0;
}
int test_reconfigure_alias(CuSl1Adapter* a) {
    return cu_sl1_adapter_configure(a, a->core.aes_key, 16);
}
unsigned test_verified(const CuSl1Adapter* a) { return a->core.verified; }
unsigned test_pairs(const CuSl1Adapter* a) { (void)a; return 0; }
'''


class AdapterTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory(prefix="cu-sl1-adapter-")
        temporary = Path(cls.directory.name)
        (temporary / "bridge.c").write_text(BRIDGE)
        cls.command = ["cc", "-std=c99", "-shared", "-fPIC", "-O2", "-Wall", "-Wextra",
                       "-Werror", "-pedantic", "-I", str(ADAPTER), "-I", str(CORE),
                       str(temporary / "bridge.c"), str(ADAPTER / "sl1_framing.c"),
                       str(CORE / "sl1_cardauth.c"), str(CORE / "vendor/aes.c"),
                       "-o", str(temporary / "adapter.dylib")]
        result = subprocess.run(cls.command, capture_output=True, text=True)
        if result.returncode:
            cls.directory.cleanup()
            raise RuntimeError(f"Host compilation failed ({result.returncode}): {result.stderr}")
        cls.lib = C.CDLL(str(temporary / "adapter.dylib"))
        cls.lib.test_make.restype = C.c_void_p
        for name in ("test_drop", "cu_sl1_adapter_reset", "cu_sl1_adapter_disable"):
            getattr(cls.lib, name).argtypes = [C.c_void_p]
            getattr(cls.lib, name).restype = None
        cls.lib.cu_sl1_adapter_init.argtypes = [C.c_void_p]
        cls.lib.cu_sl1_adapter_init.restype = None
        cls.lib.cu_sl1_adapter_configure.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t]
        cls.lib.cu_sl1_adapter_configure.restype = C.c_bool
        cls.lib.cu_sl1_adapter_enabled.argtypes = [C.c_void_p]
        cls.lib.cu_sl1_adapter_enabled.restype = C.c_bool
        cls.lib.cu_sl1_adapter_receive.argtypes = [
            C.c_void_p, C.c_void_p, C.c_size_t, C.c_size_t, C.c_bool,
            C.c_void_p, C.c_size_t, C.POINTER(Reply)]
        cls.lib.cu_sl1_adapter_receive.restype = C.c_bool
        for name in ("test_pending", "test_session_empty", "test_secrets_empty",
                     "test_reconfigure_alias", "test_verified", "test_pairs"):
            getattr(cls.lib, name).argtypes = [C.c_void_p]
            getattr(cls.lib, name).restype = C.c_uint
        cls.lib.test_key_matches.argtypes = [C.c_void_p, C.c_void_p]
        cls.lib.test_key_matches.restype = C.c_int

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def setUp(self):
        self.adapter = self.lib.test_make()
        self.assertTrue(self.adapter)

    def tearDown(self):
        self.lib.test_drop(self.adapter)

    def configure(self, key=KEY, length=None):
        raw = C.create_string_buffer(key) if key is not None else None
        return self.lib.cu_sl1_adapter_configure(
            self.adapter, raw, len(key) if length is None and key is not None else length or 0)

    def receive(self, data, bits=None, plaintext=True, entropy=RND_B, entropy_size=None):
        reply = Reply()
        C.memset(C.byref(reply), 0xa5, C.sizeof(reply))
        raw = C.create_string_buffer(data) if data is not None else None
        random = C.create_string_buffer(entropy) if entropy is not None else None
        size = len(data) if data is not None else 0
        handled = self.lib.cu_sl1_adapter_receive(
            self.adapter, raw, size, size * 8 if bits is None else bits, plaintext,
            random, len(entropy) if entropy_size is None and entropy is not None else entropy_size or 0,
            C.byref(reply))
        if raw is not None:
            self.assertEqual(raw.raw[:size], data, "RX input changed")
        if random is not None:
            self.assertEqual(random.raw[:len(entropy)], entropy, "Entropy input changed")
        return handled, reply.bits, bytes(reply.data[:reply.size]), reply.append_crc

    def assert_silent(self, result, handled=True):
        self.assertEqual(result, (handled, 0, b"", False))

    def start(self, **kwargs):
        self.assertTrue(self.configure())
        return self.receive(frame(START), **kwargs)

    def test_default_off_has_no_bench_fallback(self):
        self.assertFalse(self.lib.cu_sl1_adapter_enabled(self.adapter))
        self.assertTrue(self.lib.test_secrets_empty(self.adapter))
        self.assert_silent(self.receive(frame(START)), handled=False)
        public_token = token(key=bytes(range(16)))
        self.assert_silent(self.receive(public_token), handled=False)

    def test_positive_full_proof_and_hardware_crc_contract(self):
        self.assertEqual(self.start(), (True, 136, b"\x90" + AES.new(KEY, AES.MODE_ECB).encrypt(RND_B), True))
        self.assertEqual(self.receive(token()),
                         (True, 136, b"\x90" + AES.new(KEY, AES.MODE_ECB).encrypt(RND_A[1:] + RND_A[:1]), True))
        self.assertEqual(self.lib.test_verified(self.adapter), 1)
        self.assertTrue(self.lib.test_session_empty(self.adapter))
        self.assertEqual(self.lib.test_pairs(self.adapter), 0)

    def test_wrong_reader_key_returns_raw_four_bit_nak(self):
        self.start()
        self.assertEqual(self.receive(token(key=OTHER_KEY)), (True, 4, b"\x04", False))
        self.assertEqual(self.lib.test_verified(self.adapter), 0)
        self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_wrong_rotated_proof_is_rejected(self):
        self.start()
        self.assertEqual(self.receive(token(wrong_rotation=True)), (True, 4, b"\x04", False))
        self.assertEqual(self.lib.test_verified(self.adapter), 0)

    def test_token_before_start_and_replayed_token_are_rejected(self):
        self.configure()
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))
        self.receive(frame(START))
        self.receive(token())
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))
        self.assertEqual(self.lib.test_verified(self.adapter), 1)

    def test_bad_start_crc_is_silent_and_discards_pending(self):
        self.start()
        bad = frame(START)[:-1] + bytes([frame(START)[-1] ^ 1])
        self.assert_silent(self.receive(bad))
        self.assertTrue(self.lib.test_session_empty(self.adapter))
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))
        self.assertEqual(self.receive(frame(START))[1], 136)

    def test_bad_token_crc_is_silent_and_discards_pending(self):
        self.start()
        bad = token()[:-1] + bytes([token()[-1] ^ 0x80])
        self.assert_silent(self.receive(bad))
        self.assertTrue(self.lib.test_session_empty(self.adapter))
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))

    def test_bad_lengths_are_silent_even_with_valid_crc(self):
        self.configure()
        for payload in (b"\x76", b"\x76\x04", START + b"\x00", b"\x72" + bytes(31), b"\x72" + bytes(33)):
            with self.subTest(length=len(payload) + 2):
                self.receive(frame(START))
                self.assert_silent(self.receive(frame(payload)))
                self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_exact_bit_counts_required_for_both_commands(self):
        self.configure()
        for data, counts in ((frame(START), (0, 39, 41, 48)), (token(), (0, 279, 281, 288))):
            for count in counts:
                with self.subTest(length=len(data), bits=count):
                    self.receive(frame(START))
                    self.assert_silent(self.receive(data, bits=count))
                    self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_missing_or_wrong_sized_entropy_refuses_start(self):
        self.configure()
        for entropy, size in ((None, 0), (None, 16), (RND_B, 0), (RND_B, 15), (RND_B, 17)):
            with self.subTest(present=entropy is not None, size=size):
                self.receive(frame(START))
                self.assert_silent(self.receive(frame(START), entropy=entropy, entropy_size=size))
                self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_token_does_not_need_new_entropy(self):
        self.start()
        self.assertEqual(self.receive(token(), entropy=None)[1], 136)
        self.assertEqual(self.lib.test_verified(self.adapter), 1)

    def test_native_classic_has_priority_regardless_of_ciphertext_prefix(self):
        self.configure()
        for payload in (frame(START), token(), b"\x76", b"\x72", b"\x30" + bytes(20)):
            with self.subTest(length=len(payload)):
                self.receive(frame(START))
                self.assert_silent(self.receive(payload, plaintext=False), handled=False)
                self.assertTrue(self.lib.test_session_empty(self.adapter))
                self.assertTrue(self.lib.test_key_matches(self.adapter, C.create_string_buffer(KEY)))

    def test_unrelated_plaintext_left_to_legacy_and_cancels_pending(self):
        self.configure()
        for payload in (bytes.fromhex("6000"), bytes.fromhex("6100"), bytes.fromhex("3001"),
                        bytes.fromhex("e080"), b"\xc2", bytes.fromhex("5000"), bytes.fromhex("a004")):
            with self.subTest(opcode=payload[0]):
                self.receive(frame(START))
                self.assert_silent(self.receive(frame(payload)), handled=False)
                self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_other_aes_key_reference_left_to_legacy(self):
        self.start()
        self.assert_silent(self.receive(frame(bytes.fromhex("760390"))), handled=False)
        self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_field_reset_keeps_key_but_cannot_reuse_challenge(self):
        self.start()
        self.lib.cu_sl1_adapter_reset(self.adapter)
        self.assertTrue(self.lib.cu_sl1_adapter_enabled(self.adapter))
        self.assertTrue(self.lib.test_key_matches(self.adapter, C.create_string_buffer(KEY)))
        self.assertTrue(self.lib.test_session_empty(self.adapter))
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))
        self.assertEqual(self.receive(frame(START))[1], 136)

    def test_disable_for_slot_unload_wipes_key_and_pending(self):
        self.start()
        self.lib.cu_sl1_adapter_disable(self.adapter)
        self.assertTrue(self.lib.test_secrets_empty(self.adapter))
        self.assert_silent(self.receive(token()), handled=False)
        self.assert_silent(self.receive(frame(START)), handled=False)
        self.assertTrue(self.configure())
        self.assertEqual(self.receive(frame(START))[1], 136)

    def test_reconfigure_changes_key_and_invalidates_old_pending(self):
        self.start()
        self.assertTrue(self.configure(OTHER_KEY))
        self.assertTrue(self.lib.test_session_empty(self.adapter))
        self.assertEqual(self.lib.test_verified(self.adapter), 0)
        self.assertEqual(self.receive(token()), (True, 4, b"\x04", False))
        self.assertEqual(self.receive(frame(START)),
                         (True, 136, b"\x90" + AES.new(OTHER_KEY, AES.MODE_ECB).encrypt(RND_B), True))
        self.assertEqual(self.receive(token(key=OTHER_KEY))[1], 136)

    def test_invalid_reconfiguration_fails_closed(self):
        for key, size in ((None, 16), (KEY, 15), (KEY, 17), (KEY, 0)):
            with self.subTest(present=key is not None, size=size):
                self.start()
                self.assertFalse(self.configure(key, size))
                self.assertTrue(self.lib.test_secrets_empty(self.adapter))
                self.assert_silent(self.receive(frame(START)), handled=False)

    def test_key_can_be_reconfigured_from_same_context(self):
        self.start()
        self.assertTrue(self.lib.test_reconfigure_alias(self.adapter))
        self.assertTrue(self.lib.test_key_matches(self.adapter, C.create_string_buffer(KEY)))
        self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_fresh_supplied_entropy_changes_challenge(self):
        self.configure()
        first = self.receive(frame(START))
        second = self.receive(frame(START), entropy=RND_B[::-1])
        self.assertNotEqual(first[2], second[2])
        self.assertEqual(second[2], b"\x90" + AES.new(KEY, AES.MODE_ECB).encrypt(RND_B[::-1]))
        self.assertEqual(self.receive(token(rnd_b=RND_B[::-1]))[1], 136)

    def test_nulls_and_oversized_metadata_are_safe(self):
        self.configure()
        self.assert_silent(self.receive(None), handled=False)
        self.assert_silent(self.receive(b""), handled=False)
        self.assert_silent(self.receive(b"\x72" + bytes(100)))
        self.assert_silent(self.receive(frame(START), bits=C.c_size_t(-1).value))
        self.lib.cu_sl1_adapter_init(None)
        self.lib.cu_sl1_adapter_reset(None)
        self.lib.cu_sl1_adapter_disable(None)
        self.assertFalse(self.lib.cu_sl1_adapter_enabled(None))
        self.assertFalse(self.lib.cu_sl1_adapter_configure(None, None, 16))
        reply = Reply()
        self.assertFalse(self.lib.cu_sl1_adapter_receive(None, None, 0, 0, True, None, 0, C.byref(reply)))
        self.assertEqual(reply.bits, 0)
        self.receive(frame(START))
        self.assertFalse(self.lib.cu_sl1_adapter_receive(self.adapter, None, 100, 800, False, None, 0, None))
        self.assertTrue(self.lib.test_session_empty(self.adapter))

    def test_contexts_are_independent(self):
        other = self.lib.test_make()
        self.assertTrue(other)
        try:
            self.configure()
            self.assertTrue(self.lib.cu_sl1_adapter_configure(other, C.create_string_buffer(OTHER_KEY), 16))
            self.receive(frame(START))
            self.lib.cu_sl1_adapter_reset(other)
            self.assertTrue(self.lib.test_pending(self.adapter))
            self.assertEqual(self.receive(token())[1], 136)
            self.assertTrue(self.lib.cu_sl1_adapter_enabled(other))
        finally:
            self.lib.test_drop(other)


if __name__ == "__main__":
    unittest.main(verbosity=2)
