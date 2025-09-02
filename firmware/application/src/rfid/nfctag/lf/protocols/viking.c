#include "viking.h"

#include <stdlib.h>
#include <string.h>

#include "em410x.h"
#include "nordic_common.h"
#include "nrf_pwm.h"
#include "parity.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"
#include "utils/manchester.h"

#define EM_BITS_PER_ROW_COUNT (EM_COLUMN_COUNT + 1)

#define VIKING_RAW_SIZE (64) // Preamble (24) + data (32) + checksum (8)
#define VIKING_DATA_SIZE (4) // Card data is 4 bytes
#define VIKING_HEADER (0xf20000) // Preamble... 11110010 00000000 00000000

#define VIKING_T55XX_BLOCK_COUNT (3) // config + 2 data blocks

// Duration between falling edges is... 
#define VIKING_READ_TIME1_BASE (0x20) // on 16, off 16 cycles
#define VIKING_READ_TIME2_BASE (0x30) // on 16, off 32 cycles  (or on 32 cycles, off 16 cycles)
#define VIKING_READ_TIME3_BASE (0x40) // on 32, off 32 cycles
#define VIKING_READ_JITTER_TIME_BASE (0x07) // Jitter is just under half of 16 cycles.

#define NRF_LOG_MODULE_NAME viking_protocol
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static nrf_pwm_values_wave_form_t m_viking_pwm_seq_vals[VIKING_RAW_SIZE] = {};

nrf_pwm_sequence_t const m_viking_pwm_seq = {
    .values.p_wave_form = m_viking_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_viking_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

typedef struct {
    uint8_t data[VIKING_DATA_SIZE];
    uint64_t raw;
    uint8_t raw_length;
    manchester *modem;
} viking_codec;

static uint64_t viking_raw_data(uint8_t *uid) {
    uint64_t raw = VIKING_HEADER;
    uint8_t crc = 0x5A;
    for (int8_t i = 0; i < VIKING_DATA_SIZE; i++) {
        raw <<= 8;
        raw |= uid[i];
        crc ^= uid[i];
    }
    raw <<= 8;
    raw |= crc;
    return raw;
}

static bool viking_get_time(uint8_t interval, uint8_t base) {
    return interval >= (base - VIKING_READ_JITTER_TIME_BASE) &&
           interval <= (base + VIKING_READ_JITTER_TIME_BASE);
}

static uint8_t viking_period(uint8_t interval) {
    if (viking_get_time(interval, VIKING_READ_TIME1_BASE)) { // short/short
        return 0;
    }
    if (viking_get_time(interval, VIKING_READ_TIME2_BASE)) { // short/long or long/short
        return 1;
    }
    if (viking_get_time(interval, VIKING_READ_TIME3_BASE)) { // long/long
        return 2;
    }
    return 3; // Not manchester (or bad signal).
}

static viking_codec *viking_alloc(void) {
    viking_codec *codec = malloc(sizeof(viking_codec));
    codec->modem = malloc(sizeof(manchester));
    codec->modem->rp = viking_period;
    return codec;
};

static void viking_free(viking_codec *d) {
    if (d->modem) {
        free(d->modem);
        d->modem = NULL;
    }
    free(d);
};

static uint8_t *viking_get_data(viking_codec *d) { 
    return d->data;
};

static void viking_decoder_start(viking_codec *d, uint8_t format) {
    memset(d->data, 0, VIKING_DATA_SIZE);
    d->raw = 0;
    d->raw_length = 0;
    manchester_reset(d->modem);
};

static bool viking_decode_feed(viking_codec *d, bool bit) {
    d->raw <<= 1;
    d->raw_length++;
    if (bit) {
        d->raw |= 0x01;
    }
    if (d->raw_length < (VIKING_RAW_SIZE-2)) {
        return false;
    }

    // Check header
    uint8_t v = (d->raw >> (VIKING_RAW_SIZE - 24)) & 0xff; // Check LSB of header
    if (v != ((VIKING_HEADER & 0xFF))) {
        return false;
    }
    v = (d->raw >> (VIKING_RAW_SIZE - 16)) & 0xff; // Check mid of header
    if (v != ((VIKING_HEADER >> 8) & 0xFF)) {
        return false;
    }
    v = (d->raw >> (VIKING_RAW_SIZE - 8)) & 0xff; // Check MSB of header
    if ((v & 0xFF) != ((VIKING_HEADER >> 16) & 0xFF)) {
        return false;
    }

    // Validate CRC
    uint8_t crc = 0x5A;
    for (int i = 0; i < VIKING_DATA_SIZE; i++) {
        uint8_t data = (d->raw >> ((i+1)*8)) & 0xff;
        crc ^= data;
        d->data[VIKING_DATA_SIZE - i - 1] |= data;
    }

    return crc == (d->raw & 0xff);
}

static bool viking_decoder_feed(viking_codec *d, uint16_t interval) {
    bool bits[2] = {0};
    int8_t bitlen = 0;

    // Hack: due to hardware sometimes not detecting a time2 pulse. Rather than 
    // reset when interval is really long, assume there was a time2 pulse.
    if (interval > VIKING_READ_TIME3_BASE + VIKING_READ_JITTER_TIME_BASE) {
        interval -= VIKING_READ_TIME2_BASE;
        manchester_feed(d->modem, (uint8_t)VIKING_READ_TIME2_BASE, bits, &bitlen);
        if (bitlen == -1) {
            d->raw = 0;
            d->raw_length = 0;
            return false;
        }
        for (int i = 0; i < bitlen; i++) {
            if (viking_decode_feed(d, bits[i])) {
                return true;
            }
        }
    }

    manchester_feed(d->modem, (uint8_t)interval, bits, &bitlen);
    if (bitlen == -1) {
        if (d->raw == 62) {
            if (viking_decode_feed(d, 1)) {
                return true;
            }
        }
        d->raw = 0;
        d->raw_length = 0;
        return false;
    }
    for (int i = 0; i < bitlen; i++) {
        if (viking_decode_feed(d, bits[i])) {
            return true;
        }
    }
    return false;
};

static const nrf_pwm_sequence_t *viking_modulator(viking_codec *d, uint8_t *buf) {
    uint64_t lo = viking_raw_data(buf);
    for (int i = 0; i < VIKING_RAW_SIZE; i++) {
        uint16_t msb = 0x00;
        if (IS_SET(lo, VIKING_RAW_SIZE - i - 1)) {
            msb = (1 << 15);
        }
        m_viking_pwm_seq_vals[i].channel_0 = msb | 16;
        m_viking_pwm_seq_vals[i].counter_top = 32;

    }
    return &m_viking_pwm_seq;
};

// Viking card
const protocol viking = {
    .tag_type = TAG_TYPE_VIKING,
    .data_size = VIKING_DATA_SIZE,
    .alloc = (codec_alloc)viking_alloc,
    .free = (codec_free)viking_free,
    .get_data = (codec_get_data)viking_get_data,
    .modulator = (modulator)viking_modulator,
    .decoder =
        {
            .start = (decoder_start)viking_decoder_start,
            .feed = (decoder_feed)viking_decoder_feed,
        },
};

// Encode viking card number to T55xx blocks.
uint8_t viking_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    uint64_t raw = viking_raw_data(uid);
    blks[0] = T5577_VIKING_CONFIG;
    blks[1] = raw >> 32;
    blks[2] = raw & 0xffffffff;
    return VIKING_T55XX_BLOCK_COUNT;
}