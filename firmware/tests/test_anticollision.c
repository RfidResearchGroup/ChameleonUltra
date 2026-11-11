// SPDX-License-Identifier: GPL-2.0-or-later
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfc_14a.h"
#include "crc_utils.h"
#include "byte_mirror.h"
#include "parity.h"
#include "nrf52840_bitfields.h"

#define NRF_LOG_MODULE_REGISTER()
#define NRF_LOG_INFO(...)
#define NRF_LOG_WARNING(...)
#define ASSERT assert
#define NRF_NFCT_FRAME_DELAY_MODE_WINDOWGRID NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_WindowGrid
#define NRF_NFCT_FRAME_DELAY_MODE_EXACTVAL NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_ExactVal
#define NRF_NFCT_INT_TXFRAMESTART_MASK NFCT_INTENSET_TXFRAMESTART_Msk
#define NRF_NFCT_INT_TXFRAMEEND_MASK NFCT_INTENSET_TXFRAMEEND_Msk

static struct {
    struct { uint32_t AMOUNT, FRAMECONFIG; } RXD, TXD;
    uint32_t PACKETPTR, FRAMEDELAYMODE, FRAMEDELAYMIN, FRAMEDELAYMAX;
    uint32_t INTENSET, TASKS_STARTTX;
} test_nfct;
#define NRF_NFCT (&test_nfct)
#define nrf_nfct_frame_delay_max_set(ticks) (test_nfct.FRAMEDELAYMAX = (ticks))
#define nrf_nfct_frame_delay_min_set(ticks) (test_nfct.FRAMEDELAYMIN = (ticks))
#define nrf_nfct_frame_delay_mode_set(mode) (test_nfct.FRAMEDELAYMODE = (mode))
#define nrf_nfct_frame_delay_max_get() (test_nfct.FRAMEDELAYMAX)
#define nrf_nfct_frame_delay_min_get() (test_nfct.FRAMEDELAYMIN)

/* Production code, including data_process, unwrap and both TX paths. */
#include "subject.c"

static nfc_tag_14a_coll_res_entity_t profile;
static nfc_tag_14a_coll_res_reference_t resources;
static uint16_t callback_bits;
static uint8_t callback_data[256];
static unsigned state_calls;
static unsigned reset_calls;
static unsigned rats_calls;
static uint8_t rats_parameter;
static char activation_order[16];
static unsigned activation_steps;

static nfc_tag_14a_coll_res_reference_t *get_resources(void) {
    return &resources;
}

static void state_callback(uint8_t *data, uint16_t bits) {
    state_calls++;
    callback_bits = bits;
    assert((bits + 7) / 8 <= sizeof(callback_data));
    memcpy(callback_data, data, (bits + 7) / 8);
}

static void reset_callback(void) {
    assert(activation_steps < sizeof(activation_order));
    assert(test_nfct.TASKS_STARTTX == 0);
    reset_calls++;
    activation_order[activation_steps++] = 'R';
}

static void rats_callback(uint8_t parameter) {
    assert(activation_steps < sizeof(activation_order));
    assert(test_nfct.TASKS_STARTTX == 0);
    rats_calls++;
    rats_parameter = parameter;
    activation_order[activation_steps++] = 'A';
}

static void setup(unsigned size) {
    /* Finish any selection timing left by a preceding synthetic transaction.
     * The driver normally does this at TX_FRAMEEND, outside this harness. */
    nfc_tag_14a_set_frame_delay_max(4096);
    memset(&profile, 0, sizeof(profile));
    const uint8_t example[] = {0x04, 0xa1, 0xb2, 0xc3, 0xd4, 0xe5, 0xf6,
                               0x37, 0x48, 0x59};
    profile.size = (nfc_tag_14a_uid_size)size;
    memcpy(profile.uid, example, size);
    profile.atqa[0] = 0x04 | (((size - 4) / 3) << 6);
    profile.atqa[1] = 0x03;
    profile.sak[0] = 0x20;
    resources = (nfc_tag_14a_coll_res_reference_t){
        &profile.size, profile.atqa, profile.sak, profile.uid, &profile.ats
    };
    m_tag_handler = (nfc_tag_14a_handler_t){
        .cb_reset = reset_callback,
        .cb_state = state_callback,
        .get_coll_res = get_resources,
        .cb_rats = rats_callback,
    };
    m_tag_state_14a = NFC_TAG_STATE_14A_READY;
    m_sniff_passive = false;
    memset(&test_nfct, 0, sizeof(test_nfct));
    test_nfct.FRAMEDELAYMIN = 512;
    test_nfct.FRAMEDELAYMAX = 4096;
    callback_bits = 0;
    memset(callback_data, 0, sizeof(callback_data));
    state_calls = reset_calls = rats_calls = activation_steps = 0;
    memset(activation_order, 0, sizeof(activation_order));
}

static unsigned get_bit(const uint8_t *bytes, unsigned bit) {
    return (bytes[bit / 8] >> (bit % 8)) & 1;
}

static void put_bit(uint8_t *bytes, unsigned bit, unsigned value) {
    if (value) bytes[bit / 8] |= 1u << (bit % 8);
}

/* Independent, bit-by-bit RF oracle. Do not call the production wrap routine. */
static unsigned encode(const uint8_t *data, unsigned bits, uint8_t *radio) {
    unsigned out = 0;
    memset(radio, 0, 300);
    for (unsigned bit = 0; bit < bits; bit++) {
        put_bit(radio, out++, get_bit(data, bit));
        if ((bit % 8) == 7) {
            unsigned parity = 1;
            for (unsigned b = bit - 7; b <= bit; b++) parity ^= get_bit(data, b);
            put_bit(radio, out++, parity);
        }
    }
    return out;
}

static void receive(const uint8_t *data, unsigned bits) {
    uint8_t radio[300];
    test_nfct.RXD.AMOUNT = encode(data, bits, radio);
    test_nfct.TASKS_STARTTX = 0;
    m_is_responded = false;
    nfc_tag_14a_data_process(radio);
}

static void crc_a(uint8_t *data, unsigned length) {
    uint16_t crc = 0x6363;
    for (unsigned i = 0; i < length; i++) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    data[length] = crc & 0xff;
    data[length + 1] = crc >> 8;
}

static void assert_tx_setup(bool selection_timing) {
    assert(test_nfct.TASKS_STARTTX == 1);
    assert(m_is_responded);
    assert(test_nfct.PACKETPTR == (uint32_t)(uintptr_t)m_nfc_tx_buffer);
    const uint32_t tx_interrupts = NRF_NFCT_INT_TXFRAMESTART_MASK | NRF_NFCT_INT_TXFRAMEEND_MASK;
    assert((test_nfct.INTENSET & tx_interrupts) == tx_interrupts);
    assert(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_SOF_Msk);
    const uint32_t valid_flags = NFCT_TXD_FRAMECONFIG_SOF_Msk |
        NFCT_TXD_FRAMECONFIG_PARITY_Msk | NFCT_TXD_FRAMECONFIG_DISCARDMODE_Msk |
        NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk;
    assert((test_nfct.TXD.FRAMECONFIG & ~valid_flags) == 0);
    if (selection_timing) {
        assert(test_nfct.FRAMEDELAYMODE == NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_ExactVal);
        assert(test_nfct.FRAMEDELAYMAX == 9 * 128);
    }
}

/* Model the Nordic descriptor, independently of the production sender.
 * DiscardStart keeps the HIGH bits of the first RAM byte. Hardware adds
 * parity after that partial first byte, but not after a partial last byte.
 * The reader ignores the first split-byte parity value (ISO14443-3). */
static unsigned transmitted_radio(uint8_t *radio) {
    memset(radio, 0, 300);
    const uint32_t config = test_nfct.TXD.FRAMECONFIG;
    const unsigned amount = test_nfct.TXD.AMOUNT;
    unsigned whole_bytes = (amount & NFCT_TXD_AMOUNT_TXDATABYTES_Msk) >> NFCT_TXD_AMOUNT_TXDATABYTES_Pos;
    const unsigned partial_bits = amount & NFCT_TXD_AMOUNT_TXDATABITS_Msk;
    assert(amount == whole_bytes * 8 + partial_bits);
    assert(amount > 0 && amount <= sizeof(m_nfc_tx_buffer) * 8);
    uint8_t data[sizeof(m_nfc_tx_buffer) + 2];
    memcpy(data, m_nfc_tx_buffer, whole_bytes + (partial_bits != 0));
    if (config & NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk) {
        assert(partial_bits == 0);
        crc_a(data, whole_bytes);
        whole_bytes += 2;
    }
    const unsigned bytes = whole_bytes + (partial_bits != 0);
    const bool discard_start = config & NFCT_TXD_FRAMECONFIG_DISCARDMODE_Msk;
    unsigned out = 0;
    for (unsigned byte = 0; byte < bytes; byte++) {
        const unsigned start = (byte == 0 && partial_bits && discard_start) ? 8 - partial_bits : 0;
        const unsigned end = (byte + 1 == bytes && partial_bits && !discard_start) ? partial_bits : 8;
        for (unsigned bit = start; bit < end; bit++) put_bit(radio, out++, (data[byte] >> bit) & 1);
        if ((config & NFCT_TXD_FRAMECONFIG_PARITY_Msk) && end == 8) {
            unsigned parity = 1;
            for (unsigned bit = 0; bit < 8; bit++) parity ^= (data[byte] >> bit) & 1;
            put_bit(radio, out++, parity);
        }
    }
    return out;
}

static void assert_bytes_response(const uint8_t *data, unsigned length,
                                  bool append_crc, bool selection_timing) {
    assert_tx_setup(selection_timing);
    assert(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_PARITY_Msk);
    assert(!!(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk) == append_crc);
    assert(test_nfct.TXD.AMOUNT == length * 8);
    uint8_t expected[300], actual[300], payload[258];
    memcpy(payload, data, length);
    if (append_crc) crc_a(payload, length);
    const unsigned expected_bits = encode(payload, (length + (append_crc ? 2 : 0)) * 8, expected);
    const unsigned actual_bits = transmitted_radio(actual);
    assert(actual_bits == expected_bits);
    for (unsigned bit = 0; bit < expected_bits; bit++)
        assert(get_bit(actual, bit) == get_bit(expected, bit));
}

static void cascade(unsigned level, uint8_t *uid) {
    const unsigned offset = 3 * level;
    if (profile.size - offset > 4) {
        uid[0] = 0x88;
        memcpy(uid + 1, profile.uid + offset, 3);
    } else {
        memcpy(uid, profile.uid + offset, 4);
    }
    uid[4] = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
}

static uint8_t sel(unsigned level) { return 0x93 + 2 * level; }

static void request_prefix(unsigned level, unsigned known, uint8_t *command) {
    uint8_t uid[5];
    cascade(level, uid);
    memset(command, 0, 16);
    command[0] = sel(level);
    command[1] = ((2 + known / 8) << 4) | (known % 8);
    for (unsigned i = 0; i < known; i++) put_bit(command + 2, i, get_bit(uid, i));
}

static void assert_suffix(const uint8_t *uid, unsigned known) {
    uint8_t full[300], actual[300];
    const unsigned total = encode(uid, 40, full);
    const unsigned discarded = known + known / 8;
    assert_tx_setup(true);
    assert(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_PARITY_Msk);
    assert(!(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk));
    assert(test_nfct.TXD.AMOUNT == 40 - known);
    if (known % 8) assert(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_DISCARDMODE_Msk);
    const unsigned actual_bits = transmitted_radio(actual);
    if (actual_bits != total - discarded) {
        fprintf(stderr, "known=%u expected=%u RF bits actual=%u\n",
                known, total - discarded, actual_bits);
        abort();
    }
    for (unsigned bit = 0; bit < actual_bits; bit++) {
        if (known % 8 && bit == 8 - known % 8) continue;
        if (get_bit(actual, bit) != get_bit(full, discarded + bit)) {
            fprintf(stderr, "known=%u RF suffix mismatch at bit %u\n", known, bit);
            abort();
        }
    }
    assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
}

static void prefix_case(unsigned size, unsigned level, unsigned known) {
    setup(size);
    uint8_t command[16], uid[5];
    cascade(level, uid);
    request_prefix(level, known, command);
    receive(command, 16 + known);
    assert_suffix(uid, known);
}

static void select_command(unsigned level, uint8_t *command) {
    command[0] = sel(level);
    command[1] = 0x70;
    cascade(level, command + 2);
    crc_a(command, 7);
}

static void check_selected(unsigned level, unsigned levels) {
    uint8_t sak[3] = {level + 1 < levels ? 0x04 : profile.sak[0]};
    crc_a(sak, 1);
    if (test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk)
        assert_bytes_response(sak, 1, true, true);
    else
        assert_bytes_response(sak, 3, false, true);
    if (level + 1 < levels) {
        assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
    } else {
        assert(m_tag_state_14a == NFC_TAG_STATE_14A_ACTIVE);
    }
}

static void full_uid(unsigned size) {
    setup(size);
    const unsigned levels = (size - 1) / 3;
    for (unsigned level = 0; level < levels; level++) {
        uint8_t command[16], uid[5];
        cascade(level, uid);
        request_prefix(level, 0, command);
        receive(command, 16);
        assert_suffix(uid, 0);
        select_command(level, command);
        receive(command, 72);
        check_selected(level, levels);
    }
}

static void expect_silent(uint8_t *command, unsigned bits) {
    receive(command, bits);
    assert(test_nfct.TASKS_STARTTX == 0);
    assert(!m_is_responded);
    assert(m_tag_state_14a != NFC_TAG_STATE_14A_ACTIVE);
}

static void reqa(void) {
    uint8_t command = 0x26;
    receive(&command, 7);
    assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
    assert_bytes_response(profile.atqa, 2, false, true);
}

static void prepare_rats(uint8_t *command, uint8_t parameter, bool ats) {
    m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
    const uint8_t example_ats[] = {0x06, 0x75, 0x00, 0x81, 0x02, 0x80};
    if (ats) {
        memcpy(profile.ats.data, example_ats, sizeof(example_ats));
        profile.ats.length = sizeof(example_ats);
    }
    memset(command, 0, 16);
    command[0] = 0xe0;
    command[1] = parameter;
    crc_a(command, 2);
}

static void assert_no_ats_nak(void) {
    uint8_t radio[300];
    assert(reset_calls == 1 && rats_calls == 0 && state_calls == 0);
    assert(activation_steps == 1 && activation_order[0] == 'R');
    assert_tx_setup(false);
    assert(test_nfct.TXD.AMOUNT == 4);
    assert(!(test_nfct.TXD.FRAMECONFIG & (NFCT_TXD_FRAMECONFIG_PARITY_Msk | NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk)));
    assert(transmitted_radio(radio) == 4);
    assert((radio[0] & 0x0f) == NAK_INVALID_OPERATION_TBIV);
}

/* A raw RF oracle independent of both production wrap and unwrap. Exact-sized
 * allocations expose any extra read/write at complete 9-bit boundaries. */
static void check_unwrap_raw(unsigned raw_bits, bool in_place, bool capture_parity) {
    const unsigned raw_bytes = (raw_bits + 7) / 8;
    const unsigned data_bits = raw_bits - raw_bits / 9;
    const unsigned data_bytes = (data_bits + 7) / 8;
    const unsigned parity_bytes = raw_bits / 9;
    uint8_t *raw = raw_bytes ? malloc(raw_bytes) : NULL;
    uint8_t *data = in_place ? raw : (data_bytes ? malloc(data_bytes) : NULL);
    uint8_t *parity = capture_parity && parity_bytes ? malloc(parity_bytes) : NULL;
    assert(!raw_bytes || raw);
    assert(!data_bytes || data);
    assert(!capture_parity || !parity_bytes || parity);
    uint8_t expected[300] = {0}, expected_parity[300] = {0};
    assert(raw_bytes <= 300 && data_bytes <= 300 && parity_bytes <= 300);
    for (unsigned byte = 0; byte < raw_bytes; byte++) raw[byte] = 0xa3 + byte * 37;
    if (!in_place && data_bytes) memset(data, 0xa5, data_bytes);
    if (parity) memset(parity, 0xa5, parity_bytes);
    unsigned out = 0, par = 0;
    for (unsigned bit = 0; bit < raw_bits; bit++) {
        if (bit % 9 == 8) expected_parity[par++] = get_bit(raw, bit);
        else put_bit(expected, out++, get_bit(raw, bit));
    }
    assert(out == data_bits && par == parity_bytes);
    assert(nfc_tag_14a_unwrap_frame(raw, raw_bits, data, parity) == data_bits);
    for (unsigned bit = 0; bit < data_bits; bit++)
        assert(get_bit(data, bit) == get_bit(expected, bit));
    if (data_bits % 8) assert((data[data_bytes - 1] >> (data_bits % 8)) == 0);
    if (parity) assert(memcmp(parity, expected_parity, parity_bytes) == 0);
    if (!in_place) {
        for (unsigned byte = 0; byte < raw_bytes; byte++)
            assert(raw[byte] == (uint8_t)(0xa3 + byte * 37));
        free(data);
    }
    free(parity);
    free(raw);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *name = argv[1];
    if (!strcmp(name, "full_uid_4")) full_uid(4);
    else if (!strcmp(name, "full_uid_7")) full_uid(7);
    else if (!strcmp(name, "full_uid_10")) full_uid(10);
    else if (!strcmp(name, "prefix_one_byte")) prefix_case(7, 0, 8);
    else if (!strcmp(name, "prefix_four_bytes_cl1")) prefix_case(7, 0, 32);
    else if (!strcmp(name, "prefix_four_bytes_cl2")) prefix_case(7, 1, 32);
    else if (!strcmp(name, "prefix_four_bytes_cl3")) prefix_case(10, 2, 32);
    else if (!strcmp(name, "split_bit_cl1")) prefix_case(7, 0, 1);
    else if (!strcmp(name, "split_bit_cl2")) prefix_case(7, 1, 11);
    else if (!strcmp(name, "split_bit_cl3")) prefix_case(10, 2, 23);
    else if (!strcmp(name, "all_prefix_splits")) {
        const unsigned sizes[] = {4, 7, 10};
        for (unsigned i = 0; i < 3; i++)
            for (unsigned level = 0; level < (sizes[i] - 1) / 3; level++)
                for (unsigned known = 0; known <= 32; known++)
                    prefix_case(sizes[i], level, known);
    } else if (!strcmp(name, "mismatched_byte_prefix")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 8, command);
        command[2] ^= 1;
        expect_silent(command, 24);
    } else if (!strcmp(name, "mismatched_zero_split")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 4, command);
        assert(command[2] == 8);
        command[2] = 0;
        expect_silent(command, 20);
    } else if (!strcmp(name, "truncated_prefix")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 32, command);
        expect_silent(command, 40);
    } else if (!strcmp(name, "trailing_prefix_data")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 8, command);
        expect_silent(command, 32);
    } else if (!strcmp(name, "invalid_nvb")) {
        const uint8_t values[] = {0x00, 0x10, 0x1f, 0x28, 0x2f, 0x61, 0x67,
                                   0x68, 0x6f, 0x71, 0x80, 0xff};
        for (unsigned i = 0; i < sizeof(values); i++) {
            setup(7);
            uint8_t command[16] = {0x93, values[i]};
            expect_silent(command, 16);
        }
    } else if (!strcmp(name, "invalid_nvb_matching_length")) {
        for (unsigned whole = 2; whole <= 5; whole++) {
            for (unsigned extra = 8; extra <= 15; extra++) {
                setup(7);
                uint8_t command[16];
                const unsigned known = (whole - 2) * 8 + extra;
                request_prefix(0, known, command);
                command[1] = (whole << 4) | extra;
                expect_silent(command, whole * 8 + extra);
            }
        }
    } else if (!strcmp(name, "invalid_bcc_prefix_matching_length")) {
        for (unsigned extra = 1; extra <= 7; extra++) {
            setup(7);
            uint8_t command[16];
            request_prefix(0, 32 + extra, command);
            assert(command[1] == 0x60 + extra);
            expect_silent(command, 48 + extra);
        }
    } else if (!strcmp(name, "select_bad_crc")) {
        setup(7);
        uint8_t command[16];
        select_command(0, command);
        command[7] ^= 1;
        expect_silent(command, 72);
    } else if (!strcmp(name, "select_bad_bcc")) {
        setup(7);
        uint8_t command[16];
        select_command(0, command);
        command[6] ^= 1;
        crc_a(command, 7);
        expect_silent(command, 72);
    } else if (!strcmp(name, "select_wrong_uid")) {
        setup(7);
        uint8_t command[16];
        select_command(0, command);
        command[2] ^= 1;
        command[6] ^= 1;
        crc_a(command, 7);
        expect_silent(command, 72);
    } else if (!strcmp(name, "select_invalid_length")) {
        const unsigned lengths[] = {16, 24, 63, 64, 71, 73, 80};
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            setup(7);
            uint8_t command[16] = {0};
            select_command(0, command);
            expect_silent(command, lengths[i]);
        }
    } else if (!strcmp(name, "prefix_mismatch_retry")) {
        const unsigned known_bits[] = {1, 4, 8, 11, 24, 31, 32};
        for (unsigned i = 0; i < sizeof(known_bits) / sizeof(known_bits[0]); i++) {
            setup(7);
            uint8_t command[16], uid[5];
            const unsigned known = known_bits[i];
            cascade(0, uid);
            request_prefix(0, known, command);
            command[2 + (known - 1) / 8] ^= 1u << ((known - 1) % 8);
            expect_silent(command, 16 + known);
            assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
            request_prefix(0, known, command);
            receive(command, 16 + known);
            assert_suffix(uid, known);
        }
    } else if (!strcmp(name, "select_bad_crc_retry")) {
        setup(7);
        uint8_t command[16];
        select_command(0, command);
        command[7] ^= 1;
        expect_silent(command, 72);
        assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
        command[7] ^= 1;
        receive(command, 72);
        check_selected(0, 2);
    } else if (!strcmp(name, "selection_recovery")) {
        setup(7);
        uint8_t command[16], uid[5];
        select_command(0, command);
        command[2] ^= 1;
        command[6] ^= 1;
        crc_a(command, 7);
        expect_silent(command, 72);
        assert(m_tag_state_14a == NFC_TAG_STATE_14A_IDLE);
        reqa();
        cascade(0, uid);
        request_prefix(0, 11, command);
        receive(command, 27);
        assert_suffix(uid, 11);
    } else if (!strcmp(name, "selection_timing")) {
        const unsigned prefixes[] = {0, 1, 8, 11, 24, 31, 32};
        for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
            setup(7);
            uint8_t command[16], uid[5];
            test_nfct.FRAMEDELAYMIN = 3000;
            test_nfct.FRAMEDELAYMAX = 65535;
            test_nfct.FRAMEDELAYMODE = NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_WindowGrid;
            request_prefix(0, prefixes[i], command);
            cascade(0, uid);
            receive(command, 16 + prefixes[i]);
            assert_suffix(uid, prefixes[i]);
        }
    } else if (!strcmp(name, "selection_timing_restored")) {
        setup(7);
        uint8_t command[16];
        test_nfct.FRAMEDELAYMAX = 8192;
        request_prefix(0, 1, command);
        receive(command, 17);
        assert_tx_setup(true);
        uint8_t response = 0x5a;
        nfc_tag_14a_tx_bytes(&response, 1, false);
        assert(test_nfct.FRAMEDELAYMAX == 8192);
        assert(test_nfct.FRAMEDELAYMODE == NFCT_FRAMEDELAYMODE_FRAMEDELAYMODE_WindowGrid);
        assert_bytes_response(&response, 1, false, false);
    } else if (!strcmp(name, "passive_prefix")) {
        for (unsigned known = 0; known <= 32; known++) {
            setup(7);
            uint8_t command[16];
            m_sniff_passive = true;
            request_prefix(0, known, command);
            expect_silent(command, 16 + known);
        }
    } else if (!strcmp(name, "collision_walk")) {
        setup(7);
        m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
        reqa();
        const unsigned prefixes[] = {0, 1, 4, 8, 11, 24, 31, 32};
        for (unsigned level = 0; level < 2; level++) {
            uint8_t command[16], uid[5];
            cascade(level, uid);
            for (unsigned i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
                request_prefix(level, prefixes[i], command);
                receive(command, 16 + prefixes[i]);
                assert_suffix(uid, prefixes[i]);
            }
            select_command(level, command);
            receive(command, 72);
            check_selected(level, 2);
        }
    } else if (!strcmp(name, "large_active_frame")) {
        setup(7);
        uint8_t frame[64] = {0x02};
        for (unsigned byte = 1; byte < sizeof(frame); byte++) frame[byte] = byte * 17 + 3;
        m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
        receive(frame, sizeof(frame) * 8);
        assert(callback_bits == sizeof(frame) * 8);
        assert(memcmp(callback_data, frame, sizeof(frame)) == 0);
    } else if (!strcmp(name, "rats_activation")) {
        setup(7);
        uint8_t command[16];
        prepare_rats(command, 0x8a, true);
        receive(command, 32);
        assert(reset_calls == 1 && rats_calls == 1);
        assert(activation_steps == 2 && memcmp(activation_order, "RA", 2) == 0);
        assert(rats_parameter == 0x8a);
        assert(state_calls == 0);
        assert_bytes_response(profile.ats.data, profile.ats.length, true, false);
    } else if (!strcmp(name, "rats_invalid_cid")) {
        setup(7);
        uint8_t command[16];
        prepare_rats(command, 0x8f, true);
        receive(command, 32);
        assert(reset_calls == 0 && rats_calls == 0 && state_calls == 0);
        assert(test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
        assert(m_tag_state_14a == NFC_TAG_STATE_14A_ACTIVE);
    } else if (!strcmp(name, "rats_invalid_crc")) {
        setup(7);
        uint8_t command[16];
        prepare_rats(command, 0x8a, true);
        command[2] ^= 1;
        receive(command, 32);
        assert(reset_calls == 0 && rats_calls == 0 && state_calls == 0);
        assert(test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
    } else if (!strcmp(name, "rats_invalid_length")) {
        const unsigned lengths[] = {8, 16, 24, 31, 33, 40};
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            setup(7);
            uint8_t command[16];
            prepare_rats(command, 0x8a, true);
            receive(command, lengths[i]);
            assert(reset_calls == 0 && rats_calls == 0 && state_calls == 0);
            assert(test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
        }
    } else if (!strcmp(name, "rats_without_ats")) {
        setup(7);
        uint8_t command[16];
        prepare_rats(command, 0x80, false);
        receive(command, 32);
        assert_no_ats_nak();
    } else if (!strcmp(name, "rats_missing_resources")) {
        for (unsigned missing_ats = 0; missing_ats < 2; missing_ats++) {
            setup(7);
            uint8_t command[16];
            prepare_rats(command, 0x80, true);
            if (missing_ats) resources.ats = NULL;
            else m_tag_handler.get_coll_res = NULL;
            receive(command, 32);
            assert_no_ats_nak();
        }
    } else if (!strcmp(name, "ats_rate_sanitized")) {
        const uint8_t rates[] = {0x77, 0xff, 0x80};
        for (unsigned i = 0; i < sizeof(rates); i++) {
            setup(7);
            uint8_t command[16], saved[6], expected[6];
            prepare_rats(command, 0x80, true);
            profile.ats.data[2] = rates[i];
            memcpy(saved, profile.ats.data, 6);
            memcpy(expected, saved, 6);
            expected[2] &= 0x80;
            receive(command, 32);
            assert(reset_calls == 1 && rats_calls == 1 && rats_parameter == 0x80);
            assert(memcmp(profile.ats.data, saved, 6) == 0);
            assert_bytes_response(expected, 6, true, false);
        }
    } else if (!strcmp(name, "ats_fsc_sanitized")) {
        for (uint8_t fsci = 7; fsci <= 15; fsci++) {
            setup(7);
            uint8_t command[16];
            prepare_rats(command, 0x80, true);
            profile.ats.length = 2;
            profile.ats.data[0] = 2;
            profile.ats.data[1] = fsci;
            receive(command, 32);
            const uint8_t expected[] = {2, fsci > 8 ? 8 : fsci};
            assert_bytes_response(expected, sizeof(expected), true, false);
            assert(profile.ats.data[1] == fsci);
            assert(reset_calls == 1 && rats_calls == 1);
        }
    } else if (!strcmp(name, "ats_invalid_structure")) {
        const struct { unsigned length; uint8_t bytes[21]; } examples[] = {
            {1, {2}},
            {2, {3, 0x00}},
            {2, {2, 0x10}},
            {3, {3, 0x70, 0x00}},
            {4, {4, 0x70, 0x00, 0x81}},
            {6, {7, 0x75, 0x77, 0x81, 0x02, 0x80}},
            {21, {21, 0x70, 0x00, 0x81, 0x02}},
        };
        for (unsigned i = 0; i < sizeof(examples) / sizeof(examples[0]); i++) {
            setup(7);
            uint8_t command[16], saved[21];
            prepare_rats(command, 0x80, true);
            profile.ats.length = examples[i].length;
            memcpy(profile.ats.data, examples[i].bytes, examples[i].length);
            memcpy(saved, profile.ats.data, profile.ats.length);
            receive(command, 32);
            assert(reset_calls == 1 && rats_calls == 0 && state_calls == 0);
            assert(test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
            assert(memcmp(saved, profile.ats.data, profile.ats.length) == 0);
        }
    } else if (!strcmp(name, "ats_fsd_limit")) {
        for (unsigned length = 14; length <= 15; length++) {
            setup(7);
            uint8_t command[16];
            prepare_rats(command, 0x00, true);
            memset(profile.ats.data, 0, length);
            profile.ats.length = profile.ats.data[0] = length;
            receive(command, 32);
            assert(reset_calls == 1 && state_calls == 0);
            if (length == 14) {
                assert(rats_calls == 1 && rats_parameter == 0x00);
                assert_bytes_response(profile.ats.data, length, true, false);
            } else {
                assert(rats_calls == 0 && test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
            }
        }
    } else if (!strcmp(name, "ats_valid_boundaries")) {
        const unsigned lengths[] = {1, 20};
        for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
            setup(7);
            uint8_t command[16];
            prepare_rats(command, 0x10, true);
            memset(profile.ats.data, 0, 20);
            profile.ats.length = profile.ats.data[0] = lengths[i];
            if (lengths[i] == 20) profile.ats.data[1] = 0x70;
            receive(command, 32);
            assert(reset_calls == 1 && rats_calls == 1 && rats_parameter == 0x10);
            assert_bytes_response(profile.ats.data, lengths[i], true, false);
        }
    } else if (!strcmp(name, "missing_resources")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 0, command);
        m_tag_handler.get_coll_res = NULL;
        expect_silent(command, 16);
    } else if (!strcmp(name, "unsupported_uid_size")) {
        setup(7);
        uint8_t command[16];
        request_prefix(0, 0, command);
        profile.size = (nfc_tag_14a_uid_size)5;
        expect_silent(command, 16);
    } else if (!strcmp(name, "unwrap_tight_buffers")) {
        for (unsigned bytes = 1; bytes <= 256; bytes++)
            for (unsigned in_place = 0; in_place < 2; in_place++)
                for (unsigned parity = 0; parity < 2; parity++)
                    check_unwrap_raw(bytes * 9, in_place, parity);
    } else if (!strcmp(name, "unwrap_raw_boundaries")) {
        for (unsigned raw_bits = 0; raw_bits <= 16 * 9 + 8; raw_bits++)
            for (unsigned in_place = 0; in_place < 2; in_place++)
                for (unsigned parity = 0; parity < 2; parity++)
                    check_unwrap_raw(raw_bits, in_place, parity);
        for (unsigned raw_bits = MAX_NFC_RX_BUFFER_SIZE * 8 - 9; raw_bits <= MAX_NFC_RX_BUFFER_SIZE * 8; raw_bits++)
            for (unsigned in_place = 0; in_place < 2; in_place++)
                for (unsigned parity = 0; parity < 2; parity++)
                    check_unwrap_raw(raw_bits, in_place, parity);
    } else if (!strcmp(name, "maximum_active_frame")) {
        setup(7);
        assert(MAX_NFC_RX_BUFFER_SIZE >= 288);
        uint8_t frame[256] = {0x02};
        for (unsigned byte = 1; byte < sizeof(frame); byte++) frame[byte] = byte * 37 + 0xa3;
        m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
        receive(frame, sizeof(frame) * 8);
        assert(state_calls == 1 && callback_bits == sizeof(frame) * 8);
        assert(memcmp(callback_data, frame, sizeof(frame)) == 0);
    } else if (!strcmp(name, "rx_limit_rejection")) {
        setup(7);
        uint8_t radio = 0;
        m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
        test_nfct.RXD.AMOUNT = MAX_NFC_RX_BUFFER_SIZE * 8 + 1;
        test_nfct.TASKS_STARTTX = 0;
        m_is_responded = false;
        nfc_tag_14a_data_process(&radio);
        assert(state_calls == 0 && test_nfct.TASKS_STARTTX == 0 && !m_is_responded);
    } else {
        fprintf(stderr, "Unknown test: %s\n", name);
        return 2;
    }
    return 0;
}
