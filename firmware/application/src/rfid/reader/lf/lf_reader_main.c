#include "lf_reader_main.h"

#include "bsp_delay.h"
#include "bsp_time.h"
#include "hex_utils.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/em410x.h"
#include "protocols/hidprox.h"
#include "protocols/t55xx.h"
#include "protocols/pac.h"
#include "protocols/viking.h"

#define NRF_LOG_MODULE_NAME lf_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

// The default card search is available N Millisecond timeout
static uint32_t g_timeout_readem_ms = 500;

/**
 * Search EM410X tag
 */
uint8_t scan_em410x(uint8_t *uid) {
    if (em410x_read(uid, g_timeout_readem_ms)) {
        return STATUS_LF_TAG_OK;
    }
    return STATUS_LF_TAG_NO_FOUND;
}

/**
 * Search HID Prox tag
 */
uint8_t scan_hidprox(uint8_t *data, uint8_t format_hint) {
    if (hidprox_read(data, format_hint, g_timeout_readem_ms)) {
        return STATUS_LF_TAG_OK;
    }
    return STATUS_LF_TAG_NO_FOUND;
}

/**
 * Search PAC/Stanley tag
 */
uint8_t scan_pac(uint8_t *card_id) {
    if (pac_read(card_id, g_timeout_readem_ms)) {
        return STATUS_LF_TAG_OK;
    }
    return STATUS_LF_TAG_NO_FOUND;
}

/**
 * Search Viking tag
 */
uint8_t scan_viking(uint8_t *uid) {
    if (viking_read(uid, g_timeout_readem_ms)) {
        return STATUS_LF_TAG_OK;
    }
    return STATUS_LF_TAG_NO_FOUND;
}

/**
 * Try reset t55XX tag passwords by enumerating old passwords.
 */
static void try_reset_t55xx_passwd(uint32_t new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    for (uint8_t i = 0; i < old_passwd_count; i++) {
        uint32_t old_passwd = bytes_to_num(old_passwds + i * 4, 4);
        t55xx_reset_passwd(old_passwd, new_passwd);
    }
    t55xx_reset_passwd(new_passwd, new_passwd);
}

/**
 * Write card data to t55xx
 */
static uint8_t write_t55xx(uint32_t *blks, uint8_t blk_count, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    uint32_t passwd = bytes_to_num(new_passwd, 4);

    start_lf_125khz_radio();
    bsp_delay_ms(1);  // Delays for a while after starting the field

    try_reset_t55xx_passwd(passwd, old_passwds, old_passwd_count);
    t55xx_write_data(passwd, blks, blk_count);

    stop_lf_125khz_radio();

    // writing results should be verified by upper computer
    return STATUS_LF_TAG_OK;
}

/**
 * Write em410x card data to t55xx
 */
uint8_t write_em410x_to_t55xx(uint8_t *uid, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    uint32_t blks[7] = {0x00};
    uint8_t blk_count = em410x_t55xx_writer(uid, blks);
    if (blk_count == 0) {
        return STATUS_PAR_ERR;
    }
    return write_t55xx(blks, blk_count, new_passwd, old_passwds, old_passwd_count);
}

uint8_t write_em410x_electra_to_t55xx(uint8_t *uid, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    uint32_t blks[7] = {0x00};
    uint8_t blk_count = em410x_electra_t55xx_writer(uid, blks);
    if (blk_count == 0) {
        return STATUS_PAR_ERR;
    }
    return write_t55xx(blks, blk_count, new_passwd, old_passwds, old_passwd_count);
}

/**
 * Write hidprox card data to t55xx
 */
uint8_t write_hidprox_to_t55xx(uint8_t format, uint32_t fc, uint64_t cn, uint32_t il, uint32_t oem, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    wiegand_card_t card = {
        .format = format,
        .card_number = cn,
        .facility_code = fc,
        .issue_level = il,
        .oem = oem,
    };
    uint32_t blks[7] = {0x00};
    uint8_t blk_count = hidprox_t55xx_writer(&card, blks);
    if (blk_count == 0) {
        return STATUS_PAR_ERR;
    }
    return write_t55xx(blks, blk_count, new_passwd, old_passwds, old_passwd_count);
}

/**
 * Write viking card data to t55xx
 */
uint8_t write_viking_to_t55xx(uint8_t *uid, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    uint32_t blks[7] = {0x00};
    uint8_t blk_count = viking_t55xx_writer(uid, blks);
    if (blk_count == 0) {
        return STATUS_PAR_ERR;
    }
    return write_t55xx(blks, blk_count, new_passwd, old_passwds, old_passwd_count);
}

uint8_t write_pac_to_t55xx(uint8_t *data, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    uint32_t blks[7] = {0x00};
    uint8_t blk_count = pac_t55xx_writer(data, blks);
    if (blk_count == 0) return STATUS_PAR_ERR;
    return write_t55xx(blks, blk_count, new_passwd, old_passwds, old_passwd_count);
}

/**
 * Set the LF card scanning timeout value (in milliseconds).
 */
void set_scan_tag_timeout(uint32_t ms) { g_timeout_readem_ms = ms; }
