#include "pyramid.h"

#include <stdlib.h>
#include <string.h>

#include "nrf_pwm.h"
#include "tag_base_type.h"

// FSK2a, RF/50. A stored bit value of 0 is sent as fc/8 (high sub-carrier) and
// 1 as fc/10. Verified against a real Farpointe tag captured with this device:
// the leading preamble zeros are fc/8 and the bit values match the tag exactly.
//
// Unlike HID/ioProx, which emit a fixed integer number of sub-carrier cycles
// per bit, Pyramid REQUIRES exact RF/50 bit-period alignment: its frame opens
// with a 15-bit run of zeros (all fc/8). counter_top=8 does not divide the
// 50-tick bit period, so a fixed 6 cycles (48 ticks) runs 2 ticks short per
// bit; across the preamble that accumulates past a full bit and a reader (e.g.
// Proxmark, which demods at a fixed RF/50 clock) drops a preamble bit and fails
// to match. We instead use a phase accumulator: emit whole sub-carrier cycles
// until the cumulative tick count reaches each bit's exact (i+1)*50 boundary.
// fc/10 is always 5 cycles (10|50); fc/8 self-corrects between 6 and 7 cycles
// (avg 6.25 = 50 ticks), so bit boundaries never drift.
#define PYRAMID_FC_HI_TOP (8)    // fc/8  @ 125kHz base (bit value 0)
#define PYRAMID_FC_LO_TOP (10)   // fc/10 @ 125kHz base (bit value 1)
#define PYRAMID_BIT_TICKS (50)   // RF/50: carrier ticks per data bit

// Worst case is all-fc/8: ceil(128*50/8) = 800 cycles. Round up for headroom.
static nrf_pwm_values_wave_form_t m_pyramid_pwm_seq_vals[PYRAMID_RAW_BITS * 7] = {};

static nrf_pwm_sequence_t m_pyramid_pwm_seq = {
    .values.p_wave_form = m_pyramid_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_pyramid_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

static void *pyramid_codec_alloc(void) {
    pyramid_codec_t *d = (pyramid_codec_t *)malloc(sizeof(pyramid_codec_t));
    if (!d) return NULL;
    memset(d, 0, sizeof(*d));
    // RF/50 FSK demod, same modulation / bitrate as HID Prox. Enable the
    // running DC baseline tracker: the raw 14-bit ADC DC otherwise swamps the
    // fc/8 Goertzel bin and pins every bit to 0.
    d->modem = fsk_alloc(FSK_BITRATE_HID);
    if (d->modem) {
        d->modem->dc_block = true;
    }
    return d;
}

static void pyramid_codec_free(void *codec) {
    pyramid_codec_t *d = (pyramid_codec_t *)codec;
    if (!d) return;
    if (d->modem) {
        fsk_free(d->modem);
        d->modem = NULL;
    }
    free(d);
}

static uint8_t *pyramid_get_data(void *codec) {
    return ((pyramid_codec_t *)codec)->data;
}

// FSK2a modulator: replay the raw 128-bit frame MSB-first as a PWM sequence,
// keeping every bit boundary aligned to RF/50 via a tick-count accumulator.
const nrf_pwm_sequence_t *pyramid_modulator(pyramid_codec_t *d, uint8_t *buf) {
    (void)d;

    int k = 0;
    uint32_t emitted = 0;  // total carrier ticks emitted so far

    for (int bi = 0; bi < PYRAMID_RAW_BITS; bi++) {
        bool bit = (buf[bi >> 3] >> (7 - (bi & 7))) & 1u;
        uint16_t top = bit ? PYRAMID_FC_LO_TOP : PYRAMID_FC_HI_TOP;
        uint32_t boundary = (uint32_t)(bi + 1) * PYRAMID_BIT_TICKS;

        // Emit whole sub-carrier cycles until we reach this bit's tick boundary.
        while (emitted < boundary) {
            m_pyramid_pwm_seq_vals[k].channel_0   = top / 2;  // 50% duty
            m_pyramid_pwm_seq_vals[k].counter_top = top;
            k++;
            emitted += top;
        }
    }

    m_pyramid_pwm_seq.length = (uint16_t)(k * 4);
    return &m_pyramid_pwm_seq;
}

// ---------------------------------------------------------------------------
// Reader / decoder
//
// FSK demod (fsk_feed) yields one raw bit per RF/50 window; bits accumulate in
// a ring buffer. We scan for the 24-bit Pyramid preamble {0x15 zeros, 1, 0x7
// zeros, 1} and validate a candidate 128-bit frame three ways before accepting
// it: 24-bit preamble, odd parity on every 8-bit group (bits 8..127), and a
// CRC-8/Maxim trailer over the 13 payload bytes. With all three gates a false
// positive is effectively impossible, so a single frame match is enough.
// ---------------------------------------------------------------------------

// 24-bit preamble: fifteen 0s, a 1, seven 0s, a 1 (matches Proxmark detectPyramid)
static const uint8_t PYRAMID_PREAMBLE[24] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 0, 0, 0, 0, 1,
};

// Reads n bits MSB-first from bits[off] into an integer.
static uint32_t bits_to_u32(const uint8_t *bits, uint16_t off, uint8_t n) {
    uint32_t v = 0;
    for (uint8_t i = 0; i < n; i++) {
        v = (v << 1) | (bits[off + i] & 1u);
    }
    return v;
}

// CRC-8/Maxim (Dallas 1-Wire): poly 0x31 reflected (0x8C), init 0x00.
static uint8_t pyramid_crc8_maxim(const uint8_t *data, uint8_t len) {
    uint8_t crc = 0;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 1u) ? (uint8_t)((crc >> 1) ^ 0x8Cu) : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

static void pyramid_reset_bits(pyramid_codec_t *d) {
    d->bit_len = 0;
}

static inline void pyramid_push_bit(pyramid_codec_t *d, uint8_t bit) {
    if (d->bit_len < PYRAMID_MAX_BITS) {
        d->bits[d->bit_len++] = bit;
        return;
    }
    // Buffer full: drop the oldest bit and append the new one
    memmove(d->bits, d->bits + 1, PYRAMID_MAX_BITS - 1);
    d->bits[PYRAMID_MAX_BITS - 1] = bit;
}

// Validates and unpacks the 128-bit frame whose preamble starts at idx.
// Requires idx + 128 <= bit_len. On success packs the raw 16 bytes into d->data.
static bool pyramid_try_decode(pyramid_codec_t *d, uint16_t idx) {
    const uint8_t *b = d->bits;

    // Odd parity on every 8-bit group of bits 8..127 (15 groups, 7 data + 1 parity)
    for (uint16_t g = idx + 8; g < idx + 128; g += 8) {
        uint32_t grp = bits_to_u32(b, g, 8);
        if ((__builtin_popcount(grp) & 1u) == 0u) {
            return false;  // group must have odd parity
        }
    }

    // CRC-8/Maxim over the 13 payload bytes (bits 16..119) vs trailer (bits 120..127)
    uint8_t cs[13];
    for (uint8_t i = 0; i < 13; i++) {
        cs[i] = (uint8_t)bits_to_u32(b, (uint16_t)(idx + 16 + i * 8), 8);
    }
    uint8_t checksum = (uint8_t)bits_to_u32(b, (uint16_t)(idx + 120), 8);
    if (pyramid_crc8_maxim(cs, 13) != checksum) {
        return false;
    }

    // Valid: pack the raw 128-bit frame MSB-first into the 16-byte output
    for (uint8_t i = 0; i < PYRAMID_DATA_SIZE; i++) {
        d->data[i] = (uint8_t)bits_to_u32(b, (uint16_t)(idx + i * 8), 8);
    }
    return true;
}

// Scans the bit buffer for a valid frame anchored on the 24-bit preamble.
static bool pyramid_scan(pyramid_codec_t *d) {
    if (d->bit_len < PYRAMID_RAW_BITS) return false;

    for (uint16_t i = 0; (uint16_t)(i + PYRAMID_RAW_BITS) <= d->bit_len; i++) {
        bool pre_ok = true;
        for (uint8_t k = 0; k < sizeof(PYRAMID_PREAMBLE); k++) {
            if ((d->bits[i + k] & 1u) != PYRAMID_PREAMBLE[k]) {
                pre_ok = false;
                break;
            }
        }
        if (!pre_ok) continue;
        if (pyramid_try_decode(d, i)) {
            return true;
        }
    }
    return false;
}

static void pyramid_decoder_start(void *codec, uint8_t format_hint) {
    (void)format_hint;
    pyramid_codec_t *d = (pyramid_codec_t *)codec;
    d->bit_len = 0;
    memset(d->bits, 0, sizeof(d->bits));
    memset(d->data, 0, sizeof(d->data));
}

static bool pyramid_decoder_feed(void *codec, uint16_t val) {
    pyramid_codec_t *d = (pyramid_codec_t *)codec;
    if (!d || !d->modem) return false;

    bool bit = false;
    if (!fsk_feed(d->modem, val, &bit)) {
        return false;
    }

    pyramid_push_bit(d, (uint8_t)(bit ? 1u : 0u));

    if (d->bit_len >= PYRAMID_RAW_BITS) {
        if (pyramid_scan(d)) {
            pyramid_reset_bits(d);
            return true;
        }
    }
    return false;
}

const protocol pyramid = {
    .tag_type = TAG_TYPE_FARPOINTE_PYRAMID,
    .data_size = PYRAMID_DATA_SIZE,
    .alloc = (codec_alloc)pyramid_codec_alloc,
    .free = (codec_free)pyramid_codec_free,
    .get_data = (codec_get_data)pyramid_get_data,
    .modulator = (modulator)pyramid_modulator,
    .decoder = {
        .start = (decoder_start)pyramid_decoder_start,
        .feed  = (decoder_feed)pyramid_decoder_feed,
    },
};
