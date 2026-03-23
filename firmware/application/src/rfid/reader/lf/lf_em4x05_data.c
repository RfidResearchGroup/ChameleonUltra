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

/* -----------------------------------------------------------------------
 * Internal constants
 * --------------------------------------------------------------------- */

#define EM4X05_CMD_BITS      9
#define EM4X05_RESP_BITS     45
#define EM4X05_ROWS          8
#define EM4X05_COLS          4
#define EM4X05_CB_SIZE       256

/* -----------------------------------------------------------------------
 * Parity helpers
 * --------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * Response decoding
 * --------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * RF/64 Manchester period classifier
 * --------------------------------------------------------------------- */

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

/* -----------------------------------------------------------------------
 * Edge-capture
 * --------------------------------------------------------------------- */

static circular_buffer g_cb;

static void em4x05_edge_cb(void) {
    uint32_t cnt = get_lf_counter_value();
    uint16_t val = (cnt > 0xff) ? 0xff : (uint16_t)(cnt & 0xff);
    cb_push_back(&g_cb, &val);
    clear_lf_counter_value();
}

/* -----------------------------------------------------------------------
 * Timeslot callbacks â send only, no receive here
 * --------------------------------------------------------------------- */

static uint8_t  g_send_opcode;
static uint8_t  g_send_addr;
static uint32_t g_send_password;
static volatile bool g_timeslot_done = false;

void send_em4305_bit(bool bit) {
    // 1. HARD GAP: Increase to 140us to ensure the field hits zero
    stop_lf_125khz_radio();
    bsp_delay_us(140); 

    // 2. DATA PULSE:
    start_lf_125khz_radio();
    if (bit) {
        // Logic 1: Aiming for ~300us ON time
        bsp_delay_us(300); 
    } else {
        // Logic 0: Aiming for ~100us ON time
        bsp_delay_us(100); 
    }
}

static void em4x05_send_timeslot_cb(void) {
    // 1. Start Gap: 480us of SILENCE to wake up the tag logic
    stop_lf_125khz_radio();
    bsp_delay_us(480);
    
    // Field must be ON briefly before the first bit's write hole
    start_lf_125khz_radio();
    bsp_delay_us(10); // Tiny "settle" time

    // 2. Build the command (Opcode + Addr + Parity)
    uint16_t cmd = em4x05_build_cmd(g_send_opcode, g_send_addr);
    
    // 3. Send the 9 bits (MSB first)
    for (int i = 8; i >= 0; i--) {
        send_em4305_bit((cmd >> i) & 1);
    }

    // 4. Ensure field stays ON so the tag has power to reply!
    start_lf_125khz_radio();
    g_timeslot_done = true;
}

/* -----------------------------------------------------------------------
 * Password data word builder
 * --------------------------------------------------------------------- */

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
    lf_gap_send_start();
    bsp_delay_us(GAP_LISTEN_US);
    uint16_t cmd = em4x05_build_cmd(EM4X05_OPCODE_DSBL, 0);
    lf_gap_send_bits(cmd, EM4X05_CMD_BITS);
    uint8_t pwd_bits[45];
    em4x05_build_data_word(g_send_password, pwd_bits);
    for (int i = 0; i < 45; i++) {
        lf_gap_send_bit(pwd_bits[i]);
    }
    g_timeslot_done = true;
}

/* -----------------------------------------------------------------------
 * LOGIN
 * --------------------------------------------------------------------- */

static bool em4x05_login(uint32_t password, uint32_t timeout_ms) {
    g_send_password = password;
    g_timeslot_done = false;

    request_timeslot(10000, em4x05_login_timeslot_cb);

    /* Wait for timeslot to complete */
    autotimer *p_wait = bsp_obtain_timer(0);
    while (!g_timeslot_done && NO_TIMEOUT_1MS(p_wait, 15)) {}
    bsp_return_timer(p_wait);

    /* Now set up edge capture to receive ACK */
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

/* -----------------------------------------------------------------------
 * Block read
 * Key insight from T5577: send command in timeslot, then receive AFTER
 * --------------------------------------------------------------------- */

static bool em4x05_read_block(uint8_t addr, uint32_t *data, uint32_t timeout_ms) {
    g_send_opcode = EM4X05_OPCODE_READ;
    g_send_addr   = addr;
    g_timeslot_done = false;

    /*
     * Send READ command via timeslot.
     * Timeslot duration must cover:
     *   start_gap(55*8=440us) + listen(50*8=400us) + 9 bits*(32+16)*8us = 3296us
     * Use 5000us for margin.
     */
    request_timeslot(5000, em4x05_send_timeslot_cb);

    /* Wait for timeslot to complete before setting up receive */
    autotimer *p_wait = bsp_obtain_timer(0);
    while (!g_timeslot_done && NO_TIMEOUT_1MS(p_wait, 10)) {}
    bsp_return_timer(p_wait);

    /*
     * Now set up edge capture. The tag starts responding ~3Tc (~24us)
     * after the last command bit. We have a small window here but the
     * circular buffer will absorb any edges we catch.
     */
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

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

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
