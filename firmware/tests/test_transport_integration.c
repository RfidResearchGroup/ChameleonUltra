// SPDX-License-Identifier: GPL-2.0-or-later
/* Reuse the independent RF encoder and mock Nordic peripheral, then connect
 * the real layer-three handler/registration to the real native layer four. */
#define main anticollision_suite_main
#include "test_anticollision.c"
#undef main
#include "transport_registration.c"
#undef NRF_LOG_INFO
#include "../application/src/rfid/nfctag/hf/nfc_14a_4.c"

void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense,
                                            fds_slot_record_map_t *map) {
    map->id = slot;
    map->key = sense;
}

bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *buffer) {
    return false;
}

static void send_body(const uint8_t *body, unsigned length) {
    uint8_t frame[270] = {0};
    assert(length + 2 <= sizeof(frame));
    memcpy(frame, body, length);
    crc_a(frame, length);
    receive(frame, (length + 2) * 8);
}

static void expect_tx(const uint8_t *body, unsigned length) {
    assert(test_nfct.TASKS_STARTTX == 1);
    assert(test_nfct.TXD.AMOUNT == length * 8);
    assert(test_nfct.TXD.FRAMECONFIG & NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk);
    assert(memcmp(m_nfc_tx_buffer, body, length) == 0);
}

int main(void) {
    setup(7);
    static nfc_tag_14a_4_information_t slot;
    slot.res_coll = profile;
    const uint8_t ats[] = {0x06, 0x75, 0x77, 0x81, 0x02, 0x80};
    slot.res_coll.ats.length = sizeof(ats);
    memcpy(slot.res_coll.ats.data, ats, sizeof(ats));
    tag_data_buffer_t storage = {.length = sizeof(slot), .buffer = (uint8_t *)&slot};
    assert(nfc_tag_14a_4_data_loadcb(TAG_TYPE_HF14A_4, &storage) == sizeof(slot));
    m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;

    const uint8_t reqa = 0x26;
    receive(&reqa, 7);
    assert(m_tag_state_14a == NFC_TAG_STATE_14A_READY);
    for (unsigned level = 0; level < 2; level++) {
        uint8_t command[16], uid[5];
        cascade(level, uid);
        unsigned known = level == 0 ? 3 : 32;
        request_prefix(level, known, command);
        receive(command, 16 + known);
        assert_suffix(uid, known);
        select_command(level, command);
        receive(command, 72);
        check_selected(level, 2);
    }
    assert(!m_tcl_session_state.m_activated);
    const uint8_t rats[] = {0xe0, 0x85};
    send_body(rats, sizeof(rats));
    const uint8_t expected_ats[] = {0x06, 0x75, 0x00, 0x81, 0x02, 0x80};
    expect_tx(expected_ats, sizeof(expected_ats));
    assert(memcmp(slot.res_coll.ats.data, ats, sizeof(ats)) == 0);
    assert(m_tcl_session_state.m_activated && m_tcl_session_state.m_cid == 5);
    assert(test_nfct.FRAMEDELAYMAX == 4096);

    const uint8_t pps[] = {0xd5, 0x11, 0x00};
    send_body(pps, sizeof(pps));
    expect_tx(pps, 1);
    const uint8_t command[] = {0x0a, 0x05, 0x90, 0x60};
    send_body(command, sizeof(command));
    const uint8_t wtx[] = {0xfa, 0x05, 0x3b};
    expect_tx(wtx, sizeof(wtx));
    uint8_t apdu[260];
    uint16_t apdu_len = 0;
    assert(nfc_tag_14a_4_get_pending_apdu(apdu, &apdu_len));
    assert(apdu_len == 2 && memcmp(apdu, command + 2, 2) == 0);
    const uint8_t response[] = {0x91, 0x00};
    nfc_tag_14a_4_set_response(response, sizeof(response));
    const uint8_t wrong_multiplier[] = {0xfa, 0x05, 0x01};
    send_body(wrong_multiplier, sizeof(wrong_multiplier));
    assert(test_nfct.TASKS_STARTTX == 0 && m_tcl_session_state.m_response_ready);
    send_body(wtx, sizeof(wtx));
    const uint8_t first_response[] = {0x0a, 0x05, 0x91, 0x00};
    expect_tx(first_response, sizeof(first_response));

    const uint8_t chained[] = {0x1b, 0x05, 0x90, 0xbd, 0x00};
    const uint8_t rack[] = {0xab, 0x05};
    send_body(chained, sizeof(chained));
    expect_tx(rack, sizeof(rack));
    send_body(chained, sizeof(chained));
    expect_tx(rack, sizeof(rack));
    assert(!nfc_tag_14a_4_get_pending_apdu(apdu, &apdu_len));
    const uint8_t final[] = {0x0a, 0x05, 0x01, 0x02, 0x03};
    send_body(final, sizeof(final));
    expect_tx(wtx, sizeof(wtx));
    assert(nfc_tag_14a_4_get_pending_apdu(apdu, &apdu_len));
    const uint8_t expected_apdu[] = {0x90, 0xbd, 0x00, 0x01, 0x02, 0x03};
    assert(apdu_len == sizeof(expected_apdu));
    assert(memcmp(apdu, expected_apdu, sizeof(expected_apdu)) == 0);
    nfc_tag_14a_4_set_response(response, sizeof(response));
    send_body(wtx, sizeof(wtx));
    expect_tx(first_response, sizeof(first_response));
    assert(!nfc_tag_14a_4_get_pending_apdu(apdu, &apdu_len));

    slot.res_coll.ats.length = 2;
    slot.res_coll.ats.data[0] = 2;
    slot.res_coll.ats.data[1] = 9; /* Configured FSC512 exceeds the receiver. */
    const uint8_t fresh_rats[] = {0xe0, 0x80};
    send_body(fresh_rats, sizeof(fresh_rats));
    const uint8_t bounded_ats[] = {2, 8};
    expect_tx(bounded_ats, sizeof(bounded_ats));
    assert(slot.res_coll.ats.data[1] == 9);
    assert(m_tcl_session_state.m_activated && m_tcl_session_state.m_fsc == 256);
    return 0;
}
