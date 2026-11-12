#include "usb_ccid_reader.h"

#if defined(PROJECT_CHAMELEON_ULTRA)

#include <string.h>

#include "app_status.h"
#include "bsp_delay.h"
#include "rc522.h"
#include "rfid_main.h"

/*
 * Expose PC/SC storage-card UID retrieval, not an ISO-DEP APDU tunnel.
 * Commands and USB events run on the application loop. Ownership is therefore
 * a lease across calls, rather than a mutex: CDC/BLE reader commands revoke
 * the CCID lease, and background polling skips any field left on by them.
 */

static picc_14a_tag_t m_tag;
static bool m_present;
static bool m_active;
static bool m_owns_field;

extern bool g_is_reader_antenna_on;

static void clear_card(void) {
    memset(&m_tag, 0, sizeof(m_tag));
    m_present = false;
    m_active = false;
}

static bool valid_uid_length(uint8_t length) {
    return length == 4U || length == 7U || length == 10U;
}

static bool same_card(const picc_14a_tag_t *a, const picc_14a_tag_t *b) {
    return valid_uid_length(a->uid_len) &&
           a->uid_len == b->uid_len &&
           a->sak == b->sak &&
           memcmp(a->atqa, b->atqa, sizeof(a->atqa)) == 0 &&
           memcmp(a->uid, b->uid, a->uid_len) == 0;
}

static uint8_t scan_card(picc_14a_tag_t *tag, bool keep_field) {
    if (get_device_mode() != DEVICE_MODE_READER) {
        return STATUS_HF_TAG_NO;
    }

    /* A field cycle also brings cards left in HALT/ISO-DEP back to IDLE. */
    memset(tag, 0, sizeof(*tag));
    pcd_14a_reader_antenna_off();
    bsp_delay_ms(5U);
    pcd_14a_reader_reset();
    pcd_14a_reader_antenna_on();
    bsp_delay_ms(8U);

    uint16_t previous_timeout = pcd_14a_reader_timeout_get();
    pcd_14a_reader_timeout_set(200U);
    uint8_t status = pcd_14a_reader_scan_auto(tag);
    pcd_14a_reader_timeout_set(previous_timeout);
    if (status == STATUS_HF_TAG_OK && !valid_uid_length(tag->uid_len)) {
        status = STATUS_HF_ERR_STAT;
    }

    if (!keep_field || status != STATUS_HF_TAG_OK) {
        pcd_14a_reader_antenna_off();
    }
    return status;
}

static usb_ccid_reader_result_t map_status(uint8_t status) {
    if (status == STATUS_HF_TAG_OK) {
        return USB_CCID_READER_OK;
    }
    if (status == STATUS_HF_TAG_NO) {
        return USB_CCID_READER_NO_CARD;
    }
    return USB_CCID_READER_IO_ERROR;
}

static uint16_t card_name(const picc_14a_tag_t *tag) {
    switch (tag->sak) {
        case 0x08U: return 0x0001U; /* MIFARE Classic 1K */
        case 0x18U: return 0x0002U; /* MIFARE Classic 4K */
        /* SAK 00 is shared by several tag families; keep its card name unknown. */
        default:    return 0x0000U;
    }
}

static uint8_t atr_lrc(const uint8_t *atr, size_t length_without_lrc) {
    uint8_t value = 0U;
    for (size_t i = 1U; i < length_without_lrc; i++) {
        value ^= atr[i];
    }
    return value;
}

static usb_ccid_reader_result_t build_storage_atr(
    const picc_14a_tag_t *tag,
    uint8_t *atr,
    size_t capacity,
    size_t *length
) {
    static const uint8_t prefix[] = {
        0x3B, 0x8F, 0x80, 0x01, 0x80, 0x4F, 0x0C,
        0xA0, 0x00, 0x00, 0x03, 0x06,
    };
    if (capacity < 20U) {
        return USB_CCID_READER_OVERFLOW;
    }

    memcpy(atr, prefix, sizeof(prefix));
    atr[12] = 0x03U;
    uint16_t name = card_name(tag);
    atr[13] = (uint8_t)(name >> 8U);
    atr[14] = (uint8_t)name;
    memset(&atr[15], 0, 4U);
    atr[19] = atr_lrc(atr, 19U);
    *length = 20U;
    return USB_CCID_READER_OK;
}

static usb_ccid_reader_result_t reselect_card(void) {
    picc_14a_tag_t current;
    uint8_t status = scan_card(&current, true);
    if (status != STATUS_HF_TAG_OK || !same_card(&current, &m_tag)) {
        if (status == STATUS_HF_TAG_OK) {
            pcd_14a_reader_antenna_off();
        }
        clear_card();
        m_owns_field = false;
        return USB_CCID_READER_NO_CARD;
    }
    memcpy(&m_tag, &current, sizeof(m_tag));
    return USB_CCID_READER_OK;
}

static void reconcile_state(void) {
    /*
     * A mode change can uninitialize the SPI reader. Never touch its registers
     * from a later USB reset/event after that point.
     */
    if (get_device_mode() != DEVICE_MODE_READER ||
            (m_owns_field && !g_is_reader_antenna_on)) {
        clear_card();
        m_owns_field = false;
    }
}

void usb_ccid_reader_ccid_reset(void) {
    reconcile_state();
    if (m_owns_field) {
        pcd_14a_reader_antenna_off();
    }
    clear_card();
    m_owns_field = false;
}

usb_ccid_reader_result_t usb_ccid_reader_poll(bool *present) {
    if (present == NULL) {
        return USB_CCID_READER_IO_ERROR;
    }
    reconcile_state();
    if (!usb_ccid_reader_mode_is_ready()) {
        *present = false;
        return USB_CCID_READER_NO_CARD;
    }
    if (m_owns_field && m_active) {
        usb_ccid_reader_result_t result = reselect_card();
        *present = m_present;
        return result;
    }
    if (g_is_reader_antenna_on) {
        *present = m_present;
        return USB_CCID_READER_BUSY;
    }

    picc_14a_tag_t tag;
    uint8_t status = scan_card(&tag, false);
    m_present = status == STATUS_HF_TAG_OK;
    if (m_present) {
        memcpy(&m_tag, &tag, sizeof(m_tag));
    } else {
        clear_card();
    }
    *present = m_present;
    return map_status(status);
}

usb_ccid_reader_result_t usb_ccid_reader_power_on(
    uint8_t *atr,
    size_t atr_capacity,
    size_t *atr_length
) {
    if (atr == NULL || atr_length == NULL) {
        return USB_CCID_READER_IO_ERROR;
    }
    *atr_length = 0U;
    reconcile_state();
    if (!usb_ccid_reader_mode_is_ready()) {
        return USB_CCID_READER_NO_CARD;
    }
    if (!m_owns_field && g_is_reader_antenna_on) {
        return USB_CCID_READER_BUSY;
    }

    usb_ccid_reader_power_off();
    clear_card();
    m_owns_field = true;

    picc_14a_tag_t tag;
    usb_ccid_reader_result_t result = map_status(scan_card(&tag, true));
    if (result != USB_CCID_READER_OK) {
        m_owns_field = false;
        return result;
    }

    memcpy(&m_tag, &tag, sizeof(m_tag));
    m_present = true;
    m_active = true;
    result = build_storage_atr(&m_tag, atr, atr_capacity, atr_length);
    if (result != USB_CCID_READER_OK) {
        usb_ccid_reader_power_off();
    }
    return result;
}

void usb_ccid_reader_power_off(void) {
    reconcile_state();
    if (!m_owns_field) {
        return;
    }
    pcd_14a_reader_antenna_off();
    /* Power-off is not a physical card-removal event. Polling checks presence. */
    m_active = false;
    m_owns_field = false;
}

usb_ccid_reader_result_t usb_ccid_reader_transceive(
    const uint8_t *apdu,
    size_t apdu_length,
    uint8_t *response,
    size_t response_capacity,
    size_t *response_length
) {
    if (apdu == NULL || response == NULL || response_length == NULL ||
            apdu_length == 0U || apdu_length > USB_CCID_READER_MAX_APDU) {
        return USB_CCID_READER_BAD_APDU;
    }
    *response_length = 0U;
    if (response_capacity < 2U) {
        return USB_CCID_READER_OVERFLOW;
    }
    reconcile_state();
    if (!m_owns_field || !m_active || !m_present) {
        return USB_CCID_READER_NO_CARD;
    }

    /* Do not return a cached UID after removal or replacement of the card. */
    usb_ccid_reader_result_t result = reselect_card();
    if (result != USB_CCID_READER_OK) {
        return result;
    }

    if (apdu_length != 5U) {
        response[0] = 0x67U;
        response[1] = 0x00U;
        *response_length = 2U;
        return USB_CCID_READER_OK;
    }
    if (apdu[0] != 0xFFU) {
        response[0] = 0x6EU;
        response[1] = 0x00U;
        *response_length = 2U;
        return USB_CCID_READER_OK;
    }
    if (apdu[1] != 0xCAU) {
        response[0] = 0x6DU;
        response[1] = 0x00U;
        *response_length = 2U;
        return USB_CCID_READER_OK;
    }
    if (apdu[2] != 0x00U || apdu[3] != 0x00U) {
        response[0] = 0x6AU;
        response[1] = 0x86U;
        *response_length = 2U;
        return USB_CCID_READER_OK;
    }
    if (apdu[4] != 0U && apdu[4] < m_tag.uid_len) {
        response[0] = 0x6CU;
        response[1] = m_tag.uid_len;
        *response_length = 2U;
        return USB_CCID_READER_OK;
    }
    size_t data_length = apdu[4] == 0U ? m_tag.uid_len : apdu[4];
    if (data_length + 2U > response_capacity) {
        return USB_CCID_READER_OVERFLOW;
    }
    memcpy(response, m_tag.uid, m_tag.uid_len);
    /*
     * PC/SC Part 3, table 3-9: Le=0 requests the exact UID. For a larger
     * explicit Le, pad the missing data with zeroes and report warning 6282.
     */
    if (data_length > m_tag.uid_len) {
        memset(&response[m_tag.uid_len], 0, data_length - m_tag.uid_len);
        response[data_length] = 0x62U;
        response[data_length + 1U] = 0x82U;
    } else {
        response[data_length] = 0x90U;
        response[data_length + 1U] = 0x00U;
    }
    *response_length = data_length + 2U;
    return USB_CCID_READER_OK;
}

bool usb_ccid_reader_mode_is_ready(void) {
    return get_device_mode() == DEVICE_MODE_READER;
}

void usb_ccid_reader_get_state(bool *present, bool *active) {
    reconcile_state();
    if (present != NULL) {
        *present = m_present;
    }
    if (active != NULL) {
        *active = m_active;
    }
}

void usb_ccid_reader_external_invalidate(void) {
    usb_ccid_reader_ccid_reset();
}

#endif
