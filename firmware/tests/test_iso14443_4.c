// SPDX-License-Identifier: GPL-2.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crc_utils.h"
#include "../application/src/rfid/nfctag/hf/nfc_14a_4.c"
#include "../application/src/rfid/nfctag/hf/nfc_seos.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static nfc_tag_14a_handler_t registered_handler;
static nfc_tag_14a_4_information_t slot_data;
static uint8_t transmitted[512];
static uint32_t transmitted_length;
static bool transmitted_crc;
static unsigned tx_count;
static nfc_tag_14a_state_t tag_state;
static nfc_tag_seos_information_t seos_data;
static nfc_tag_seos_information_t *test_seos_information = &seos_data;
static nfc_tag_14a_4_tcl_state_t test_seos_session;

/* Actual SEOS activation wrapper, extracted by the runner without crypto code. */
#define m_tag_information test_seos_information
#define m_tcl_session_state test_seos_session
#include "seos_rats_handler.inc"
#undef m_tag_information
#undef m_tcl_session_state

void nfc_tag_14a_set_state(nfc_tag_14a_state_t state) { tag_state = state; }

void nfc_tag_14a_set_handler(nfc_tag_14a_handler_t *handler) {
    registered_handler = *handler;
}

void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool append_crc) {
    if (bytes > sizeof(transmitted)) abort();
    memcpy(transmitted, data, bytes);
    transmitted_length = bytes;
    transmitted_crc = append_crc;
    tx_count++;
}

bool nfc_tag_14a_checks_crc(uint8_t *data, size_t length) {
    if (length < 3) return false;
    uint16_t crc;
    calc_14a_crc_lut(data, length - 2, (uint8_t *)&crc);
    return data[length - 2] == (uint8_t)crc && data[length - 1] == (uint8_t)(crc >> 8);
}

void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense,
                                            fds_slot_record_map_t *map) {
    map->id = slot;
    map->key = sense;
}

bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer) {
    return false;
}

static void reset_fixture(void) {
    memset(&slot_data, 0, sizeof(slot_data));
    const uint8_t ats[] = {0x05, 0x78, 0x00, 0x80, 0x02};
    memcpy(slot_data.res_coll.ats.data, ats, sizeof(ats));
    slot_data.res_coll.ats.length = sizeof(ats);
    tag_data_buffer_t buffer = {.length = sizeof(slot_data), .buffer = (uint8_t *)&slot_data};
    nfc_tag_14a_4_data_loadcb(TAG_TYPE_HF14A_4, &buffer);
    registered_handler.cb_reset();
    registered_handler.cb_rats(0x80);
    tx_count = 0;
    transmitted_length = 0;
    memset(transmitted, 0, sizeof(transmitted));
    tag_state = NFC_TAG_STATE_14A_ACTIVE;
}

static size_t make_frame(uint8_t *frame, const uint8_t *body, size_t length) {
    memcpy(frame, body, length);
    uint16_t crc;
    calc_14a_crc_lut(frame, length, (uint8_t *)&crc);
    frame[length] = (uint8_t)crc;
    frame[length + 1] = (uint8_t)(crc >> 8);
    return length + 2;
}

static void receive(const uint8_t *body, size_t length) {
    uint8_t frame[512] = {0};
    size_t frame_length = make_frame(frame, body, length);
    registered_handler.cb_state(frame, frame_length * 8);
}

static int check_apdu(const uint8_t *expected, uint16_t expected_length) {
    uint8_t apdu[NFC_14A_4_MAX_APDU];
    uint16_t length = 0;
    CHECK(nfc_tag_14a_4_get_pending_apdu(apdu, &length));
    CHECK(length == expected_length);
    CHECK(memcmp(apdu, expected, length) == 0);
    return 0;
}

static int receive_bits_and_crc(void) {
    const uint8_t body[] = {0x02, 0x90, 0x60, 0x00, 0x00, 0x00};
    receive(body, sizeof(body));
    return check_apdu(body + 1, sizeof(body) - 1);
}

static int invalid_crc(void) {
    const uint8_t body[] = {0x02, 0x90, 0x60};
    uint8_t frame[512] = {0};
    size_t length = make_frame(frame, body, sizeof(body));
    frame[length - 1] ^= 1;
    registered_handler.cb_state(frame, length * 8);
    CHECK(!m_tcl_session_state.m_apdu_pending);
    CHECK(tx_count == 0);
    return 0;
}

static int partial_byte(void) {
    const uint8_t body[] = {0x02, 0x90, 0x60};
    uint8_t frame[512] = {0};
    size_t length = make_frame(frame, body, sizeof(body));
    registered_handler.cb_state(frame, length * 8 - 1);
    CHECK(!m_tcl_session_state.m_apdu_pending);
    CHECK(tx_count == 0);
    return 0;
}

static int cid_response(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t command[] = {0x90, 0x60};
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_add_static_response(command, sizeof(command), response, sizeof(response));
    const uint8_t body[] = {0x0a, 0x05, 0x90, 0x60};
    receive(body, sizeof(body));
    CHECK(check_apdu(command, sizeof(command)) == 0);
    const uint8_t expected[] = {0x0a, 0x05, 0x91, 0x00};
    CHECK(tx_count == 1 && transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    CHECK(transmitted_crc);
    return 0;
}

static int long_apdu(void) {
    uint8_t body[201] = {0x02};
    for (size_t i = 1; i < sizeof(body); i++) body[i] = (uint8_t)i;
    receive(body, sizeof(body));
    return check_apdu(body + 1, sizeof(body) - 1);
}

static int deselect(void) {
    const uint8_t body[] = {0xc2};
    receive(body, sizeof(body));
    CHECK(tx_count == 1 && transmitted_length == sizeof(body));
    CHECK(transmitted[0] == 0xc2 && transmitted_crc);
    CHECK(!m_tcl_session_state.m_activated && tag_state == NFC_TAG_STATE_14A_HALTED);
    return 0;
}

static int wtx_ready_response(void) {
    const uint8_t command[] = {0x02, 0x90, 0x60};
    receive(command, sizeof(command));
    CHECK(tx_count == 1 && transmitted[0] == 0xf2 && transmitted[1] == 0x3b);
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_set_response(response, sizeof(response));
    const uint8_t body[] = {0xf2, 0x3b};
    receive(body, sizeof(body));
    const uint8_t expected[] = {0x02, 0x91, 0x00};
    CHECK(tx_count == 2 && transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    return 0;
}

static int chained_apdu(void) {
    const uint8_t first[] = {0x12, 0x90, 0xbd, 0x00};
    const uint8_t last[] = {0x03, 0x01, 0x02, 0x03};
    receive(first, sizeof(first));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    CHECK(tx_count == 1 && transmitted_length == 1 && transmitted[0] == 0xa2);
    receive(last, sizeof(last));
    const uint8_t expected[] = {0x90, 0xbd, 0x00, 0x01, 0x02, 0x03};
    return check_apdu(expected, sizeof(expected));
}

static int chain_overflow(void) {
    uint8_t first[201] = {0x12};
    uint8_t last[101] = {0x03};
    receive(first, sizeof(first));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    receive(last, sizeof(last));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    return 0;
}

static int pps_cid(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t body[] = {0xd5, 0x11, 0x00};
    receive(body, sizeof(body));
    CHECK(tx_count == 1 && transmitted_length == 1 && transmitted[0] == 0xd5);
    return 0;
}

static int pps_unsupported_rate(void) {
    const uint8_t body[] = {0xd0, 0x11, 0x11};
    receive(body, sizeof(body));
    CHECK(tx_count == 0);
    return 0;
}

static int truncated_optional_fields(void) {
    const uint8_t body[] = {0x0e, 0x05};
    receive(body, sizeof(body));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    return 0;
}

static int static_response_retransmission(void) {
    const uint8_t command[] = {0x90, 0x60};
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_add_static_response(command, sizeof(command), response, sizeof(response));
    const uint8_t body[] = {0x02, 0x90, 0x60};
    receive(body, sizeof(body));
    const uint8_t expected[] = {0x02, 0x91, 0x00};
    CHECK(transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    receive(body, sizeof(body));
    CHECK(tx_count == 2 && transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    CHECK(m_tcl_session_state.m_block_num == 1);
    return 0;
}

static int deselect_other_session(void) {
    nfc_tag_14a_4_tcl_state_t other;
    nfc_tag_14a_4_activate(&other, 0x80, &slot_data.res_coll.ats);
    other.m_block_num = 1;
    other.m_apdu_pending = true;
    m_tcl_session_state.m_block_num = 1;
    const uint8_t body[] = {0xc2};
    uint8_t frame[512] = {0};
    size_t length = make_frame(frame, body, sizeof(body));
    CHECK(!nfc_tag_14a_4_base_handler(&other, frame, length * 8));
    CHECK(other.m_block_num == 0 && !other.m_apdu_pending);
    CHECK(m_tcl_session_state.m_block_num == 1);
    return 0;
}

static int chained_retransmission(void) {
    const uint8_t first[] = {0x12, 0x90, 0xbd, 0x00};
    const uint8_t last[] = {0x03, 0x01, 0x02, 0x03};
    receive(first, sizeof(first));
    receive(first, sizeof(first));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    CHECK(transmitted_length == 1 && transmitted[0] == 0xa2);
    receive(last, sizeof(last));
    const uint8_t expected[] = {0x90, 0xbd, 0x00, 0x01, 0x02, 0x03};
    return check_apdu(expected, sizeof(expected));
}

static int maximum_apdu(void) {
    uint8_t first[201] = {0x12};
    uint8_t last[61] = {0x03};
    uint8_t expected[NFC_14A_4_MAX_APDU];
    memset(first + 1, 0x55, sizeof(first) - 1);
    memset(last + 1, 0xaa, sizeof(last) - 1);
    memcpy(expected, first + 1, sizeof(first) - 1);
    memcpy(expected + sizeof(first) - 1, last + 1, sizeof(last) - 1);
    receive(first, sizeof(first));
    CHECK(!m_tcl_session_state.m_apdu_pending);
    receive(last, sizeof(last));
    return check_apdu(expected, sizeof(expected));
}

static int native_activation(void) {
    registered_handler.cb_reset();
    const uint8_t pps[] = {0xd0, 0x01};
    receive(pps, sizeof(pps));
    CHECK(tx_count == 0 && !m_tcl_session_state.m_activated);
    registered_handler.cb_rats(0x45);
    CHECK(m_tcl_session_state.m_activated && m_tcl_session_state.m_pps_allowed);
    CHECK(m_tcl_session_state.m_cid == 5 && m_tcl_session_state.m_cid_supported);
    CHECK(m_tcl_session_state.m_fsd == 48 && m_tcl_session_state.m_fsc == 256);
    registered_handler.cb_rats(0x8f);
    CHECK(!m_tcl_session_state.m_activated);
    return 0;
}

static int seos_activation_wrapper(void) {
    memset(&seos_data, 0, sizeof(seos_data));
    seos_data.res_coll.ats = slot_data.res_coll.ats;
    nfc_tag_seos_rats_handler(0x45);
    CHECK(test_seos_session.m_activated && test_seos_session.m_cid == 5);
    CHECK(test_seos_session.m_cid_supported && test_seos_session.m_fsd == 48);
    CHECK(m_tcl_session_state.m_cid == 0); /* The native session is separate. */
    seos_data.res_coll.ats.data[4] = 0;
    nfc_tag_seos_rats_handler(0x85);
    CHECK(test_seos_session.m_activated && !test_seos_session.m_cid_supported);
    seos_data.res_coll.ats.length = 2;
    seos_data.res_coll.ats.data[0] = 2;
    seos_data.res_coll.ats.data[1] = 0x08; /* TC absent: default CID support. */
    nfc_tag_seos_rats_handler(0x85);
    CHECK(test_seos_session.m_cid_supported);
    return 0;
}

static int cid_address_validation(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t wrong[] = {0x0a, 0x06, 0x90, 0x60};
    const uint8_t reserved[] = {0x0a, 0x15, 0x90, 0x60};
    const uint8_t missing[] = {0x02, 0x90, 0x60};
    receive(wrong, sizeof(wrong));
    receive(reserved, sizeof(reserved));
    receive(missing, sizeof(missing));
    CHECK(tx_count == 0 && m_tcl_session_state.m_block_num == 0);
    CHECK(!m_tcl_session_state.m_apdu_pending && m_tcl_session_state.m_cid == 5);
    const uint8_t valid[] = {0x0a, 0x05, 0x90, 0x60};
    receive(valid, sizeof(valid));
    CHECK(tx_count == 1 && transmitted[0] == 0xfa && transmitted[1] == 5);
    return 0;
}

static int unsupported_cid_and_nad(void) {
    slot_data.res_coll.ats.data[4] = 0;
    registered_handler.cb_rats(0x80);
    const uint8_t cid[] = {0x0a, 0x00, 0x90, 0x60};
    const uint8_t nad[] = {0x06, 0x00, 0x90, 0x60};
    receive(cid, sizeof(cid));
    receive(nad, sizeof(nad));
    CHECK(tx_count == 0 && !m_tcl_session_state.m_apdu_pending);
    const uint8_t plain[] = {0x02, 0x90, 0x60};
    receive(plain, sizeof(plain));
    CHECK(tx_count == 1 && transmitted[0] == 0xf2);
    return 0;
}

static int pps_activation_and_cid(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t wrong[] = {0xd6, 0x11, 0x00};
    const uint8_t correct[] = {0xd5, 0x11, 0x00};
    receive(wrong, sizeof(wrong));
    CHECK(tx_count == 0 && m_tcl_session_state.m_pps_allowed);
    receive(correct, sizeof(correct));
    CHECK(tx_count == 1 && transmitted[0] == 0xd5 && !m_tcl_session_state.m_pps_allowed);
    receive(correct, sizeof(correct));
    CHECK(tx_count == 1);
    return 0;
}

static int pps_after_i_block(void) {
    const uint8_t command[] = {0x02, 0x90, 0x60};
    const uint8_t pps[] = {0xd0, 0x01};
    receive(command, sizeof(command));
    receive(pps, sizeof(pps));
    CHECK(tx_count == 1 && m_tcl_session_state.m_wtx_pending);
    return 0;
}

static int pps_invalid_closes_negotiation(void) {
    const uint8_t invalid[] = {0xd0, 0x02};
    const uint8_t valid[] = {0xd0, 0x01};
    receive(invalid, sizeof(invalid));
    receive(valid, sizeof(valid));
    CHECK(tx_count == 0 && !m_tcl_session_state.m_pps_allowed);
    return 0;
}

static int wtx_cid_exchange(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t command[] = {0x0a, 0x05, 0x90, 0x60};
    const uint8_t response[] = {0x91, 0x00};
    const uint8_t ack[] = {0xfa, 0x05, 0x3b};
    receive(command, sizeof(command));
    CHECK(tx_count == 1 && transmitted[0] == 0xfa && transmitted[1] == 5);
    nfc_tag_14a_4_set_response(response, sizeof(response));
    receive(ack, sizeof(ack));
    const uint8_t expected[] = {0x0a, 0x05, 0x91, 0x00};
    CHECK(tx_count == 2 && transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    CHECK(!m_tcl_session_state.m_wtx_pending && !m_tcl_session_state.m_response_ready);
    return 0;
}

static int wtx_invalid_acknowledgments(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t command[] = {0x0a, 0x05, 0x90, 0x60};
    const uint8_t response[] = {0x91, 0x00};
    receive(command, sizeof(command));
    nfc_tag_14a_4_set_response(response, sizeof(response));
    const uint8_t wrong[][3] = {{0xfa, 5, 1}, {0xfa, 6, 59}, {0xfa, 5, 0}, {0xfa, 5, 60}};
    for (unsigned i = 0; i < sizeof(wrong) / sizeof(wrong[0]); i++) {
        receive(wrong[i], sizeof(wrong[i]));
        CHECK(tx_count == 1 && m_tcl_session_state.m_response_ready && m_tcl_session_state.m_wtx_pending);
    }
    const uint8_t missing_cid[] = {0xf2, 59};
    receive(missing_cid, sizeof(missing_cid));
    CHECK(tx_count == 1 && m_tcl_session_state.m_response_ready);
    const uint8_t truncated[] = {0xfa, 5};
    const uint8_t trailing[] = {0xfa, 5, 59, 0};
    receive(truncated, sizeof(truncated));
    receive(trailing, sizeof(trailing));
    uint8_t crc_bad[16];
    const uint8_t ack_body[] = {0xfa, 5, 59};
    unsigned crc_length = make_frame(crc_bad, ack_body, sizeof(ack_body));
    crc_bad[crc_length - 1] ^= 1;
    registered_handler.cb_state(crc_bad, crc_length * 8);
    CHECK(tx_count == 1 && m_tcl_session_state.m_response_ready && m_tcl_session_state.m_wtx_pending);
    const uint8_t correct[] = {0xfa, 5, 59};
    receive(correct, sizeof(correct));
    CHECK(tx_count == 2 && !m_tcl_session_state.m_response_ready);
    receive(correct, sizeof(correct));
    CHECK(tx_count == 2); /* An unsolicited second ACK cannot consume anything. */
    return 0;
}

static int wtx_unsolicited(void) {
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_set_response(response, sizeof(response));
    const uint8_t ack[] = {0xf2, 59};
    receive(ack, sizeof(ack));
    CHECK(tx_count == 0 && !m_tcl_session_state.m_response_ready);
    return 0;
}

static int wtx_repeat_until_ready(void) {
    const uint8_t command[] = {0x02, 0x90, 0x60};
    const uint8_t ack[] = {0xf2, 59};
    receive(command, sizeof(command));
    receive(ack, sizeof(ack));
    CHECK(tx_count == 2 && transmitted[0] == 0xf2 && m_tcl_session_state.m_wtx_pending);
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_set_response(response, sizeof(response));
    nfc_tag_14a_4_base_respond(&m_tcl_session_state);
    CHECK(tx_count == 2 && m_tcl_session_state.m_response_ready); /* Wait for ACK. */
    receive(ack, sizeof(ack));
    CHECK(tx_count == 3 && transmitted[0] == 0x02);
    return 0;
}

static int wtx_duplicate_request(void) {
    const uint8_t command[] = {0x02, 0x90, 0x60};
    receive(command, sizeof(command));
    CHECK(check_apdu(command + 1, sizeof(command) - 1) == 0);
    receive(command, sizeof(command));
    CHECK(tx_count == 2 && transmitted[0] == 0xf2);
    CHECK(!m_tcl_session_state.m_apdu_pending && m_tcl_session_state.m_apdu_len == 2);
    const uint8_t premature_next[] = {0x03, 0x90, 0x61};
    receive(premature_next, sizeof(premature_next));
    CHECK(tx_count == 2 && m_tcl_session_state.m_apdu_buf[1] == 0x60);
    return 0;
}

static int stateful_chained_reader(void) {
    uint8_t reader_bn = 0;
    const uint8_t payloads[] = {0x90, 0xbd, 0x00};
    for (unsigned i = 0; i < sizeof(payloads); i++) {
        uint8_t frame[] = {0x12 | reader_bn, payloads[i]};
        receive(frame, sizeof(frame));
        CHECK(transmitted_length == 1 && transmitted[0] == (0xa2 | reader_bn));
        receive(frame, sizeof(frame)); /* Lost ACK: same fragment again. */
        CHECK(transmitted[0] == (0xa2 | reader_bn));
        CHECK(m_tcl_session_state.m_apdu_len == i + 1);
        reader_bn ^= 1;
    }
    const uint8_t final[] = {0x02 | reader_bn, 0x01};
    receive(final, sizeof(final));
    const uint8_t expected[] = {0x90, 0xbd, 0x00, 0x01};
    return check_apdu(expected, sizeof(expected));
}

static int rblock_recovery(void) {
    const uint8_t command[] = {0x90, 0x60};
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_add_static_response(command, sizeof(command), response, sizeof(response));
    const uint8_t frame[] = {0x02, 0x90, 0x60};
    receive(frame, sizeof(frame));
    const uint8_t nak[] = {0xb2};
    receive(nak, sizeof(nak));
    const uint8_t expected[] = {0x02, 0x91, 0x00};
    CHECK(tx_count == 2 && transmitted_length == sizeof(expected));
    CHECK(memcmp(transmitted, expected, sizeof(expected)) == 0);
    CHECK(m_tcl_session_state.m_block_num == 1);
    return 0;
}

static int check_outgoing_fragmentation(unsigned response_length) {
    registered_handler.cb_rats(0x05); /* FSD16, CID5 => 12 INF bytes per block. */
    const uint8_t command[] = {0x90, 0x60};
    uint8_t response[NFC_14A_4_MAX_APDU];
    for (unsigned i = 0; i < response_length; i++) response[i] = i;
    nfc_tag_14a_4_add_static_response(command, sizeof(command), response, response_length);
    const uint8_t request[] = {0x0a, 5, 0x90, 0x60};
    receive(request, sizeof(request));
    unsigned copied = 0;
    uint8_t reader_bn = 0;
    const uint8_t wrong_cid_ack[] = {0xab, 6};
    const uint8_t trailing_ack[] = {0xab, 5, 0};
    receive(wrong_cid_ack, sizeof(wrong_cid_ack));
    receive(trailing_ack, sizeof(trailing_ack));
    CHECK(tx_count == 1 && m_tcl_session_state.m_resp_offset == 12);
    while (1) {
        CHECK(transmitted_length + 2 <= 16 && transmitted[1] == 5);
        CHECK((transmitted[0] & 1) == reader_bn);
        unsigned length = transmitted_length - 2;
        CHECK(memcmp(transmitted + 2, response + copied, length) == 0);
        copied += length;
        if (!(transmitted[0] & 0x10)) break;
        uint8_t previous[32];
        unsigned previous_length = transmitted_length;
        memcpy(previous, transmitted, previous_length);
        uint8_t nak[] = {0xba | reader_bn, 5};
        receive(nak, sizeof(nak));
        CHECK(transmitted_length == previous_length && memcmp(transmitted, previous, previous_length) == 0);
        reader_bn ^= 1;
        uint8_t ack[] = {0xaa | reader_bn, 5};
        receive(ack, sizeof(ack));
    }
    CHECK(copied == response_length && !m_tcl_session_state.m_chaining_out);
    CHECK(m_tcl_session_state.m_block_num == (reader_bn ^ 1));
    uint8_t next_request[] = {0x0a | (reader_bn ^ 1), 5, 0x90, 0x60};
    receive(next_request, sizeof(next_request));
    CHECK((transmitted[0] & 1) == (reader_bn ^ 1));
    CHECK(memcmp(transmitted + 2, response, transmitted_length - 2) == 0);
    return 0;
}

static int outgoing_fragmentation(void) { return check_outgoing_fragmentation(35); }
static int outgoing_fragmentation_even(void) { return check_outgoing_fragmentation(24); }
static int outgoing_fragmentation_maximum(void) { return check_outgoing_fragmentation(260); }

static int initial_presence_with_cid(void) {
    registered_handler.cb_rats(0x85);
    const uint8_t nak[] = {0xba, 5};
    receive(nak, sizeof(nak));
    CHECK(tx_count == 1 && transmitted_length == 2);
    CHECK(transmitted[0] == 0xab && transmitted[1] == 5);
    CHECK(m_tcl_session_state.m_block_num == 0);
    const uint8_t command[] = {0x0a, 5, 0x90, 0x60};
    receive(command, sizeof(command));
    CHECK(tx_count == 2 && m_tcl_session_state.m_apdu_pending);
    return 0;
}

static int empty_i_block(void) {
    const uint8_t empty[] = {0x02};
    receive(empty, sizeof(empty));
    CHECK(tx_count == 1 && transmitted_length == 1 && transmitted[0] == 0x02);
    CHECK(!m_tcl_session_state.m_apdu_pending);
    receive(empty, sizeof(empty));
    CHECK(tx_count == 2 && transmitted_length == 1 && transmitted[0] == 0x02);
    return 0;
}

static int malformed_control_frames(void) {
    const uint8_t deselect_payload[] = {0xc2, 0x90};
    const uint8_t rack_payload[] = {0xa2, 0x90};
    const uint8_t invalid_iblock[] = {0x00, 0x90, 0x60};
    receive(deselect_payload, sizeof(deselect_payload));
    receive(rack_payload, sizeof(rack_payload));
    receive(invalid_iblock, sizeof(invalid_iblock));
    CHECK(tx_count == 0 && m_tcl_session_state.m_activated && m_tcl_session_state.m_block_num == 0);
    return 0;
}

static int fsc_boundaries(void) {
    slot_data.res_coll.ats.data[1] = 0x70; /* FSC16, CID support still present. */
    registered_handler.cb_rats(0x80);
    uint8_t too_large[15] = {0x02}; /* 17 bytes including CRC. */
    receive(too_large, sizeof(too_large));
    CHECK(tx_count == 0 && !m_tcl_session_state.m_apdu_pending);
    uint8_t exact[14] = {0x02}; /* Exactly FSC16 including CRC. */
    receive(exact, sizeof(exact));
    CHECK(tx_count == 1 && m_tcl_session_state.m_apdu_len == 13);
    return 0;
}

int main(int argc, char **argv) {
    struct {
        const char *name;
        int (*run)(void);
    } cases[] = {
        {"receive_bits_and_crc", receive_bits_and_crc},
        {"invalid_crc", invalid_crc},
        {"partial_byte", partial_byte},
        {"cid_response", cid_response},
        {"long_apdu", long_apdu},
        {"deselect", deselect},
        {"wtx_ready_response", wtx_ready_response},
        {"chained_apdu", chained_apdu},
        {"chain_overflow", chain_overflow},
        {"pps_cid", pps_cid},
        {"pps_unsupported_rate", pps_unsupported_rate},
        {"truncated_optional_fields", truncated_optional_fields},
        {"static_response_retransmission", static_response_retransmission},
        {"deselect_other_session", deselect_other_session},
        {"chained_retransmission", chained_retransmission},
        {"maximum_apdu", maximum_apdu},
        {"native_activation", native_activation},
        {"seos_activation_wrapper", seos_activation_wrapper},
        {"cid_address_validation", cid_address_validation},
        {"unsupported_cid_and_nad", unsupported_cid_and_nad},
        {"pps_activation_and_cid", pps_activation_and_cid},
        {"pps_after_i_block", pps_after_i_block},
        {"pps_invalid_closes_negotiation", pps_invalid_closes_negotiation},
        {"wtx_cid_exchange", wtx_cid_exchange},
        {"wtx_invalid_acknowledgments", wtx_invalid_acknowledgments},
        {"wtx_unsolicited", wtx_unsolicited},
        {"wtx_repeat_until_ready", wtx_repeat_until_ready},
        {"wtx_duplicate_request", wtx_duplicate_request},
        {"stateful_chained_reader", stateful_chained_reader},
        {"rblock_recovery", rblock_recovery},
        {"outgoing_fragmentation", outgoing_fragmentation},
        {"outgoing_fragmentation_even", outgoing_fragmentation_even},
        {"outgoing_fragmentation_maximum", outgoing_fragmentation_maximum},
        {"initial_presence_with_cid", initial_presence_with_cid},
        {"empty_i_block", empty_i_block},
        {"malformed_control_frames", malformed_control_frames},
        {"fsc_boundaries", fsc_boundaries},
    };
    if (argc != 2) return 2;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        if (strcmp(argv[1], cases[i].name) == 0) {
            reset_fixture();
            return cases[i].run();
        }
    }
    return 2;
}
