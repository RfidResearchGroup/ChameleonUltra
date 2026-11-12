#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_status.h"
#include "rc522.h"
#include "rfid_main.h"
#include "usb_ccid_reader.h"

/* The production packed RC522 structure contains byte fields only. These
 * checks keep the host fixture's storage/offsets consistent without importing
 * the MCU CMSIS headers. The stubs are not on the ARM build's include path. */
_Static_assert(sizeof(picc_14a_tag_t) == 271U, "RC522 card structure size");
_Static_assert(offsetof(picc_14a_tag_t, ats) == 15U, "RC522 ATS offset");
_Static_assert(offsetof(picc_14a_tag_t, ats_len) == 270U, "RC522 ATS length offset");

static device_mode_t mock_mode;
static picc_14a_tag_t mock_card;
static uint8_t mock_scan_status;
static uint16_t mock_timeout;
static unsigned scan_count;
static unsigned antenna_off_count;
static unsigned antenna_on_count;
static unsigned reset_count;
static unsigned rf_access_count;

bool g_is_reader_antenna_on;

device_mode_t get_device_mode(void) {
    return mock_mode;
}

void bsp_delay_ms(uint32_t delay_ms) {
    (void)delay_ms;
}

static void record_rf_access(void) {
    /* Tag mode has already released the RC522; any register access is a bug. */
    assert(mock_mode == DEVICE_MODE_READER);
    rf_access_count++;
}

void pcd_14a_reader_reset(void) {
    record_rf_access();
    reset_count++;
    g_is_reader_antenna_on = false;
}

void pcd_14a_reader_antenna_on(void) {
    record_rf_access();
    antenna_on_count++;
    g_is_reader_antenna_on = true;
}

void pcd_14a_reader_antenna_off(void) {
    record_rf_access();
    antenna_off_count++;
    g_is_reader_antenna_on = false;
}

uint16_t pcd_14a_reader_timeout_get(void) {
    return mock_timeout;
}

void pcd_14a_reader_timeout_set(uint16_t timeout_ms) {
    mock_timeout = timeout_ms;
}

uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag) {
    record_rf_access();
    assert(g_is_reader_antenna_on);
    assert(mock_timeout == 200U);
    scan_count++;
    if (mock_scan_status == STATUS_HF_TAG_OK) {
        *tag = mock_card;
    }
    return mock_scan_status;
}

static void fixture(uint8_t uid_length) {
    mock_mode = DEVICE_MODE_READER;
    usb_ccid_reader_external_invalidate();
    mock_scan_status = STATUS_HF_TAG_OK;
    mock_timeout = 25U;
    scan_count = 0U;
    antenna_off_count = 0U;
    antenna_on_count = 0U;
    reset_count = 0U;
    rf_access_count = 0U;
    g_is_reader_antenna_on = false;
    memset(&mock_card, 0, sizeof(mock_card));
    mock_card.uid_len = uid_length;
    mock_card.sak = 0x08U;
    mock_card.atqa[0] = 0x04U;
    for (uint8_t i = 0U; i < sizeof(mock_card.uid); i++) {
        mock_card.uid[i] = (uint8_t)(0x10U + i);
    }
}

static void assert_state(bool expected_present, bool expected_active) {
    bool present = !expected_present;
    bool active = !expected_active;
    usb_ccid_reader_get_state(&present, &active);
    assert(present == expected_present);
    assert(active == expected_active);
}

static void power_on(void) {
    uint8_t atr[32];
    size_t length = 0U;
    assert(usb_ccid_reader_power_on(atr, sizeof(atr), &length) ==
           USB_CCID_READER_OK);
    assert(length == 20U);
    assert(atr[0] == 0x3BU);
    assert(atr[1] == 0x8FU);
    assert(atr[12] == 0x03U);
    uint8_t checksum = 0U;
    for (size_t i = 1U; i < length; i++) {
        checksum ^= atr[i];
    }
    assert(checksum == 0U);
    assert_state(true, true);
    assert(g_is_reader_antenna_on);
    assert(mock_timeout == 25U);
}

static usb_ccid_reader_result_t exchange(
    const uint8_t *apdu,
    size_t apdu_length,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length
) {
    return usb_ccid_reader_transceive(
        apdu, apdu_length, response, response_capacity, response_length
    );
}

static void test_atr_card_names(void) {
    const uint8_t saks[] = {0x08U, 0x18U, 0x00U, 0x20U};
    /* SAK 00 is ambiguous: do not identify all such tags as Ultralight. */
    const uint8_t names[] = {0x01U, 0x02U, 0x00U, 0x00U};
    for (size_t i = 0U; i < sizeof(saks); i++) {
        fixture(4U);
        mock_card.sak = saks[i];
        uint8_t atr[20];
        size_t length = 0U;
        assert(usb_ccid_reader_power_on(atr, sizeof(atr), &length) ==
               USB_CCID_READER_OK);
        assert(length == sizeof(atr));
        assert(atr[13] == 0U);
        assert(atr[14] == names[i]);
        uint8_t checksum = 0U;
        for (size_t j = 1U; j < length; j++) {
            checksum ^= atr[j];
        }
        assert(checksum == 0U);
    }
}

static void test_uid_lengths_and_fresh_selection(void) {
    const uint8_t lengths[] = {4U, 7U, 10U};
    const uint8_t get_uid[] = {0xFFU, 0xCAU, 0U, 0U, 0U};
    for (size_t i = 0U; i < sizeof(lengths); i++) {
        fixture(lengths[i]);
        power_on();
        for (unsigned attempt = 0U; attempt < 2U; attempt++) {
            uint8_t response[12];
            memset(response, 0xA5, sizeof(response));
            size_t length = sizeof(response);
            unsigned before = scan_count;
            assert(exchange(get_uid, sizeof(get_uid), response, sizeof(response),
                            &length) == USB_CCID_READER_OK);
            assert(scan_count == before + 1U);
            assert(length == (size_t)lengths[i] + 2U);
            assert(memcmp(response, mock_card.uid, lengths[i]) == 0);
            assert(response[length - 2U] == 0x90U);
            assert(response[length - 1U] == 0U);
            assert(mock_timeout == 25U);
        }
    }
}

static void test_power_off_keeps_present_card_inactive(void) {
    fixture(4U);
    power_on();
    usb_ccid_reader_power_off();
    assert_state(true, false);
    assert(!g_is_reader_antenna_on);
    unsigned before = rf_access_count;
    usb_ccid_reader_power_off();
    assert(rf_access_count == before);
    assert_state(true, false);
}

static void test_uid_explicit_le_and_padding(void) {
    const uint8_t uid_lengths[] = {4U, 7U, 10U};
    for (size_t i = 0U; i < sizeof(uid_lengths); i++) {
        fixture(uid_lengths[i]);
        power_on();
        uint8_t get_uid[] = {0xFFU, 0xCAU, 0U, 0U, 0U};
        const uint8_t requested_lengths[] = {
            uid_lengths[i], (uint8_t)(uid_lengths[i] + 1U), 0xFFU,
        };
        for (size_t j = 0U; j < sizeof(requested_lengths); j++) {
            uint8_t response[258];
            memset(response, 0xA5, sizeof(response));
            size_t length = 0U;
            size_t expected = requested_lengths[j];
            get_uid[4] = requested_lengths[j];
            assert(exchange(get_uid, sizeof(get_uid), response, expected + 2U,
                            &length) == USB_CCID_READER_OK);
            assert(length == expected + 2U);
            assert(memcmp(response, mock_card.uid, uid_lengths[i]) == 0);
            for (size_t pad = uid_lengths[i]; pad < expected; pad++) {
                assert(response[pad] == 0U);
            }
            assert(response[expected] ==
                   (expected == uid_lengths[i] ? 0x90U : 0x62U));
            assert(response[expected + 1U] ==
                   (expected == uid_lengths[i] ? 0x00U : 0x82U));
            assert(response[expected + 2U] == 0xA5U);

            /* Capacity must cover the padded data and both status bytes. */
            memset(response, 0xA5, sizeof(response));
            length = sizeof(response);
            assert(exchange(get_uid, sizeof(get_uid), response, expected + 1U,
                            &length) == USB_CCID_READER_OVERFLOW);
            assert(length == 0U);
            for (size_t k = 0U; k < sizeof(response); k++) {
                assert(response[k] == 0xA5U);
            }
        }
    }
}

static void test_poll_detects_card_without_claiming_field(void) {
    fixture(4U);
    bool present = false;
    assert(usb_ccid_reader_poll(&present) == USB_CCID_READER_OK);
    assert(present);
    assert_state(true, false);
    assert(!g_is_reader_antenna_on);
    mock_scan_status = STATUS_HF_TAG_NO;
    assert(usb_ccid_reader_poll(&present) == USB_CCID_READER_NO_CARD);
    assert(!present);
    assert_state(false, false);
}

static void test_reset_and_cdc_do_not_steal_external_field(void) {
    fixture(4U);
    g_is_reader_antenna_on = true; /* The CLI left a raw exchange field alive. */
    bool present = true;
    assert(usb_ccid_reader_poll(&present) == USB_CCID_READER_BUSY);
    assert(!present);
    uint8_t atr[20];
    size_t length = 0U;
    assert(usb_ccid_reader_power_on(atr, sizeof(atr), &length) ==
           USB_CCID_READER_BUSY);
    usb_ccid_reader_ccid_reset();
    usb_ccid_reader_power_off();
    usb_ccid_reader_external_invalidate();
    assert(g_is_reader_antenna_on);
    assert(rf_access_count == 0U);
}

static void test_reset_and_cdc_release_ccid_field(void) {
    fixture(4U);
    power_on();
    usb_ccid_reader_ccid_reset();
    assert_state(false, false);
    assert(!g_is_reader_antenna_on);

    power_on();
    usb_ccid_reader_external_invalidate();
    assert_state(false, false);
    assert(!g_is_reader_antenna_on);
    const uint8_t get_uid[] = {0xFFU, 0xCAU, 0U, 0U, 0U};
    uint8_t response[12];
    size_t length = 0U;
    assert(exchange(get_uid, sizeof(get_uid), response, sizeof(response), &length) ==
           USB_CCID_READER_NO_CARD);
}

static void test_mode_switch_never_touches_released_rc522(void) {
    fixture(4U);
    power_on();
    /* The mode transition calls invalidation while the reader is still live. */
    usb_ccid_reader_external_invalidate();
    mock_mode = DEVICE_MODE_TAG;
    unsigned before = rf_access_count;
    bool present = true;
    assert(usb_ccid_reader_poll(&present) == USB_CCID_READER_NO_CARD);
    assert(!present);
    assert_state(false, false);
    usb_ccid_reader_ccid_reset();
    usb_ccid_reader_power_off();
    assert(!usb_ccid_reader_mode_is_ready());
    assert(rf_access_count == before);
}

static void test_removal_and_replacement_never_return_cached_uid(void) {
    const uint8_t get_uid[] = {0xFFU, 0xCAU, 0U, 0U, 0U};
    for (unsigned scenario = 0U; scenario < 4U; scenario++) {
        fixture(4U);
        power_on();
        if (scenario == 0U) {
            mock_scan_status = STATUS_HF_TAG_NO;
        } else if (scenario == 1U) {
            mock_card.uid[0] ^= 0x01U;
        } else if (scenario == 2U) {
            mock_card.sak ^= 0x10U;
        } else {
            mock_card.atqa[0] ^= 0x01U;
        }
        uint8_t response[12];
        memset(response, 0xA5, sizeof(response));
        size_t length = 0U;
        assert(exchange(get_uid, sizeof(get_uid), response, sizeof(response),
                        &length) == USB_CCID_READER_NO_CARD);
        assert(length == 0U);
        for (size_t i = 0U; i < sizeof(response); i++) {
            assert(response[i] == 0xA5U);
        }
        assert_state(false, false);
        assert(!g_is_reader_antenna_on);
    }
}

static void test_active_poll_detects_removal(void) {
    fixture(4U);
    power_on();
    mock_scan_status = STATUS_HF_TAG_NO;
    bool present = true;
    assert(usb_ccid_reader_poll(&present) == USB_CCID_READER_NO_CARD);
    assert(!present);
    assert_state(false, false);
    assert(!g_is_reader_antenna_on);
}

static void test_bad_and_unsupported_apdus(void) {
    fixture(4U);
    power_on();
    const uint8_t wrong_class[] = {0U, 0xCAU, 0U, 0U, 0U};
    const uint8_t wrong_instruction[] = {0xFFU, 0xCBU, 0U, 0U, 0U};
    const uint8_t wrong_p1[] = {0xFFU, 0xCAU, 1U, 0U, 0U};
    const uint8_t wrong_p2[] = {0xFFU, 0xCAU, 0U, 1U, 0U};
    const uint8_t short_le[] = {0xFFU, 0xCAU, 0U, 0U, 3U};
    const uint8_t *commands[] = {
        wrong_class, wrong_instruction, wrong_p1, wrong_p2, short_le,
    };
    const uint8_t sw1[] = {0x6EU, 0x6DU, 0x6AU, 0x6AU, 0x6CU};
    const uint8_t sw2[] = {0U, 0U, 0x86U, 0x86U, 4U};
    for (size_t i = 0U; i < sizeof(commands) / sizeof(commands[0]); i++) {
        uint8_t response[12];
        size_t length = 0U;
        assert(exchange(commands[i], 5U, response, sizeof(response), &length) ==
               USB_CCID_READER_OK);
        assert(length == 2U);
        assert(response[0] == sw1[i]);
        assert(response[1] == sw2[i]);
    }

    uint8_t response[12];
    size_t length = 0U;
    assert(exchange(short_le, 4U, response, sizeof(response), &length) ==
           USB_CCID_READER_OK);
    assert(length == 2U && response[0] == 0x67U && response[1] == 0U);
    assert(exchange(short_le, 0U, response, sizeof(response), &length) ==
           USB_CCID_READER_BAD_APDU);
    assert(exchange(short_le, 5U, response, 1U, &length) ==
           USB_CCID_READER_OVERFLOW);
    const uint8_t get_uid[] = {0xFFU, 0xCAU, 0U, 0U, 0U};
    assert(exchange(get_uid, sizeof(get_uid), response, 5U, &length) ==
           USB_CCID_READER_OVERFLOW);
}

static void test_invalid_uid_length_is_not_exposed(void) {
    const uint8_t invalid_lengths[] = {0U, 3U, 11U, 0xFFU};
    for (size_t i = 0U; i < sizeof(invalid_lengths); i++) {
        fixture(invalid_lengths[i]);
        uint8_t atr[20];
        size_t length = 0U;
        assert(usb_ccid_reader_power_on(atr, sizeof(atr), &length) !=
               USB_CCID_READER_OK);
        assert_state(false, false);
        assert(!g_is_reader_antenna_on);
    }
}

static void test_atr_overflow_releases_field(void) {
    fixture(4U);
    uint8_t atr[19];
    size_t length = 0U;
    assert(usb_ccid_reader_power_on(atr, sizeof(atr), &length) ==
           USB_CCID_READER_OVERFLOW);
    assert(!g_is_reader_antenna_on);
    assert_state(true, false);
}

int main(void) {
    test_atr_card_names();
    test_uid_lengths_and_fresh_selection();
    test_uid_explicit_le_and_padding();
    test_power_off_keeps_present_card_inactive();
    test_poll_detects_card_without_claiming_field();
    test_reset_and_cdc_do_not_steal_external_field();
    test_reset_and_cdc_release_ccid_field();
    test_mode_switch_never_touches_released_rc522();
    test_removal_and_replacement_never_return_cached_uid();
    test_active_poll_detects_removal();
    test_bad_and_unsupported_apdus();
    test_invalid_uid_length_is_not_exposed();
    test_atr_overflow_releases_field();
    puts("usb-ccid reader bridge: 13 test groups passed");
    return 0;
}
