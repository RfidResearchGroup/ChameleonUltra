#include "bsp_delay.h"
#include "hex_utils.h"
#include "lf_125khz_radio.h"
#include "nrf_gpio.h"
#include "protocols/t55xx.h"
#include "timeslot.h"

#define NRF_LOG_MODULE_NAME lf_t55xx
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

const uint32_t start_gap = 30 * 8;     // 30Tc
const uint32_t write_gap = 9 * 8;      // 9Tc
const uint32_t gap_sep_zero = 24 * 8;  // 24Tc
const uint32_t gap_sep_one = 54 * 8;   // 54Tc

static struct {
    uint8_t opcode;
    uint8_t lock_bit;
    uint32_t *data;
    uint32_t *passwd;
    uint8_t blk_addr;
} t55xx_cmd;

void t55xx_send_gap(uint32_t nus) {
    stop_lf_125khz_radio();  // turn off 125khz field
    bsp_delay_us(nus);
    start_lf_125khz_radio();  // turn on 125khz field
}

void t55xx_tx_bit(uint8_t data) {
    if (data & 0x01) {
        bsp_delay_us(gap_sep_one);
    } else {
        bsp_delay_us(gap_sep_zero);
    }
    t55xx_send_gap(write_gap);
}

void t55xx_tx_uint32_t(uint32_t data) {
    for (uint8_t i = 0; i < 32; i++) {
        t55xx_tx_bit((data >> (31 - i)) & 1);
    }
}

// t55xx high-precision timing control function
void t55xx_timeslot_callback() {
    t55xx_send_gap(start_gap);

    // send instructions first
    t55xx_tx_bit(t55xx_cmd.opcode >> 1);
    t55xx_tx_bit(t55xx_cmd.opcode & 1);

    // the instruction does not need to be sent when it is 00
    if (t55xx_cmd.opcode == 0) {
        return;
    }

    // if you need it after the instruction, you can send the password
    if (t55xx_cmd.passwd != NULL) {
        t55xx_tx_uint32_t(*t55xx_cmd.passwd);
    }

    // process lock position
    if (t55xx_cmd.lock_bit == 0 || t55xx_cmd.lock_bit == 1) {
        t55xx_tx_bit(t55xx_cmd.lock_bit & 1);
    }

    if (t55xx_cmd.data != NULL) {
        t55xx_tx_uint32_t(*t55xx_cmd.data);
    }

    // processing address
    if (t55xx_cmd.blk_addr != 255) {
        t55xx_tx_bit(t55xx_cmd.blk_addr >> 2);
        t55xx_tx_bit(t55xx_cmd.blk_addr >> 1);
        t55xx_tx_bit(t55xx_cmd.blk_addr & 1);
    }
}

/**
 * @brief Write to 5577 instructions, this instruction can be read and write
 *
 * @param opcode Operating code, should be 1* for normal operations, only the reset is 00.
 * @param passwd Password, send when not NULL, 32bit, start transmission from the bidding 0.
 * @param lock_bit Locking position may only be 1 or 0. Passing other values means not using LOCK bit (for password awakening mode)
 * @param data Data, 32 bits, transmitted from the lower bit 0
 * @param blk_addr Block number, 3 bit 0-7 yuan, input 255 means not using this bit (for password wake-up mode)
 */
void t55xx_send_cmd(uint8_t opcode, uint32_t *passwd, uint8_t lock_bit, uint32_t *data, uint8_t blk_addr) {
    // Password reading mode,        2op(1+bck)  32pw    1(0)            3addr
    // Password writing mode,        2op(1+bck)  32pw    1l      32data  3addr
    // Password wake-up mode,        2op(1+0)    32pw

    // Read the mode directly,       2op(1+bck)          1(0)            3addr
    // Standard writing mode,        2op(1+bck)          1l      32data  3addr

    // This will not be implemented
    // Standard read page mode,      2op(1+bck)
    // Reset mode,                   2op(0+0)

    t55xx_cmd.opcode = opcode;
    t55xx_cmd.passwd = passwd;
    t55xx_cmd.lock_bit = lock_bit;
    t55xx_cmd.data = data;
    t55xx_cmd.blk_addr = blk_addr;

    // request timing, and wait for the order operation to complete
    request_timeslot(37 * 1000, t55xx_timeslot_callback);

    if (opcode != 0) {
        bsp_delay_ms(6);  // Maybe continue to write a card next time, you need to wait more for a while
    } else {
        bsp_delay_ms(1);
    }
}

/**
 * @brief generic t55xx write data
 *
 * @param passwd the password for the final encryption (also the current password of the card)
 * @param blks the blocks data to write
 * @param blk_count the number of blocks to write
 */
void t55xx_write_data(uint32_t passwd, uint32_t *blks, uint8_t blk_count) {
    // write control bits (blk0) & data (w/wo passwd)
    for (uint8_t i = 0; i < blk_count; i++) {
        t55xx_send_cmd(T5577_OPCODE_PAGE0, &passwd, 0, &blks[i], i);
        t55xx_send_cmd(T5577_OPCODE_PAGE0, NULL, 0, &blks[i], i);
    }
    t55xx_send_cmd(T5577_OPCODE_RESET, NULL, 0, NULL, 0);
}

/**
 * @brief Reset the password function to set the card that is used to set the existing known password into a target password
 *
 * @param old_passwd current card password (32bits)
 * @param new_passwd target card password (32bits)
 */
void t55xx_reset_passwd(uint32_t old_passwd, uint32_t new_passwd) {
    t55xx_send_cmd(T5577_OPCODE_PAGE0, &old_passwd, 0, &new_passwd, 7);  // 0 area 7 blocks to write new passwords (passwords)
    t55xx_send_cmd(T5577_OPCODE_PAGE0, &old_passwd, 0, &new_passwd, 7);  // 0 area 7 blocks to write new passwords (passwords)
    t55xx_send_cmd(T5577_OPCODE_RESET, NULL, 0, NULL, 0);
}
