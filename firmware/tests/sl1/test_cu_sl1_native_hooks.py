"""Execute the actual opt-in CU MF1/14A handlers with explicit SDK shims.

The suite uses this checkout. No device, SDK or build bootstrap is used. AES is the actual adapter/core;
NFCT registers, TX transport, RNG and Crypto1 are deterministic host shims;
RX parity removal and anticollision dispatch execute the actual source.
These tests cover dispatch/lifetime, not Crypto1 correctness or RF timing.
"""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from Crypto.Cipher import AES


ROOT = Path(__file__).resolve().parents[3]
ADAPTER = ROOT / "firmware/application/src/rfid/nfctag/hf/sl1"
CORE = ADAPTER
KEY = bytes(range(16))
UID = bytes.fromhex("11223344556677")


def frame(payload):
    value = 0x6363
    for byte in payload:
        value ^= byte
        for _ in range(8):
            value = (value >> 1) ^ (0x8408 if value & 1 else 0)
    return payload + value.to_bytes(2, "little")


def function(source, name):
    """Extract the complete actual function, including signature, verbatim."""
    match = re.search(r"(?m)^(?:static )?(?:inline )?[\w *]+\b" + name + r"\([^;]*?\)\s*\{", source)
    if not match:
        raise ValueError(f"Missing actual function: {name}")
    # Ignore braces in C comments/string literals while finding the matching end.
    token = re.compile(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', re.S)
    depth = 0
    for item in token.finditer(source, source.index("{", match.start())):
        if item.group() == "{":
            depth += 1
        elif item.group() == "}":
            depth -= 1
            if not depth:
                return source[match.start():item.end()]
    raise ValueError(f"Unbalanced actual function: {name}")


SHIM = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "sl1_framing.h"
#define PROJECT_CHAMELEON_ULTRA 1
#define __attribute__(x)
#define PACKED
#define TAG_MAX_SLOT_NUM 8
#define NRF_SUCCESS 0
#define NRF_LOG_MODULE_REGISTER(...)
#define NRF_LOG_INFO(...)
#define NRF_LOG_ERROR(...)
#define CRITICAL_REGION_ENTER() do {} while(0)
#define CRITICAL_REGION_EXIT() do {} while(0)
static unsigned host_slot = 1, host_type = 1002;
static bool host_enabled = true, host_rng_ok = true, host_abort_fill;
static unsigned host_rng_counter, host_rng_calls, host_decrypts, host_encrypted_naks;
uint8_t tag_emulation_get_slot(void) { return host_slot; }
bool is_slot_enabled(uint8_t slot, tag_sense_type_t sense) { (void)slot; (void)sense; return host_enabled; }
void tag_emulation_get_specific_types_by_slot(uint8_t slot, tag_slot_specific_type_t *out) {
    (void)slot; out->tag_hf = host_type;
}
static int nrf_drv_rng_rand(uint8_t *out, uint8_t size) {
    ++host_rng_calls; if (!host_rng_ok) return 1;
    if (host_abort_fill) { host_abort_fill = false; nfc_tag_mf1_sl1_disable(); }
    for (unsigned i = 0; i < size; ++i) out[i] = host_rng_counter++;
    return NRF_SUCCESS;
}
struct Crypto1State { uint32_t odd, even; };
static void crypto1_deinit(struct Crypto1State *s) { memset(s, 0, sizeof(*s)); }
static void crypto1_init(struct Crypto1State *s, uint64_t key) { (void)s; (void)key; }
static uint32_t crypto1_word(struct Crypto1State *s, uint32_t word, int in) { (void)s; (void)word; (void)in; return 0; }
static void mf_crypto1_decryptEx(struct Crypto1State *s, uint8_t *in, unsigned size, uint8_t *out) {
    (void)s; ++host_decrypts; for (unsigned i = 0; i < size; ++i) out[i] = in[i] ^ 0xAA;
}
static uint8_t mf_crypto1_encrypt4bit(struct Crypto1State *s, uint8_t value) { (void)s; ++host_encrypted_naks; return value ^ 0xA; }
static void mf_crypto1_encrypt(struct Crypto1State *s, uint8_t *out, unsigned size, uint8_t *par) {
    (void)s; (void)out; memset(par, 0, size);
}
static void mf_crypto1_encryptEx(struct Crypto1State *s, uint8_t *in, uint8_t *key, uint8_t *out, unsigned size, uint8_t *par) {
    (void)s; (void)key; memcpy(out, in, size); memset(par, 0, size);
}
static uint32_t prng_successor(uint32_t value, uint32_t clocks) { return value + clocks; }
static uint64_t bytes_to_num(const uint8_t *data, unsigned size) {
    uint64_t result = 0; while(size--) result = (result << 8) | *data++; return result;
}
static void num_to_bytes(uint64_t value, unsigned size, uint8_t *out) {
    while(size) { out[--size] = value; value >>= 8; }
}
static void calc_14a_crc_lut(uint8_t *data, size_t size, uint8_t *out) {
    uint16_t crc = 0x6363;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i]; for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    out[0] = crc; out[1] = crc >> 8;
}
static uint8_t host_tx[512]; static unsigned host_bits; static bool host_crc;
static volatile bool m_sniff_passive, m_nfct_field_present;
static uint8_t host_field_status;
#define NFCT_FIELDPRESENT_FIELDPRESENT_Msk 1
static uint8_t nrf_nfct_field_status_get(void) { return host_field_status; }
nfc_tag_14a_handler_t m_tag_handler;
nfc_tag_14a_state_t m_tag_state_14a;
static const uint16_t ats_fsdi_table[] = {16,24,32,40,48,64,96,128,256,256,256,256,256,256,256,256};
static uint8_t m_uid_incomplete_sak[] = {4,0xda,0x17};
static nfc_tag_14a_sniff_cb_t m_sniff_cb;
static struct { struct { uint32_t AMOUNT; } RXD; } host_nfct;
#define NRF_NFCT (&host_nfct)
#define NFCT_RXD_AMOUNT_RXDATABITS_Msk 0xffffffffU
#define NFCT_RXD_AMOUNT_RXDATABYTES_Msk 0U
/* SDK-shaped window accessors. No RF timing is modeled. */
static bool host_sensing_available = true;
static uint32_t host_frame_delay_max = 4096, host_bad_mmio, host_crypto_limit;
static uint32_t nrf_nfct_frame_delay_max_get(void) {
    if (!host_sensing_available) ++host_bad_mmio;
    return host_frame_delay_max;
}
static void nrf_nfct_frame_delay_max_set(uint32_t value) {
    if (!host_sensing_available) ++host_bad_mmio;
    host_frame_delay_max = value;
}
void nfc_tag_14a_set_frame_delay_max(uint32_t value) { nrf_nfct_frame_delay_max_set(value); }
bool nfc_tag_14a_sensing_enabled(void) { return host_sensing_available; }
void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool crc) {
    if(m_sniff_passive) return; memcpy(host_tx, data, bytes); host_bits = bytes * 8; host_crc = crc;
}
void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits) {
    if(m_sniff_passive) return; memcpy(host_tx, data, (bits + 7) / 8); host_bits = bits; host_crc = false;
}
void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits) {
    if(m_sniff_passive) return; host_tx[0] = data; host_bits = bits; host_crc = false;
}
uint8_t nfc_tag_14a_wrap_frame(const uint8_t *in, size_t bits, const uint8_t *par, uint8_t *out) {
    (void)par; memcpy(out, in, (bits + 7) / 8); return bits;
}
static bool host_adapter_receive(CuSl1Adapter *adapter, const uint8_t *rx,
        size_t size, size_t bits, bool plaintext, const uint8_t *entropy,
        size_t entropy_size, CuSl1Reply *reply) {
    host_crypto_limit = host_frame_delay_max;
    return cu_sl1_adapter_receive(adapter, rx, size, bits, plaintext, entropy, entropy_size, reply);
}
#define cu_sl1_adapter_receive host_adapter_receive
extern const uint8_t byte_mirror[256];
void nfc_tag_14a_set_reset_enable(bool enable) { (void)enable; }
'''

BRIDGE = r'''
unsigned test_window(void) { return host_frame_delay_max; }
unsigned test_crypto_limit(void) { return host_crypto_limit; }
unsigned test_bad_mmio(void) { return host_bad_mmio; }
void test_window_set(unsigned value) { host_frame_delay_max = value; }
void test_sensing_off(void) { host_sensing_available = false; }
void test_abort_next_fill(void) { host_abort_fill = true; }
static nfc_tag_mf1_information_t host_profile;
void test_init(void) {
    mf1_sl1_unload(); memset(&host_profile, 0, sizeof(host_profile));
    host_slot = 1; host_type = TAG_TYPE_MIFARE_2048; host_enabled = true;
    host_rng_counter = host_rng_calls = host_decrypts = host_encrypted_naks = 0;
    host_abort_fill = false; host_sensing_available = true;
    host_bad_mmio = host_crypto_limit = 0; host_frame_delay_max = 4096;
    host_rng_ok = true; host_field_status = 0; m_nfct_field_present = m_sniff_passive = false;
    host_profile.res_coll.size = 7;
    const uint8_t uid[] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77};
    memcpy(host_profile.res_coll.uid, uid, 7);
    host_profile.res_coll.ats.length = 2; host_profile.res_coll.ats.data[0] = 2;
    host_profile.config.mode_gen1a_magic = true;
    for (unsigned b = 0; b < 128; ++b) memset(host_profile.memory[b], b, 16);
    host_profile.memory[3][6] = 0xff; host_profile.memory[3][7] = 7; host_profile.memory[3][8] = 0x80;
    tag_data_buffer_t buffer = {.length = sizeof(host_profile), .buffer = (uint8_t *)&host_profile};
    nfc_tag_mf1_data_loadcb(TAG_TYPE_MIFARE_2048, &buffer);
    nfc_tag_mf1_reset_handler(); m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE; host_bits = 0;
}
unsigned test_feed(const uint8_t *data, unsigned size, unsigned bits, bool high) {
    uint8_t scratch[257] = {0}; host_bits = 0;
    if (high && bits >= 9) {
        unsigned wire = 0;
        for (unsigned i = 0; i < bits; ++i) {
            unsigned bit = (data[i / 8] >> (i % 8)) & 1;
            scratch[wire / 8] |= bit << (wire % 8); ++wire;
            if (i % 8 == 7) {
                unsigned odd = 1; for (unsigned j = 0; j < 8; ++j) odd ^= (data[i/8] >> j) & 1;
                scratch[wire / 8] |= odd << (wire % 8); ++wire;
            }
        }
        NRF_NFCT->RXD.AMOUNT = wire; nfc_tag_14a_data_process(scratch);
    }
    else if (high) { memcpy(scratch, data, size); NRF_NFCT->RXD.AMOUNT = bits; nfc_tag_14a_data_process(scratch); }
    else { memcpy(scratch, data, size); nfc_tag_mf1_state_handler(scratch, bits); }
    return host_bits;
}
const uint8_t *test_tx(void) { return host_tx; }
unsigned test_state(void) { return m_tag_state_14a; }
unsigned test_mf_state(void) { return m_mf1_state; }
unsigned test_pending(void) { return m_sl1.core.pending; }
unsigned test_secrets_empty(void) {
    unsigned value = 0; for(unsigned i=0;i<16;++i) value |= m_sl1.core.aes_key[i] | m_sl1.core.rnd_b[i] | m_sl1.core.challenge[i];
    return !value && !m_sl1.enabled;
}
unsigned test_counter(unsigned which) {
    if(which == 0) return m_sl1_entropy_count; if(which == 1) return host_rng_calls;
    if(which == 2) return host_decrypts; if(which == 3) return host_encrypted_naks;
    if(which == 4) return m_sl1.core.verified; return m_sl1.core.starts;
}
void test_setting(unsigned which, unsigned value) {
    if(which == 0) host_field_status = value;
    if(which == 1) host_rng_ok = value;
    if(which == 2) host_slot = value;
    if(which == 3) host_type = value;
    if(which == 4) host_enabled = value;
    if(which == 5) host_profile.res_coll.uid[0] = value;
    if(which == 6) m_sniff_passive = value;
    if(which == 7) { host_profile.res_coll.ats.length = value; host_profile.res_coll.ats.data[0] = value; }
    if(which == 8) host_profile.res_coll.ats.data[0] = value;
}
void test_authenticated(void) {
    m_mf1_state = MF1_STATE_AUTHENTICATED; AuthenticatedSector = 0; KeyInUse = 0;
    m_tag_trailer_info = (nfc_tag_mf1_trailer_info_t *)host_profile.memory[3];
    m_tag_state_14a = NFC_TAG_STATE_14A_PROPRIETARY;
}
unsigned test_memory(unsigned b, unsigned i) { return host_profile.memory[b][i]; }
bool test_reset_session(void) { return mf1_sl1_session_reset(); }
void test_replace_handler(void) { nfc_tag_14a_handler_t empty = {0}; nfc_tag_14a_set_handler(&empty); }
bool test_nonce(uint8_t *nonce) { return mf1_sl1_classic_nonce(nonce, false); }
void test_block0_identity(void) {
    host_profile.res_coll.size = 4; host_profile.config.use_mf1_coll_res = true;
    const uint8_t effective[] = {0x89,0xab,0xcd,0xef}; memcpy(host_profile.memory[0], effective, 4);
}
void test_change_block0(void) { host_profile.memory[0][0] ^= 1; }
void test_load_type(unsigned type) {
    host_type = type;
    tag_data_buffer_t buffer = {.length = sizeof(host_profile), .buffer = (uint8_t *)&host_profile};
    nfc_tag_mf1_data_loadcb(type, &buffer);
}
void test_failed_load(void) { tag_data_buffer_t empty = {0}; nfc_tag_mf1_data_loadcb(TAG_TYPE_MIFARE_1024, &empty); }
'''


class NativeHooksTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.checkout = ROOT
        hf = cls.checkout / "firmware/application/src/rfid/nfctag/hf"
        native = (hf / "nfc_mf1.c").read_text().split("// Factory data for initialization of MF1")[0]
        native = re.sub(r'^#include[^\n]*', '', native, flags=re.M)
        header14 = re.sub(r'^#include[^\n]*', '', (hf / "nfc_14a.h").read_text(), flags=re.M)
        headermf = re.sub(r'^#include[^\n]*', '', (hf / "nfc_mf1.h").read_text(), flags=re.M)
        base = (hf.parent / "tag_base_type.h").read_text()
        code14 = (hf / "nfc_14a.c").read_text()
        pure14 = [function(code14, name) for name in (
            "nfc_tag_14a_set_state", "nfc_tag_14a_set_handler", "nfc_tag_14a_set_sniff_passive", "nfc_tag_14a_is_sniff_passive",
            "nfc_tag_14a_field_present", "nfc_tag_14a_create_bcc", "nfc_tag_14a_append_bcc",
            "nfc_tag_14a_append_crc", "nfc_tag_14a_checks_crc", "nfc_tag_14a_unwrap_frame", "nfc_tag_14a_extended_ats_fits",
            "nfc_tag_14a_data_process")]
        # Inline linkage belongs to firmware translation units; provide one host definition.
        pure14 = [re.sub(r'^inline ', '', item) for item in pure14]
        preamble = '#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#define PACKED\ntypedef struct { uint16_t length; uint8_t *buffer; uint16_t *crc; } tag_data_buffer_t;\n'
        source = preamble + base + '\n' + header14 + '\n' + headermf + '\n' + SHIM + '\n' + native + '\n' + '\n'.join(pure14) + '\n' + BRIDGE
        cls.directory = tempfile.TemporaryDirectory(prefix="cu-sl1-native-")
        temporary = Path(cls.directory.name)
        (temporary / "native.c").write_text(source)
        command = ["cc", "-std=c99", "-O1", "-shared", "-fPIC", "-Wall", "-Wextra",
                   "-Werror=implicit-function-declaration", "-Werror=incompatible-pointer-types",
                   "-I", str(ADAPTER), "-I", str(CORE), str(temporary / "native.c"),
                   str(ADAPTER / "sl1_framing.c"), str(CORE / "sl1_cardauth.c"),
                   str(CORE / "vendor/aes.c"), str(hf.parents[1] / "byte_mirror.c"), "-o", str(temporary / "native.dylib")]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Actual handler host compilation failed ({result.returncode}): {result.stderr}")
        cls.lib = C.CDLL(str(temporary / "native.dylib"))
        cls.lib.nfc_tag_mf1_sl1_configure.argtypes = [C.c_uint8, C.c_void_p, C.c_uint8, C.c_void_p]
        cls.lib.nfc_tag_mf1_sl1_configure.restype = C.c_bool
        cls.lib.nfc_tag_mf1_sl1_disable.restype = C.c_bool
        cls.lib.test_feed.argtypes = [C.c_void_p, C.c_uint, C.c_uint, C.c_bool]
        cls.lib.test_tx.restype = C.POINTER(C.c_uint8)
        cls.lib.test_nonce.argtypes = [C.c_void_p]
        cls.lib.test_nonce.restype = C.c_bool
        cls.lib.test_reset_session.restype = C.c_bool
        cls.lib.nfc_tag_14a_unwrap_frame.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_void_p]
        cls.lib.nfc_tag_14a_unwrap_frame.restype = C.c_uint16
        guard_source = r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define MAX_NFC_RX_BUFFER_SIZE 257
''' + function(code14, "nfc_tag_14a_unwrap_frame") + r'''
int main(void) {
    const size_t cases[] = {9,18,315,2056};
    const size_t page = (size_t)sysconf(_SC_PAGESIZE);
    for(unsigned i=0;i<4;++i) {
        uint8_t *mapping = mmap(NULL, page*2, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        if(mapping == MAP_FAILED || mprotect(mapping+page,page,PROT_NONE)) return 2;
        const size_t bits = cases[i], bytes = (bits+7)/8, result_bits = bits-bits/9;
        uint8_t *input = mapping+page-bytes;
        memset(input, 0x69, bytes);
        uint8_t out[512], parity[512]; memset(out,0xa5,sizeof(out)); memset(parity,0xa5,sizeof(parity));
        if(nfc_tag_14a_unwrap_frame(input,bits,out,parity) != result_bits) return 3;
        if(out[(result_bits+7)/8] != 0xa5 || parity[(result_bits+7)/8] != 0xa5) return 4;
        if(nfc_tag_14a_unwrap_frame(input,bits,input,NULL) != result_bits) return 5;
        if(munmap(mapping,page*2)) return 6;
    }
    return 0;
}
'''
        (temporary / "guard.c").write_text(guard_source)
        guard_build = subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                                      str(temporary / "guard.c"), "-o", str(temporary / "guard")],
                                     capture_output=True, text=True)
        if guard_build.returncode:
            raise RuntimeError(f"Guard-page host build failed: {guard_build.stderr}")
        cls.guard = temporary / "guard"

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "directory"):
            cls.directory.cleanup()

    def setUp(self):
        self.lib.test_init()

    def enable(self, fresh=False):
        self.assertTrue(self.lib.nfc_tag_mf1_sl1_configure(1, UID, len(UID), KEY))
        # Configure deliberately resets native state; a real reader SELECTs again.
        self.lib.nfc_tag_14a_set_state(2)

    def refill(self):
        for _ in range(4):
            self.lib.nfc_tag_mf1_sl1_poll()

    def feed(self, data, bits=None, high=False):
        count = self.lib.test_feed(data, len(data), bits or len(data) * 8, high)
        return count, bytes(self.lib.test_tx()[:(count + 7) // 8])

    def test_each_native_classic_type_can_bind(self):
        for card_type in (1000, 1001, 1002, 1003):
            with self.subTest(card_type=card_type):
                self.lib.test_init()
                self.lib.test_load_type(card_type)
                self.enable(); self.refill()
                self.assertEqual(self.feed(frame(b"\x76\x04\x90"))[0], 136)

    def test_aes_window_precedes_crypto_and_preserves_actual_prior(self):
        self.enable(); self.refill()
        self.lib.test_window_set(12345)
        self.feed(frame(b"\x76\x04\x90"))
        self.assertEqual(self.lib.test_crypto_limit(), 65535)
        self.feed(frame(b"\x72" + bytes(32)))
        self.assertEqual(self.lib.test_crypto_limit(), 65535)
        self.feed(frame(b"\x60\x01"))
        self.assertEqual(self.lib.test_window(), 12345)

    def test_malformed_aes_never_extends_window(self):
        self.enable(); self.refill()
        malformed = bytearray(frame(b"\x76\x04\x90")); malformed[-1] ^= 1
        self.feed(bytes(malformed))
        self.assertEqual(self.lib.test_crypto_limit(), 4096)
        self.assertEqual(self.lib.test_window(), 4096)

    def test_unload_after_uninit_does_not_touch_nfct_registers(self):
        self.enable(); self.refill()
        self.feed(frame(b"\x76\x04\x90"))
        self.lib.test_sensing_off()
        self.lib.test_replace_handler()
        self.assertEqual(self.lib.test_bad_mmio(), 0)
        self.assertTrue(self.lib.test_secrets_empty())

    def test_window_restored_before_classic_or_disable(self):
        self.enable(); self.refill()
        self.feed(frame(b"\x76\x04\x90"))
        self.assertEqual(self.lib.test_window(), 65535)
        self.feed(frame(b"\x60\x01"))
        self.assertEqual(self.lib.test_window(), 4096)
        self.lib.test_init(); self.enable(); self.refill()
        self.feed(frame(b"\x76\x04\x90"))
        self.assertTrue(self.lib.nfc_tag_mf1_sl1_disable())
        self.assertEqual(self.lib.test_window(), 4096)

    def test_replacement_handler_cannot_rebind_old_profile(self):
        self.enable()
        self.lib.test_replace_handler()
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_configure(1, UID, len(UID), KEY))
        self.assertTrue(self.lib.test_secrets_empty())

    def test_entropy_epoch_discards_rng_completion_after_disable(self):
        self.enable()
        self.lib.test_abort_next_fill()
        self.lib.nfc_tag_mf1_sl1_poll()
        self.assertEqual(self.lib.test_counter(0), 0)
        self.assertTrue(self.lib.test_secrets_empty())

    def test_full_aes_proof_and_native_classic_dispatch(self):
        self.enable(); self.refill()
        bits, reply = self.feed(frame(b"\x76\x04\x90"), high=True)
        self.assertEqual(bits, 136)
        rnd_b = AES.new(KEY, AES.MODE_CBC, bytes(16)).decrypt(reply[1:])
        rnd_a = bytes(range(90, 106))
        token = b"\x72" + AES.new(KEY, AES.MODE_CBC, bytes(16)).encrypt(rnd_a + rnd_b[1:] + rnd_b[:1])
        bits, proof = self.feed(frame(token), high=True)
        self.assertEqual(bits, 136)
        self.assertEqual(AES.new(KEY, AES.MODE_CBC, bytes(16)).decrypt(proof[1:]), rnd_a[1:] + rnd_a[:1])
        self.assertEqual(self.lib.test_counter(4), 1)
        bits, nonce = self.feed(frame(b"\x60\x01"), high=True)
        self.assertEqual((bits, nonce), (32, bytes(range(16, 20))))
        self.assertEqual(self.lib.test_mf_state(), 1)
        # The deterministic Crypto1 shim returns zero keystream, while the
        # native authentication handler still computes/checks ReaderResponse.
        # This verifies state/dispatch continuity, not Crypto1 cryptography.
        reader_proof = (int.from_bytes(nonce, "big") + 64).to_bytes(4, "big")
        bits, answer = self.feed(bytes(4) + reader_proof, high=True)
        self.assertEqual((bits, answer), (32, (int.from_bytes(nonce, "big") + 96).to_bytes(4, "big")))
        self.assertEqual(self.lib.test_mf_state(), 2)
        self.assertEqual(self.lib.test_state(), 4)
        read = bytes(value ^ 0xaa for value in frame(b"\x30\x01"))
        self.assertEqual(self.feed(read, high=True), (144, frame(bytes([1]) * 16)))
        self.assertEqual(self.lib.test_counter(2), 1)
        self.assertEqual(self.lib.test_counter(4), 1)

    def test_entropy_never_refilled_in_callback_or_fallback(self):
        self.enable(True)
        self.assertEqual(self.feed(frame(b"\x76\x04\x90"))[0], 0)
        self.assertEqual(self.feed(frame(b"\x60\x01"))[0], 0)
        self.assertEqual(self.lib.test_counter(1), 0)
        self.lib.test_setting(1, 0); self.refill()
        self.assertEqual(self.lib.test_counter(0), 0)

    def test_fresh_classic_consumes_four_and_pool_refills(self):
        self.enable(True); self.refill()
        self.assertEqual(self.lib.test_counter(0), 64)
        self.assertEqual(self.feed(frame(b"\x60\x01")), (32, bytes(range(4))))
        self.assertEqual(self.lib.test_counter(0), 60)
        self.lib.nfc_tag_mf1_sl1_poll()
        self.assertEqual(self.lib.test_counter(0), 64)

    def test_default_off_rats_and_gen1_are_legacy(self):
        self.assertEqual(self.feed(frame(b"\xe0\x80"), high=True)[0], 16)
        self.assertEqual(self.lib.test_state(), 0)
        self.assertEqual(self.feed(b"\x40", 7), (4, b"\x0a"))
        self.assertEqual(self.lib.test_counter(1), 0)

    def test_default_off_gen1_memory_write_is_unchanged(self):
        self.assertEqual(self.feed(b"\x40", 7), (4, b"\x0a"))
        self.assertEqual(self.feed(b"\x43", 8), (4, b"\x0a"))
        self.assertEqual(self.feed(frame(b"\xa0\x01")), (4, b"\x0a"))
        self.assertEqual(self.feed(frame(bytes([0x58]) * 16)), (4, b"\x0a"))
        self.assertEqual(self.lib.test_memory(1, 0), 0x58)

    def test_rats_c2_wupa_and_select(self):
        self.enable(); self.refill()
        self.assertEqual(self.feed(frame(b"\xe0\x80"), high=True), (16, b"\x02\x00"))
        self.assertEqual(self.lib.test_state(), 2)
        self.feed(frame(b"\x76\x04\x90"), high=True)
        self.assertTrue(self.lib.test_pending())
        self.assertEqual(self.feed(frame(b"\xc2"), high=True), (8, b"\xc2"))
        self.assertEqual(self.lib.test_state(), 3)
        self.assertFalse(self.lib.test_pending())
        self.assertEqual(self.feed(b"\x26", 7, True)[0], 0)
        self.assertEqual(self.feed(b"\x52", 7, True)[0], 16)
        cl1 = bytes.fromhex("93708811223388")
        self.assertEqual(self.feed(frame(cl1), high=True)[0], 24)
        cl2 = bytes.fromhex("95704455667700")
        self.assertEqual(self.feed(frame(cl2), high=True)[0], 8)
        self.assertEqual(self.lib.test_state(), 2)

    def test_ats_does_not_truncate_or_accept_wrong_tl(self):
        self.enable(); self.lib.test_setting(7, 20)
        self.assertEqual(self.feed(frame(b"\xe0\x00"), high=True)[0], 0)
        self.assertEqual(self.lib.test_state(), 0)
        self.lib.nfc_tag_14a_set_state(2); self.lib.test_setting(8, 19)
        self.assertEqual(self.feed(frame(b"\xe0\x80"), high=True)[0], 0)

    def test_field_reset_halt_keep_key_but_clear_pending(self):
        self.enable(); self.refill(); self.feed(frame(b"\x76\x04\x90"))
        self.assertTrue(self.lib.test_reset_session())
        self.assertFalse(self.lib.test_pending())
        self.assertFalse(self.lib.test_secrets_empty())
        self.lib.nfc_tag_14a_set_state(2)
        self.feed(frame(b"\x76\x04\x90")); self.feed(frame(b"\x50\x00"), high=True)
        self.assertEqual(self.lib.test_state(), 3)
        self.assertFalse(self.lib.test_pending())
        self.assertFalse(self.lib.test_secrets_empty())

    def test_field_busy_rejects_configuration_and_disable(self):
        self.enable(); self.refill(); self.feed(frame(b"\x76\x04\x90"))
        self.lib.test_setting(0, 1)
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_disable())
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_configure(1, UID, 7, bytes(16)))
        self.assertTrue(self.lib.test_pending())
        self.lib.test_setting(0, 0)
        self.assertTrue(self.lib.nfc_tag_mf1_sl1_disable())
        self.assertTrue(self.lib.test_secrets_empty())
        self.assertEqual(self.lib.test_counter(0), 0)

    def test_identity_changes_and_replacement_clear_secrets(self):
        for setting, value in [(2, 0), (3, 1001), (4, 0), (5, 0x99)]:
            self.lib.test_init(); self.enable(); self.refill(); self.lib.test_setting(setting, value)
            self.lib.nfc_tag_mf1_sl1_poll()
            self.assertTrue(self.lib.test_secrets_empty(), setting)
        self.lib.test_init(); self.enable(); self.lib.test_replace_handler()
        self.assertTrue(self.lib.test_secrets_empty())

    def test_invalid_reconfigure_clears_previous_key(self):
        self.enable()
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_configure(0, UID, 7, KEY))
        self.assertTrue(self.lib.test_secrets_empty())

    def test_effective_block0_uid_is_the_binding(self):
        self.lib.test_block0_identity()
        self.assertTrue(self.lib.nfc_tag_mf1_sl1_configure(1, bytes.fromhex("89abcdef"), 4, KEY))
        self.lib.test_change_block0(); self.lib.nfc_tag_mf1_sl1_poll()
        self.assertTrue(self.lib.test_secrets_empty())

    def test_failed_load_and_entering_passive_wipe_key(self):
        self.enable(); self.lib.test_failed_load()
        self.assertTrue(self.lib.test_secrets_empty())
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_configure(1, UID, 7, KEY))
        self.lib.test_init(); self.enable(); self.refill()
        self.lib.nfc_tag_14a_set_sniff_passive(True)
        self.assertTrue(self.lib.test_secrets_empty())
        self.assertEqual(self.lib.test_counter(0), 0)
        self.assertEqual(self.feed(frame(b"\x76\x04\x90"), high=True)[0], 0)

    def test_slot_metadata_without_loading_that_slot_cannot_bind_old_buffer(self):
        self.lib.test_setting(2, 2)
        self.assertFalse(self.lib.nfc_tag_mf1_sl1_configure(2, UID, 7, KEY))
        self.assertTrue(self.lib.test_secrets_empty())

    def test_readonly_raw_and_encrypted_commands(self):
        for opcode in (0xa0, 0xc0, 0xc1, 0xc2, 0xb0):
            self.lib.test_init(); self.enable()
            self.assertEqual(self.feed(frame(bytes([opcode, 1]))), (4, b"\x04"))
            self.assertEqual(self.lib.test_memory(1, 0), 1)
            self.lib.test_authenticated()
            encrypted = bytes(b ^ 0xaa for b in frame(bytes([opcode, 1])))
            self.assertEqual(self.feed(encrypted), (4, b"\x0e"))
            self.assertEqual(self.lib.test_counter(2), 1)
            self.assertEqual(self.lib.test_counter(3), 1)
            self.assertEqual(self.lib.test_memory(1, 0), 1)

    def test_passive_does_no_crypto_or_transmit(self):
        self.enable(); self.refill(); self.lib.test_setting(6, 1)
        self.assertEqual(self.feed(frame(b"\x76\x04\x90"), high=True)[0], 0)
        self.assertEqual(self.feed(frame(b"\x76\x04\x90"))[0], 0)
        self.assertEqual(self.lib.test_counter(5), 0)
        self.assertEqual(self.lib.test_counter(0), 64)

    def test_ciphertext_is_not_stolen_by_aes_or_rats(self):
        self.enable(); self.refill(); self.lib.test_authenticated()
        self.lib.nfc_tag_14a_set_state(2)  # AUTHENTICATING also uses14A ACTIVE.
        self.feed(frame(b"\xe0\x80"), high=True)
        self.assertEqual(self.lib.test_counter(2), 1)
        self.assertEqual(self.lib.test_counter(5), 0)
        self.lib.test_authenticated()
        self.feed(frame(b"\x76\x04\x90"))
        self.assertEqual(self.lib.test_counter(5), 0)

    def test_actual_unwrap_preserves_short_and_long_lengths(self):
        for count in (4, 32, 35):
            payload = bytes(range(count))
            wire = []
            for byte in payload:
                wire.extend((byte >> bit) & 1 for bit in range(8))
                wire.append(1 ^ (byte.bit_count() & 1))
            packed = bytearray((len(wire) + 7) // 8 + 2)
            for i, value in enumerate(wire):
                packed[i // 8] |= value << (i % 8)
            result = C.create_string_buffer(260)
            self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(bytes(packed), len(wire), result, None), count * 8)
            self.assertEqual(result.raw[:count], payload)
        for bits in (4, 7):
            result = C.create_string_buffer(4)
            self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(b"\x04\0", bits, result, None), bits)
            self.assertEqual(result.raw[0], 4)

    def test_actual_unwrap_overflow_and_null_fail_without_writing(self):
        result = C.create_string_buffer(b"sentinel")
        before = result.raw
        self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(b"\0\0", 257 * 8 + 1, result, None), 0)
        self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(None, 7, result, None), 0)
        self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(b"\0", 7, None, None), 0)
        self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(b"\0", 0, result, None), 0)
        self.assertEqual(result.raw, before)

    def test_actual_unwrap_exact_end_guard_pages(self):
        completed = subprocess.run([str(self.guard)], capture_output=True, text=True)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_actual_unwrap_all_partial_lengths_against_bit_reference(self):
        raw = bytes((i * 73 + 19) & 255 for i in range(257))
        for bits in range(9, 2057):
            data_bits = [(raw[i // 8] >> (i % 8)) & 1 for i in range(bits) if i % 9 != 8]
            expected = bytearray((len(data_bits) + 7) // 8)
            for i, bit in enumerate(data_bits):
                expected[i // 8] |= bit << (i % 8)
            result = C.create_string_buffer(bytes([0xA5]) * 260)
            self.assertEqual(self.lib.nfc_tag_14a_unwrap_frame(raw, bits, result, None), len(data_bits), bits)
            self.assertEqual(result.raw[:len(expected)], bytes(expected), bits)
            self.assertEqual(result.raw[len(expected)], 0xA5, bits)

    def test_wrong_proof_bad_crc_and_short_magic_clear_pending(self):
        self.enable(); self.refill()
        self.feed(frame(b"\x76\x04\x90"))
        self.assertEqual(self.feed(frame(b"\x72" + bytes(32)))[0], 4)
        self.assertFalse(self.lib.test_pending())
        self.assertEqual(self.lib.test_counter(4), 0)
        self.feed(frame(b"\x76\x04\x90"))
        damaged = bytearray(frame(b"\x72" + bytes(32))); damaged[-1] ^= 1
        self.assertEqual(self.feed(bytes(damaged))[0], 0)
        self.assertFalse(self.lib.test_pending())
        self.feed(frame(b"\x76\x04\x90"))
        self.assertEqual(self.feed(b"\x40", 7), (4, b"\x04"))
        self.assertFalse(self.lib.test_pending())

    def test_authenticated_short_magic_looking_ciphertext_stays_native(self):
        self.enable(); self.refill(); self.lib.test_authenticated()
        self.assertEqual(self.feed(b"\x40", 7)[0], 0)
        self.assertEqual(self.lib.test_mf_state(), 2)
        self.assertEqual(self.lib.test_counter(5), 0)


if __name__ == "__main__":
    unittest.main()
