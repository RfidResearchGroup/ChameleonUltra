#include "jablotron.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf_pwm.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"
#include "utils/diphase.h"

/*
 * Jablotron LF tag protocol
 * Differential Biphase (inverted), RF/64, 64 bits total
 *
 * Frame layout (MSB first):
 *   Bits  0-15 : Preamble 0xFFFF (16 ones)
 *   Bits 16-55 : 40-bit data (5 bytes), bit 16 always 0
 *   Bits 56-63 : 8-bit checksum = (sum of 5 data bytes) ^ 0x3A
 *
 * Reference: Proxmark3 cmdlfjablotron.c
 */

#define JABLOTRON_RAW_SIZE (64)
#define JABLOTRON_DATA_SIZE (5)
// 2 half-bit entries per bit * 2 passes (double-frame) = 256 entries.
// The frame is encoded twice so the PWM buffer loops cleanly: a single
// 64-bit diphase frame with an odd number of zero bits ends at a level
// opposite the starting level, leaving no transition at the loop boundary
// where the reader expects one.  Encoding the frame twice with level
// persisting between passes guarantees a continuous diphase stream
// regardless of the data's zero-count parity.
#define JABLOTRON_PWM_SIZE (JABLOTRON_RAW_SIZE * 2 * 2)
#define JABLOTRON_T55XX_BLOCK_COUNT (3)

// Edge timing for RF/64, same thresholds as EM410x/64
#define JABLOTRON_READ_TIME1_BASE (0x40)
#define JABLOTRON_READ_TIME2_BASE (0x60)
#define JABLOTRON_READ_TIME3_BASE (0x80)
#define JABLOTRON_READ_JITTER_TIME_BASE (0x10)

#define NRF_LOG_MODULE_NAME jablotron_protocol
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static nrf_pwm_values_wave_form_t m_jablotron_pwm_seq_vals[JABLOTRON_PWM_SIZE] = {};

nrf_pwm_sequence_t const m_jablotron_pwm_seq = {
    .values.p_wave_form = m_jablotron_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_jablotron_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

typedef struct {
    uint8_t data[JABLOTRON_DATA_SIZE];
    uint64_t raw;
    uint8_t raw_length;
    diphase *modem;
} jablotron_codec;

static uint64_t jablotron_raw_data(uint8_t *uid) {
    uint64_t raw = (uint64_t)0xFFFF << 48;  // preamble

    // 40-bit data (5 bytes) at bits 47-8
    uint8_t chksum = 0;
    for (int i = 0; i < JABLOTRON_DATA_SIZE; i++) {
        raw |= ((uint64_t)uid[i]) << (40 - 8 * i);
        chksum += uid[i];
    }

    // 8-bit checksum at bits 7-0
    raw |= (uint64_t)((chksum ^ 0x3A) & 0xFF);

    return raw;
}

static bool jablotron_get_time(uint8_t interval, uint8_t base) {
    return interval >= (base - JABLOTRON_READ_JITTER_TIME_BASE) &&
           interval <= (base + JABLOTRON_READ_JITTER_TIME_BASE);
}

static uint8_t jablotron_period(uint8_t interval) {
    if (jablotron_get_time(interval, JABLOTRON_READ_TIME1_BASE)) {
        return 0;
    }
    if (jablotron_get_time(interval, JABLOTRON_READ_TIME2_BASE)) {
        return 1;
    }
    if (jablotron_get_time(interval, JABLOTRON_READ_TIME3_BASE)) {
        return 2;
    }
    return 3;
}

static jablotron_codec *jablotron_alloc(void) {
    jablotron_codec *codec = malloc(sizeof(jablotron_codec));
    codec->modem = malloc(sizeof(diphase));
    codec->modem->rp = jablotron_period;
    return codec;
}

static void jablotron_free(jablotron_codec *d) {
    if (d->modem) {
        free(d->modem);
        d->modem = NULL;
    }
    free(d);
}

static uint8_t *jablotron_get_data(jablotron_codec *d) {
    return d->data;
}

static void jablotron_decoder_start(jablotron_codec *d, uint8_t format) {
    memset(d->data, 0, JABLOTRON_DATA_SIZE);
    d->raw = 0;
    d->raw_length = 0;
    diphase_reset(d->modem);
}

static bool jablotron_decode_feed(jablotron_codec *d, bool bit) {
    d->raw <<= 1;
    d->raw_length++;
    if (bit) {
        d->raw |= 0x01;
    }
    if (d->raw_length < JABLOTRON_RAW_SIZE) {
        return false;
    }

    // Check preamble: upper 16 bits must be 0xFFFF
    if (((d->raw >> 48) & 0xFFFF) != 0xFFFF) {
        return false;
    }

    // Bit 47 (MSB of data) must be 0 for preamble detection
    if ((d->raw >> 47) & 0x01) {
        return false;
    }

    // Validate checksum: sum of 5 data bytes XOR 0x3A
    uint8_t chksum = 0;
    for (int i = 0; i < JABLOTRON_DATA_SIZE; i++) {
        chksum += (d->raw >> (40 - 8 * i)) & 0xFF;
    }
    chksum ^= 0x3A;

    if (chksum != (d->raw & 0xFF)) {
        return false;
    }

    // Extract 5-byte data
    for (int i = 0; i < JABLOTRON_DATA_SIZE; i++) {
        d->data[i] = (d->raw >> (40 - 8 * i)) & 0xFF;
    }

    return true;
}

static bool jablotron_decoder_feed(jablotron_codec *d, uint16_t interval) {
    bool bits[2] = {0};
    int8_t bitlen = 0;
    diphase_feed(d->modem, (uint8_t)interval, bits, &bitlen);
    if (bitlen == -1) {
        d->raw = 0;
        d->raw_length = 0;
        return false;
    }
    for (int i = 0; i < bitlen; i++) {
        if (jablotron_decode_feed(d, bits[i])) {
            return true;
        }
    }
    return false;
}

/*
 * Diphase modulator: encode raw 64-bit frame into PWM sequence.
 *
 * Each bit produces two half-bit PWM entries.  counter_top = 31 gives
 * 32 ticks per entry at NRF_PWM_CLK_125kHz = 32 carrier cycles per
 * half-bit = exactly RF/64 per bit.
 *
 * The frame is encoded twice (256 entries total) with level carrying
 * across the two passes; see the comment on JABLOTRON_PWM_SIZE.
 *
 * Inverted diphase rules:
 *   - Boundary transition: always (level flips at start of each bit).
 *   - Bit 1: no mid-bit transition (both halves same level).
 *   - Bit 0: mid-bit transition (halves at different levels).
 *
 * Constant-level PWM encoding (same pattern as pac.c):
 *   compare = 0              -> pin held LOW  (counter never < 0)
 *   compare = counter_top+1  -> pin held HIGH (counter never reaches 32)
 * Using counter_top+1 (not counter_top) avoids a 1-tick glitch from
 * simultaneous compare-match and counter-wrap.
 */
static const nrf_pwm_sequence_t *jablotron_modulator(jablotron_codec *d, uint8_t *buf) {
    uint64_t raw = jablotron_raw_data(buf);
    bool level = false;  // carries across both passes

    int out = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < JABLOTRON_RAW_SIZE; i++) {
            bool bit = (raw >> (JABLOTRON_RAW_SIZE - 1 - i)) & 0x01;

            // Boundary transition: always flip
            level = !level;

            // First half-bit
            m_jablotron_pwm_seq_vals[out].channel_0 = level ? 32 : 0;
            m_jablotron_pwm_seq_vals[out].counter_top = 31;
            out++;

            // Mid-bit transition for bit 0
            if (!bit) {
                level = !level;
            }

            // Second half-bit
            m_jablotron_pwm_seq_vals[out].channel_0 = level ? 32 : 0;
            m_jablotron_pwm_seq_vals[out].counter_top = 31;
            out++;
        }
    }

    return &m_jablotron_pwm_seq;
}

const protocol jablotron = {
    .tag_type = TAG_TYPE_JABLOTRON,
    .data_size = JABLOTRON_DATA_SIZE,
    .alloc = (codec_alloc)jablotron_alloc,
    .free = (codec_free)jablotron_free,
    .get_data = (codec_get_data)jablotron_get_data,
    .modulator = (modulator)jablotron_modulator,
    .decoder =
        {
            .start = (decoder_start)jablotron_decoder_start,
            .feed = (decoder_feed)jablotron_decoder_feed,
        },
};

uint8_t jablotron_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    uint64_t raw = jablotron_raw_data(uid);
    blks[0] = T5577_JABLOTRON_CONFIG;
    blks[1] = raw >> 32;
    blks[2] = raw & 0xFFFFFFFF;
    return JABLOTRON_T55XX_BLOCK_COUNT;
}
