#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "hex_utils.h"
#include "lf_125khz_radio.h"
#include "lf_reader_data.h"
#include "nrf_gpio.h"
#include "protocols/t55xx.h"
#include "timeslot.h"

#include "utils/manchester.h"

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

// ---------------------------------------------------------------------------
// T55xx block read — reuses the proven em410x edge-capture front-end
// (register_rio_callback + gpiote + LF counter), same as em410x_read().
//
// Instrumented: mode 1 dumps the raw edge intervals (carrier cycles between
// falling edges) so the signal can be inspected directly; mode 0 feeds the
// manchester modem; mode 2 captures the raw SAADC envelope amplitude (the
// robust path for dense data — immune to the comparator missing weak
// transitions). downlink=1 sends the addressed read command first;
// downlink=0 captures the free-running regular-read stream (blocks 1..maxblock).
//
// Manchester only for now. Covers default/wipe (0x000880E0, RF/32), em410x
// (RF/64), viking (RF/32).
// ---------------------------------------------------------------------------

#define T55XX_CB_SIZE 256

static circular_buffer t55xx_g_cb;
static uint8_t t55xx_g_rf_n = 32;

static void t55xx_edge_cb(void) {
    uint32_t cnt = get_lf_counter_value();
    uint16_t val = (cnt > 0xff) ? 0xff : (uint16_t)(cnt & 0xff);
    cb_push_back(&t55xx_g_cb, &val);
    clear_lf_counter_value();
}

/*
 * Bitrate-scaled Manchester cell classifier. Derived from em4x05's RF/64
 * table (T1=0x40, T15=0x60, T2=0x80, JIT=0x10) by the ratio rf_n/64.
 */
static uint8_t t55xx_manch_period(uint8_t iv) {
    uint16_t t1  = t55xx_g_rf_n;
    uint16_t t15 = (uint16_t)t55xx_g_rf_n * 3 / 2;
    uint16_t t2  = (uint16_t)t55xx_g_rf_n * 2;
    uint16_t jit = t55xx_g_rf_n / 4;
    if (iv >= t1  - jit && iv <= t1  + jit) return 0;
    if (iv >= t15 - jit && iv <= t15 + jit) return 1;
    if (iv >= t2  - jit && iv <= t2  + jit) return 2;
    return 3;
}

/**
 * @brief Capture from a T55xx.
 *
 * @param rf_n       bitrate divisor (0 => 32); demod mode only
 * @param mode       0 = Manchester-demod bits, 1 = raw edge intervals,
 *                   2 = raw SAADC envelope amplitude (8-bit/sample)
 * @param downlink   1 = send addressed read command first; 0 = regular read
 * @param use_passwd password-protected read (downlink only)
 * @param passwd     32-bit password
 * @param block      block number
 * @param page1      target page 1
 * @param out        caller buffer: intervals/amplitude bytes, or 1-bit-per-byte
 * @param max_out    capacity of out
 * @param timeout_ms capture window
 * @return number of items written (bits for demod, else bytes)
 */
uint16_t t55xx_read(uint8_t rf_n, uint8_t mode, uint8_t downlink,
                    uint8_t use_passwd, uint32_t passwd,
                    uint8_t block, uint8_t page1,
                    uint8_t *out, uint16_t max_out, uint32_t timeout_ms) {
    t55xx_g_rf_n = rf_n ? rf_n : 32;

    uint8_t   opcode  = page1 ? T5577_OPCODE_PAGE1 : T5577_OPCODE_PAGE0;
    uint32_t *pwd_ptr = use_passwd ? &passwd : NULL;

    /* Mode 2: SAADC envelope amplitude — reuses the proven raw_read_to_buffer
     * sampler (immune to comparator miss-triggering on dense data), but with
     * the field held on after the addressed downlink instead of self-managed. */
    if (mode == 2) {
        start_lf_125khz_radio();
        bsp_delay_ms(2);
        if (downlink) {
            t55xx_send_cmd(opcode, pwd_ptr, 0, NULL, block);  /* field stays on */
        }
        size_t outlen = 0;
        raw_read_to_buffer_ex(out, max_out, timeout_ms, &outlen, false);
        stop_lf_125khz_radio();
        return (uint16_t)outlen;
    }

    /* Modes 0/1: edge front-end. Match em410x_read ordering — hook the edge
     * front-end BEFORE the field. */
    cb_init(&t55xx_g_cb, T55XX_CB_SIZE, sizeof(uint16_t));
    register_rio_callback(t55xx_edge_cb);
    lf_125khz_radio_gpiote_enable();
    start_lf_125khz_radio();
    bsp_delay_ms(2);  /* antenna settle, like raw_read_to_buffer */

    if (downlink) {
        /* data=NULL, lock_bit=0 => READ downlink; field left on, no RESET. */
        t55xx_send_cmd(opcode, pwd_ptr, 0, NULL, block);
    }
    clear_lf_counter_value();

    uint16_t n = 0;
    autotimer *p_at = bsp_obtain_timer(0);

    if (mode == 1) {
        while (n < max_out && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
            uint16_t iv = 0;
            if (!cb_pop_front(&t55xx_g_cb, &iv)) {
                continue;
            }
            out[n++] = (uint8_t)(iv & 0xff);
        }
    } else {
        manchester modem = {
            .sync = true,
            .rp   = t55xx_manch_period,
        };
        while (n < max_out && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
            uint16_t iv = 0;
            if (!cb_pop_front(&t55xx_g_cb, &iv)) {
                continue;
            }
            bool mbits[2] = {false, false};
            int8_t mlen = 0;
            manchester_feed(&modem, (uint8_t)iv, mbits, &mlen);
            if (mlen == -1) {
                manchester_reset(&modem);  /* resync only; keep the run intact */
                continue;
            }
            for (int8_t i = 0; i < mlen && n < max_out; i++) {
                out[n++] = mbits[i] ? 1 : 0;
            }
        }
    }

    bsp_return_timer(p_at);
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
    cb_free(&t55xx_g_cb);
    stop_lf_125khz_radio();
    return n;
}
