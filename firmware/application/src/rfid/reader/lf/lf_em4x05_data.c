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

/*
 * Command word structure (9 bits, sent MSB-first after start gap):
 *   bit 8:    start bit (always 1)
 *   bits 7-6: opcode (2 bits)
 *   bits 5-3: block address (3 bits)
 *   bits 2-0: odd parity of the 6 data bits (3 bits, one per pair)
 *
 * The parity scheme: three parity bits p[2:0] where
 *   p[2] = parity(opcode[1], opcode[0], addr[2])
 *   p[1] = parity(opcode[1], addr[1],   addr[0])  -- see datasheet §6.1
 *   p[0] = parity(opcode[0], addr[2],   addr[1])
 * Each is odd parity over the three bits.
 *
 * Response word (45 bits):
 *   bit 44:     header (always 0 per datasheet — marks start)
 *   bits 43-12: data (32 bits, MSB first)
 *   bits 11-8:  column parity (4 bits)
 *   bit 7:      stop bit (always 0)
 *   bits 6-3:   row parity (4 bits)  [actually these are col parity bits 3-0]
 *   bit 2-1:    stop bits
 *   bit 0:      trailer
 *
 * Simplified: we read 45 bits, extract bits [43:12] as data, validate the
 * 4-bit column parity over the 32 data bits (8 nibbles × 4 bits each), and
 * validate the row parity bits.
 *
 * Parity (EM4x05 datasheet §5, same layout as EM4100):
 *   The 32 data bits are arranged as 8 rows × 4 columns.
 *   Row parity: one odd parity bit per row (appended after each row).
 *   Column parity: one odd parity bit per column (4 bits total, at the end).
 *
 * In the 45-bit response the layout is:
 *   [0]       header
 *   [1..4]    row 0 data nibble
 *   [5]       row 0 parity
 *   [6..9]    row 1 data nibble
 *   [10]      row 1 parity
 *   ...
 *   [36..39]  row 7 data nibble
 *   [40]      row 7 parity
 *   [41..44]  column parity nibble
 */

#define EM4X05_CMD_BITS      9
#define EM4X05_RESP_BITS     45
#define EM4X05_ROWS          8
#define EM4X05_COLS          4

#define EM4X05_CB_SIZE       256   /* circular buffer capacity (intervals) */

/* -----------------------------------------------------------------------
 * Parity helpers
 * --------------------------------------------------------------------- */

/* Odd parity of a byte (1 if odd number of set bits, 0 if even) */
static inline uint8_t odd_parity4(uint8_t nibble) {
    nibble ^= nibble >> 2;
    nibble ^= nibble >> 1;
    return (~nibble) & 1;
}

/* Build the 3-bit command parity field per EM4x05 datasheet §6.1 */
static uint8_t em4x05_cmd_parity(uint8_t opcode, uint8_t addr) {
    uint8_t o1 = (opcode >> 1) & 1;
    uint8_t o0 = (opcode)      & 1;
    uint8_t a2 = (addr >> 2)   & 1;
    uint8_t a1 = (addr >> 1)   & 1;
    uint8_t a0 = (addr)        & 1;

    uint8_t p2 = (~(o1 ^ o0 ^ a2)) & 1;  /* odd parity of o1,o0,a2 */
    uint8_t p1 = (~(o1 ^ a1 ^ a0)) & 1;
    uint8_t p0 = (~(o0 ^ a2 ^ a1)) & 1;

    return (p2 << 2) | (p1 << 1) | p0;
}

/* Build the full 9-bit command word */
static uint16_t em4x05_build_cmd(uint8_t opcode, uint8_t addr) {
    uint8_t parity = em4x05_cmd_parity(opcode, addr);
    /* [start=1][opcode 2b][addr 3b][parity 3b] */
    return (1u << 8) | ((opcode & 0x3) << 6) | ((addr & 0x7) << 3) | (parity & 0x7);
}

/* -----------------------------------------------------------------------
 * Response decoding
 * --------------------------------------------------------------------- */

/*
 * Decode a 45-bit response word.
 *
 * bits[] must contain exactly EM4X05_RESP_BITS bits in order of reception
 * (bit 0 = first received = header bit).
 *
 * Returns true and writes *data if parity checks pass.
 */
static bool em4x05_decode_response(const uint8_t *bits, uint32_t *data) {
    /* bit[0] = header, should be 0 (tag sets it to 0 before data) */
    /* Not strictly required for decoding but sanity-check it */
    if (bits[0] != 0) {
        return false;
    }

    uint32_t result = 0;
    uint8_t col_parity[EM4X05_COLS] = {0};

    for (int row = 0; row < EM4X05_ROWS; row++) {
        int base = 1 + row * (EM4X05_COLS + 1);  /* +1 for parity bit */
        uint8_t nibble = 0;
        for (int col = 0; col < EM4X05_COLS; col++) {
            uint8_t b = bits[base + col] & 1;
            nibble = (nibble << 1) | b;
            col_parity[col] ^= b;
        }
        /* Row parity check */
        uint8_t rp = bits[base + EM4X05_COLS] & 1;
        if (rp != odd_parity4(nibble)) {
            NRF_LOG_DEBUG("em4x05: row %d parity fail", row);
            return false;
        }
        result = (result << EM4X05_COLS) | nibble;
    }

    /* Column parity check: bits [41..44] */
    int cp_base = 1 + EM4X05_ROWS * (EM4X05_COLS + 1);
    for (int col = 0; col < EM4X05_COLS; col++) {
        uint8_t received_cp = bits[cp_base + col] & 1;
        /* col_parity[col] is XOR of all data bits in that column;
         * odd parity means it should equal 1 when col_parity is even */
        if (received_cp != ((~col_parity[col]) & 1)) {
            NRF_LOG_DEBUG("em4x05: col %d parity fail", col);
            return false;
        }
    }

    *data = result;
    return true;
}

/* -----------------------------------------------------------------------
 * Timeslot command send
 * --------------------------------------------------------------------- */

static uint8_t g_send_opcode;
static uint8_t g_send_addr;

static void em4x05_send_timeslot_cb(void) {
    lf_gap_send_start();
    /* Allow tag to power up fully after start gap */
    bsp_delay_us(GAP_LISTEN_US);
    /* Send 9-bit command MSB-first */
    uint16_t cmd = em4x05_build_cmd(g_send_opcode, g_send_addr);
    lf_gap_send_bits(cmd, EM4X05_CMD_BITS);
    /*
     * Leave carrier on — tag will begin responding after ~3Tc.
     * The receive loop in em4x05_read_block() takes over from here.
     */
}

/* -----------------------------------------------------------------------
 * Edge-capture receive loop
 * --------------------------------------------------------------------- */

static circular_buffer g_cb;

static void em4x05_edge_cb(void) {
    uint32_t cnt = get_lf_counter_value();
    uint16_t val = (cnt > 0xff) ? 0xff : (uint16_t)(cnt & 0xff);
    cb_push_back(&g_cb, &val);
    clear_lf_counter_value();
}

/*
 * Read one block from the tag.
 *
 * Sends the READ command via a timeslot, then switches to edge-capture
 * receive mode and runs the Manchester decoder until 45 bits are collected
 * or the timeout expires.
 *
 * @param addr        Block address (0–15).
 * @param data        Output: decoded 32-bit block value.
 * @param timeout_ms  Overall timeout for the receive phase.
 * @return            true on success.
 */
/*
 * RF/64 Manchester period classifier for EM4x05.
 * 1T=64Tc, 1.5T=96Tc, 2T=128Tc, jitter=±16Tc.
 */
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

static bool em4x05_read_block(uint8_t addr, uint32_t *data, uint32_t timeout_ms) {
    manchester modem = {
        .sync = true,
        .rp   = em4x05_rf64_period,
    };

    uint8_t resp_bits[EM4X05_RESP_BITS] = {0};
    uint8_t bit_count = 0;

    /* Stage 1: send command in timeslot */
    g_send_opcode = EM4X05_OPCODE_READ;
    g_send_addr   = addr;

    /*
     * The timeslot duration needs to cover the command transmission:
     * start_gap(50) + listen(50) + 9 bits × (56+10)µs ≈ 1294µs.
     * Request 2ms to be safe.
     */
    request_timeslot(2000, em4x05_send_timeslot_cb);

    /* Give the timeslot time to complete */
    bsp_delay_ms(3);

    /* Stage 2: receive — switch to edge-capture mode */
    cb_init(&g_cb, EM4X05_CB_SIZE, sizeof(uint16_t));
    register_rio_callback(em4x05_edge_cb);
    lf_125khz_radio_gpiote_enable();
    clear_lf_counter_value();

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
            /* Desync — reset and keep trying */
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
                /*
                 * Parity failed — could be a framing alignment issue.
                 * Slide the window by discarding the oldest bit and
                 * continuing accumulation.
                 */
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

    /*
     * Read block 0 (config) and block 15 (UID).
     * For EM4x69 also attempt blocks 13 and 14 (64-bit UID).
     *
     * We split the timeout evenly across up to 4 block reads.
     */
    uint32_t block_timeout = timeout_ms / 4;
    if (block_timeout < 50) block_timeout = 50;

    /* Block 0: configuration */
    if (!em4x05_read_block(EM4X05_BLOCK_CONFIG, &out->config, block_timeout)) {
        NRF_LOG_DEBUG("em4x05: block 0 read failed");
        return false;
    }

    /* Block 15: UID (EM4x05) */
    if (!em4x05_read_block(EM4X05_BLOCK_UID, &out->uid, block_timeout)) {
        NRF_LOG_DEBUG("em4x05: block 15 read failed");
        return false;
    }

    /*
     * Attempt EM4x69 64-bit UID blocks (13 and 14).
     * Failure here is non-fatal — tag may simply be an EM4x05.
     */
    uint32_t uid_lo = 0, uid_hi = 0;
    if (em4x05_read_block(EM4X69_BLOCK_UID_LO, &uid_lo, block_timeout) &&
        em4x05_read_block(EM4X69_BLOCK_UID_HI, &uid_hi, block_timeout)) {
        out->uid_hi   = uid_hi;
        out->uid      = uid_lo;   /* overwrite with EM4x69 UID lo */
        out->is_em4x69 = true;
    }

    return true;
}

uint8_t scan_em4x05(em4x05_data_t *out) {
    start_lf_125khz_radio();
    bsp_delay_ms(5);   /* allow tag to power up */

    bool found = em4x05_read(out, 500);

    stop_lf_125khz_radio();

    return found ? STATUS_LF_TAG_OK : STATUS_LF_TAG_NO_FOUND;
}
