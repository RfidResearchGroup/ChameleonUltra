#include "lf_reader_main.h"

#include "bsp_delay.h"
#include "bsp_time.h"
#include "hex_utils.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "protocols/em410x.h"
#include "protocols/ioprox.h"
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
 * @brief Search ioProx tag
 * @param output 16 bytes ioprox_codec_t->data layout: version, facility code, card number, raw8
 * @return STATUS_LF_TAG_OK on success
 */
uint8_t scan_ioprox(uint8_t *data, uint8_t format_hint) {
    if (ioprox_read(data, format_hint, g_timeout_readem_ms)) {
        return STATUS_LF_TAG_OK;
    }
    return STATUS_LF_TAG_NO_FOUND;
}

/**
 * @brief Decode raw8 data to structured ioProx format
 * @param raw8 Input 8 bytes
 * @param output 16 bytes ioprox_codec_t->data layout: version, facility code, card number, raw8
 * @return STATUS_SUCCESS on success
 */
uint8_t decode_ioprox_raw(uint8_t *raw8, uint8_t *output) {
    if (ioprox_decode_raw_to_data(raw8, output)) {
        return STATUS_SUCCESS;
    }
    return STATUS_CMD_ERR;
}

/**
 * @brief Encode ioProx parameters to structured ioProx format
 * @param ver Version byte
 * @param fc Facility code byte
 * @param cn Card number (16-bit)
 * @param out 16 bytes ioprox_codec_t->data layout: version, facility code, card number, raw8
 * @return STATUS_SUCCESS on success
 */
uint8_t encode_ioprox_params(uint8_t ver, uint8_t fc, uint16_t cn, uint8_t *out) {
    if (ioprox_encode_params_to_data(ver, fc, cn, out)) {
        return STATUS_SUCCESS;
    }
    return STATUS_CMD_ERR;
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
 * Write ioprox card data to t55xx
 */
uint8_t write_ioprox_to_t55xx(uint8_t *card_data, uint8_t *new_passwd, uint8_t *old_passwds, uint8_t old_passwd_count) {
    // Prepare T5577 block array: index 0 = config word, 1-2 = data blocks
    uint32_t blks[3] = {0x00};

    uint8_t blk_count = ioprox_t55xx_writer(card_data, blks);

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
