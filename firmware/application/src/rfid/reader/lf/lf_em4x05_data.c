#include "lf_em4x05_data.h"

#include <stdlib.h>
#include <string.h>

#include "app_status.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "circular_buffer.h"
#include "lf_125khz_radio.h"
#include "lf_gap.h"
#include "lf_reader_data.h"
#include "timeslot.h"

#include "utils/manchester.h"

#define NRF_LOG_MODULE_NAME lf_em4x05
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define EM4X05_CMD_BITS      9
#define EM4X05_RESP_BITS     45
#define EM4X05_ROWS          8
#define EM4X05_COLS          4
#define EM4X05_CB_SIZE       256

static inline uint8_t odd_parity4(uint8_t nibble) {
    nibble ^= nibble >> 2;
    nibble ^= nibble >> 1;
    return (~nibble) & 1;
}

static uint8_t em4x05_cmd_parity(uint8_t opcode, uint8_t addr) {
    uint8_t o1 = (opcode >> 1) & 1;
    uint8_t o0 = (opcode)      & 1;
    uint8_t a2 = (addr >> 2)   & 1;
    uint8_t a1 = (addr >> 1)   & 1;
    uint8_t a0 = (addr)        & 1;
    uint8_t p2 = (~(o1 ^ o0 ^ a2)) & 1;
    uint8_t p1 = (~(o1 ^ a1 ^ a0)) & 1;
    uint8_t p0 = (~(o0 ^ a2 ^ a1)) & 1;
    return (p2 << 2) | (p1 << 1) | p0;
}

static uint16_t em4x05_build_cmd(uint8_t opcode, uint8_t addr) {
    uint8_t parity = em4x05_cmd_parity(opcode, addr);
    return (1u << 8) | ((opcode & 0x3) << 6) | ((addr & 0x7) << 3) | (parity & 0x7);
}

static bool em4x05_decode_response(const uint8_t *bits, uint32_t *data) {
    if (bits[0] != 0) {
        return false;
    }
    uint32_t result = 0;
    uint8_t col_parity[EM4X05_COLS] = {0};
    for (int row = 0; row < EM4X05_ROWS; row++) {
        int base = 1 + row * (EM4X05_COLS + 1);
        uint8_t nibble = 0;
        for (int col = 0; col < EM4X05_COLS; col++) {
            uint8_t b = bits[base + col] & 1;
            nibble = (nibble << 1) | b;
            col_parity[col] ^= b;
        }
        uint8_t rp = bits[base + EM4X05_COLS] & 1;
        if (rp != odd_parity4(nibble)) {
            NRF_LOG_DEBUG("em4x05: row %d parity fail", row);
            return false;
        }
        result = (result << EM4X05_COLS) | nibble;
    }
    int cp_base = 1 + EM4X05_ROWS * (EM4X05_COLS + 1);
    for (int col = 0; col < EM4X05_COLS; col++) {
        uint8_t received_cp = bits[cp_base + col] & 1;
        if (received_cp != ((~col_parity[col]) & 1)) {
            NRF_LOG_DEBUG("em4x05: col %d parity fail", col);
            return false;
        }
    }
    *data = result;
    return true;
}

#define EM4X05_T1   0x40u
#define EM4X05_T15  0x60u
#define EM4X05_T2   0x80u
#define EM4X05_JIT  0x10u

static uint8_t em4x05_rf64_period(uint8_t interval) {
    if (interval >= (EM4X05_T1  - EM4X05_JIT) && interval <= (EM4X05_T1  + EM4X05_JIT)) return 0;
    if (interval >= (EM4X05_T15 - EM4X05_JIT) && interval <= (EM4X05_T15 + EM4X05_JIT)) return 1;
    if (interval >= (EM4X05_T2  - EM4X05_JIT) && interval <= (EM4X05_T2  + EM4X05_JIT)) return 2;
    return 3;
}

static circular_buffer g_cb;

static void em4x05_edge_cb(void) {
    uint32_t cnt = get_lf_counter_value();
    uint16_t val = (cnt > 0xff) ? 0xff : (uint16_t)(cnt & 0xff);
    cb_push_back(&g_cb, &val);
    clear_lf_counter_value();
}

static uint8_t  g_send_opcode;
static uint8_t  g_send_addr;
static uint32_t g_send_password;
static volatile bool g_timeslot_done = false;

/*
 * Send one EM4305 command bit.
 * Protocol: field ON for bit duration, then write gap (field OFF).
 * The write gap delay is padded to compensate for antenna ringing (~200us).
 * Field is left ON after the gap ready for the next bit or response window.
 */
static void send_em4305_bit(bool bit) {
    if (bit) {
        bsp_delay_us(256);   /* bit 1: 32 Tc = 256us */
    } else {
        bsp_delay_us(184);   /* bit 0: 23 Tc = 184us */
    }
    stop_lf_125khz_radio();
    bsp_delay_us(250);       /* write gap: 128us target + ~122us ringing compensation */
    start_lf_125khz_radio();
}

static void em4x05_send_timeslot_cb(void) {
    /* 1. Start gap: wake up tag */
    stop_lf_125khz_radio();
    bsp_delay_us(440);           /* 55 Tc = 440us */

    /* 2. Settle: allow tag clock recovery to lock onto carrier */
    start_lf_125khz_radio();
    bsp_delay_us(104);           /* 13 carrier cycles = 104us */

    /* 3. Send 9-bit command MSB first */
    uint16_t cmd = em4x05_build_cmd(g_send_opcode, g_send_addr);
    for (int i = 8; i >= 0; i--) {
        send_em4305_bit((cmd >> i) & 1);
    }

    /* 4. Field stays ON (left by last start_lf in send_em4305_bit)
     * Tag will respond ~3 Tc (~24us) after the last write gap */
    g_timeslot_done = true;
}

static void em4x05_build_data_word(uint32_t data, uint8_t bits[45]) {
    uint8_t col_par[4] = {0};
    int pos = 0;
    bits[pos++] = 0;
    for (int row = 0; row < 8; row++) {
        uint8_t nibble = (data >> (28 - row * 4)) & 0xF;
        uint8_t rp = 0;
        for (int col = 0; col < 4; col++) {
            uint8_t b = (nibble >> (3 - col)) & 1;
            bits[pos++] = b;
            col_par[col] ^= b;
            rp ^= b;
        }
        bits[pos++] = (~rp) & 1;
    }
    for (int col = 0; col < 4; col++) {
        bits[pos++] = (~col_par[col]) & 1;
    }
}

static void em4x05_login_timeslot_cb(void) {
    /* Start gap */
    stop_lf_125khz_radio();
    bsp_delay_us(440);
    start_lf_125khz_radio();
    bsp_delay_us(104);

    /* LOGIN command: opcode=0b00 (DSBL), addr=0b000 */
    uint16_t cmd = em4x05_build_cmd(EM4X05_OPCODE_DSBL, 0);
    for (int i = 8; i >= 0; i--) {
        send_em4305_bit((cmd >> i) & 1);
    }

    /* Send 45-bit password word using same bit encoding */
    uint8_t pwd_bits[45];
    em4x05_build_data_word(g_send_password, pwd_bits);
    for (int i = 0; i < 45; i++) {
        send_em4305_bit(pwd_bits[i]);
    }

    g_timeslot_done = true;
}

static bool em4x05_login(uint32_t password, uint32_t timeout_ms) {
    g_send_password = password;
    g_timeslot_done = false;

    request_timeslot(15000, em4x05_login_timeslot_cb);

    autotimer *p_wait = bsp_obtain_timer(0);
    while (!g_timeslot_done && NO_TIMEOUT_1MS(p_wait, 20)) {}
    bsp_return_timer(p_wait);

    cb_init(&g_cb, EM4X05_CB_SIZE, sizeof(uint16_t));
    register_rio_callback(em4x05_edge_cb);
    lf_125khz_radio_gpiote_enable();
    clear_lf_counter_value();

    bool ack = false;
    autotimer *p_at = bsp_obtain_timer(0);
    while (!ack && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t interval = 0;
        if (!cb_pop_front(&g_cb, &interval)) {
            continue;
        }
        uint8_t period = em4x05_rf64_period((uint8_t)interval);
        if (period <= 2) {
            ack = true;
        }
    }
    bsp_return_timer(p_at);
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
    cb_free(&g_cb);
    return ack;
}

static bool em4x05_read_block(uint8_t addr, uint32_t *data, uint32_t timeout_ms) {
    g_send_opcode   = EM4X05_OPCODE_READ;
    g_send_addr     = addr;
    g_timeslot_done = false;

    /*
     * Timeslot must cover full command transmission:
     *   start_gap(440) + settle(104) + 9 bits * (256+250) = 5098us
     * Use 6000us for margin.
     */
    request_timeslot(6000, em4x05_send_timeslot_cb);

    autotimer *p_wait = bsp_obtain_timer(0);
    while (!g_timeslot_done && NO_TIMEOUT_1MS(p_wait, 10)) {}
    bsp_return_timer(p_wait);

    cb_init(&g_cb, EM4X05_CB_SIZE, sizeof(uint16_t));
    register_rio_callback(em4x05_edge_cb);
    lf_125khz_radio_gpiote_enable();
    clear_lf_counter_value();

    manchester modem = {
        .sync = true,
        .rp   = em4x05_rf64_period,
    };
    uint8_t resp_bits[EM4X05_RESP_BITS] = {0};
    uint8_t bit_count = 0;
    bool ok = false;

    autotimer *p_at = bsp_obtain_timer(0);
    while (!ok && NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        uint16_t interval = 0;
        if (!cb_pop_front(&g_cb, &interval)) {
            continue;
        }
        bool mbits[2] = {false, false};
        int8_t mbitlen = 0;
        manchester_feed(&modem, (uint8_t)interval, mbits, &mbitlen);
        if (mbitlen == -1) {
            manchester_reset(&modem);
            bit_count = 0;
            continue;
        }
        for (int8_t i = 0; i < mbitlen && bit_count < EM4X05_RESP_BITS; i++) {
            resp_bits[bit_count++] = mbits[i] ? 1 : 0;
        }
        if (bit_count >= EM4X05_RESP_BITS) {
            ok = em4x05_decode_response(resp_bits, data);
            if (!ok) {
                memmove(resp_bits, resp_bits + 1, EM4X05_RESP_BITS - 1);
                bit_count = EM4X05_RESP_BITS - 1;
            }
        }
    }
    bsp_return_timer(p_at);
    lf_125khz_radio_gpiote_disable();
    unregister_rio_callback();
    cb_free(&g_cb);
    return ok;
}

bool em4x05_read(em4x05_data_t *out, uint32_t timeout_ms) {
    memset(out, 0, sizeof(*out));

    uint32_t block_timeout = timeout_ms / 4;
    if (block_timeout < 100) block_timeout = 100;

    if (!em4x05_read_block(EM4X05_BLOCK_CONFIG, &out->config, block_timeout)) {
        NRF_LOG_DEBUG("em4x05: block 0 read failed");
        return false;
    }

    if (out->config == 0x00000000 || out->config == 0xFFFFFFFF) {
        NRF_LOG_DEBUG("em4x05: invalid config word 0x%08X", out->config);
        return false;
    }

    bool rl = (out->config >> 6) & 1;
    if (rl) {
        NRF_LOG_DEBUG("em4x05: RL set, attempting login pwd=%08X", out->password);
        if (!em4x05_login(out->password, block_timeout)) {
            NRF_LOG_DEBUG("em4x05: login failed");
            out->login_required = true;
            return false;
        }
        out->login_required = false;
        NRF_LOG_DEBUG("em4x05: login OK");
    }

    uint8_t lwr = (out->config >> 16) & 0xF;
    uint8_t uid_block = (lwr >= 1 && lwr < 14) ? lwr : EM4X05_BLOCK_UID;

    if (!em4x05_read_block(uid_block, &out->uid, block_timeout)) {
        NRF_LOG_DEBUG("em4x05: UID block %d read failed", uid_block);
        return false;
    }
    out->uid_block = uid_block;

    uint32_t uid_lo = 0, uid_hi = 0;
    if (em4x05_read_block(EM4X69_BLOCK_UID_LO, &uid_lo, block_timeout) &&
            em4x05_read_block(EM4X69_BLOCK_UID_HI, &uid_hi, block_timeout)) {
        out->uid_hi    = uid_hi;
        out->uid       = uid_lo;
        out->is_em4x69 = true;
    }

    return true;
}

uint8_t scan_em4x05(em4x05_data_t *out) {
    start_lf_125khz_radio();
    bsp_delay_ms(5);

    bool found = em4x05_read(out, 1000);

    stop_lf_125khz_radio();

    if (!found && out->login_required) {
        return STATUS_LF_TAG_LOGIN_REQUIRED;
    }
    return found ? STATUS_LF_TAG_OK : STATUS_LF_TAG_NO_FOUND;
}

/* -----------------------------------------------------------------------
 * WRITE support
 *
 * UNVERIFIED ON HARDWARE.  The bit-level primitives below (send_em4305_bit,
 * em4x05_build_data_word, the timeslot structure) are the same ones the
 * read/login path uses and are known good for *sending*.  What has never
 * been exercised is programming an EEPROM block: em4x05_build_data_word is
 * currently only used to encode the LOGIN password, which the tag checks
 * but does not store.  Two things must be confirmed against the EM4305
 * datasheet and a real tag before trusting this:
 *   1. T_PROG_US below -- the field-on wait while the tag burns the block.
 *      The value here is a documented-typical placeholder, NOT measured.
 *   2. Whether a write must be followed by a read-back to confirm, since
 *      WRITE returns no response word to ACK on.
 * --------------------------------------------------------------------- */

/*
 * EM4305 programming time after the data word, field left ON.
 *
 * PM3 value: tPC + tWEE = 10820us (armsrc/lfops.c EM4xWriteWord, the
 * commented "WaitUS(10820)").  PM3 itself no longer blind-waits -- it samples
 * the tag's response preamble instead, because a denied write returns an
 * error preamble much sooner than a successful one completes.  We blind-wait
 * for now; TODO: replace with response sampling using the existing read-side
 * modem (em4x05_edge_cb + em4x05_decode_response) to confirm success and to
 * detect write-protect denial.  Protect-word timing, if ever added, is
 * tPC + tPR = 13640us.
 */
#define EM4X05_T_PROG_US   10820

/* EM4305 config register is physical block 4 (EM_CONFIG_BLOCK in PM3);
 * data blocks for a 4-block payload are 5,6,7,8.  This is distinct from the
 * read-side EM4X05_BLOCK_CONFIG(0) constant, which is where scan reads the
 * config word back from. */
#define EM4X05_WRITE_CONFIG_BLOCK   4
#define EM4X05_WRITE_DATA_BLOCK0    5

static uint8_t  g_write_addr;
static uint32_t g_write_data;

/* Reverse the 32 bits of x.  EM4305 stores data words in the opposite bit
 * order to how the FDX-B frame packs them; PM3's em4x05_clone_tag applies
 * exactly this to every data block (but NOT to the config word). */
static uint32_t em4x05_reflect32(uint32_t x) {
    x = ((x & 0x55555555U) << 1) | ((x & 0xAAAAAAAAU) >> 1);
    x = ((x & 0x33333333U) << 2) | ((x & 0xCCCCCCCCU) >> 2);
    x = ((x & 0x0F0F0F0FU) << 4) | ((x & 0xF0F0F0F0U) >> 4);
    x = ((x & 0x00FF00FFU) << 8) | ((x & 0xFF00FF00U) >> 8);
    return (x << 16) | (x >> 16);
}

static void em4x05_write_timeslot_cb(void) {
    /* Start gap + settle, identical to em4x05_send_timeslot_cb. */
    stop_lf_125khz_radio();
    bsp_delay_us(440);
    start_lf_125khz_radio();
    bsp_delay_us(104);

    /* 9-bit WRITE command, MSB first. */
    uint16_t cmd = em4x05_build_cmd(EM4X05_OPCODE_WRITE, g_write_addr);
    for (int i = 8; i >= 0; i--) {
        send_em4305_bit((cmd >> i) & 1);
    }

    /* 45-bit data word (leading 0 + 8*(nibble+row parity) + 4 column parity). */
    uint8_t data_bits[45];
    em4x05_build_data_word(g_write_data, data_bits);
    for (int i = 0; i < 45; i++) {
        send_em4305_bit(data_bits[i]);
    }

    /* Programming pause: field stays ON while the tag burns the block. */
    bsp_delay_us(EM4X05_T_PROG_US);

    g_timeslot_done = true;
}

static bool em4x05_write_block(uint8_t addr, uint32_t data) {
    g_write_addr    = addr;
    g_write_data    = data;
    g_timeslot_done = false;

    /* start_gap(440)+settle(104)+54 bits*(256+250)+T_prog(10820) ~= 38.7ms.
     * Budget 45ms for margin; wait loop below covers 50ms. */
    request_timeslot(45000, em4x05_write_timeslot_cb);

    autotimer *p_wait = bsp_obtain_timer(0);
    while (!g_timeslot_done && NO_TIMEOUT_1MS(p_wait, 50)) {}
    bsp_return_timer(p_wait);

    return g_timeslot_done;
}

/*
 * Write an FDX-B frame to an EM4305/4469 tag.
 *
 * Config word 0x0002008F = EM4x05_SET_BITRATE(32) | MODULATION_BIPHASE |
 * SET_NUM_BLOCKS(4), i.e. the numeric value of PM3's EM4305_FDXB_CONFIG_BLOCK.
 *
 * Blocks 5-8 carry the same four 32-bit words the T5577 writer produces
 * (blocks 1-4 there): fdxb_t55xx_writer has already packed the PM3-identical
 * frame bits.  Only the config word and the target chip differ.
 *
 * @param blks: blks[1..4] = the four frame words from fdxb_t55xx_writer
 * @param password: login password, or 0 if none required
 * @param needs_login: issue LOGIN before writing (RL/WL protected tag)
 */
uint8_t write_fdxb_to_em4305(uint32_t *blks, uint32_t password, bool needs_login) {
    start_lf_125khz_radio();
    bsp_delay_ms(5);

    if (needs_login) {
        if (!em4x05_login(password, 1000)) {
            stop_lf_125khz_radio();
            return STATUS_LF_TAG_NO_FOUND;
        }
    }

    /*
     * Data payload -> blocks 5-8, each bit-reflected (EM4305 data-word bit
     * order is the reverse of the frame packing; PM3 does the same).
     * Config word -> block 4, written WITHOUT reflection and LAST, so the
     * tag only begins modulating once the four data blocks are in place.
     */
    bool ok = true;
    ok = ok && em4x05_write_block(EM4X05_WRITE_DATA_BLOCK0 + 0, em4x05_reflect32(blks[1]));
    ok = ok && em4x05_write_block(EM4X05_WRITE_DATA_BLOCK0 + 1, em4x05_reflect32(blks[2]));
    ok = ok && em4x05_write_block(EM4X05_WRITE_DATA_BLOCK0 + 2, em4x05_reflect32(blks[3]));
    ok = ok && em4x05_write_block(EM4X05_WRITE_DATA_BLOCK0 + 3, em4x05_reflect32(blks[4]));
    ok = ok && em4x05_write_block(EM4X05_WRITE_CONFIG_BLOCK, 0x0002008F);

    stop_lf_125khz_radio();
    return ok ? STATUS_LF_TAG_OK : STATUS_LF_TAG_NO_FOUND;
}
