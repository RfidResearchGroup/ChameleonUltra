#include "em410x.h"

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

#define EM_RAW_SIZE (64)
#define EM_DATA_SIZE_BASE (5)
#define EM_ELECTRA_EPILOGUE_SIZE (8)
#define EM_DATA_SIZE_ELECTRA (EM_DATA_SIZE_BASE + EM_ELECTRA_EPILOGUE_SIZE)
#define EM_DATA_SIZE_MAX (EM_DATA_SIZE_ELECTRA)
#define EM_T55XX_ELECTRA_BLOCK_COUNT (5)
#define EM_ROW_COUNT (10)
#define EM_COLUMN_COUNT (4)
#define EM_HEADER (0x1ff)  // 9 bits of 1

#define EM_ENCODED_DATA_HEADER (0xFF80000000000000ULL)

#define EM_T55XX_BLOCK_COUNT (3)

#define EM_READ_TIME1_BASE (0x40)
#define EM_READ_TIME2_BASE (0x60)
#define EM_READ_TIME3_BASE (0x80)
#define EM_READ_JITTER_TIME_BASE (0x10)

#define NRF_LOG_MODULE_NAME em4100
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static nrf_pwm_values_wave_form_t m_em410x_pwm_seq_vals_base[EM_RAW_SIZE] = {};
static nrf_pwm_values_wave_form_t m_em410x_pwm_seq_vals_electra[EM_RAW_SIZE * 2] = {};

nrf_pwm_sequence_t const m_em410x_pwm_seq_base = {
    .values.p_wave_form = m_em410x_pwm_seq_vals_base,
    .length = NRF_PWM_VALUES_LENGTH(m_em410x_pwm_seq_vals_base),
    .repeats = 0,
    .end_delay = 0,
};

nrf_pwm_sequence_t const m_em410x_pwm_seq_electra = {
    .values.p_wave_form = m_em410x_pwm_seq_vals_electra,
    .length = NRF_PWM_VALUES_LENGTH(m_em410x_pwm_seq_vals_electra),
    .repeats = 0,
    .end_delay = 0,
};

const protocol *em410x_protocols[] = {
    &em410x_electra,
    &em410x_64,
    &em410x_32,
    &em410x_16,
};

size_t em410x_protocols_size = ARRAY_SIZE(em410x_protocols);

typedef struct {
    uint8_t data[EM_DATA_SIZE_MAX];
    uint64_t raw;
    uint64_t epilogue;
    uint8_t raw_length;
    uint8_t total_length;
    manchester *modem;
} em410x_codec;

uint64_t em410x_raw_data(uint8_t *uid) {
    uint64_t raw = EM_HEADER;
    uint8_t pc = 0x00;  // column parity
    // 10 rows, each row is 4 bits data + 1 bit parity
    for (int8_t i = 0; i < EM_ROW_COUNT; i++) {
        uint8_t data;
        if (i % 2) {
            data = uid[i >> 1] & 0x0f;
        } else {
            data = (uid[i >> 1] >> EM_COLUMN_COUNT) & 0x0f;
        }
        pc ^= data;
        raw = (raw << EM_COLUMN_COUNT) | data;
        raw <<= 1;
        if (!oddparity8(data)) {
            raw |= 0x01;  // row parity bit
        }
    }
    raw = (raw << EM_COLUMN_COUNT) | pc;  // column parity
    raw <<= 1;                            // stop bit
    return raw;
}

uint64_t em410x_raw_epilogue(uint8_t *uid) {
    uint64_t raw = 0;

    for (int i = 0; i < EM_ELECTRA_EPILOGUE_SIZE; i++) {
        raw <<= 8;
        raw |= uid[EM_DATA_SIZE_BASE + i];
    }

    return raw;
}

bool em410x_get_time(uint16_t divisor, uint8_t interval, uint8_t base) {
    return interval >= (base - EM_READ_JITTER_TIME_BASE) / divisor &&
           interval <= (base + EM_READ_JITTER_TIME_BASE) / divisor;
}

uint8_t em410x_period(uint16_t divisor, uint8_t interval) {
    if (em410x_get_time(divisor, interval, EM_READ_TIME1_BASE)) {
        return 0;
    }
    if (em410x_get_time(divisor, interval, EM_READ_TIME2_BASE)) {
        return 1;
    }
    if (em410x_get_time(divisor, interval, EM_READ_TIME3_BASE)) {
        return 2;
    }
    return 3;
}

uint8_t em410x_64_period(uint8_t interval) {
    return em410x_period(1, interval);  // clock_per_bit = 64, divisor = 1
}

uint8_t em410x_32_period(uint8_t interval) {
    return em410x_period(2, interval);  // clock_per_bit = 32, divisor = 2
}

uint8_t em410x_16_period(uint8_t interval) {
    return em410x_period(4, interval);  // clock_per_bit = 16, divisor = 4
}

em410x_codec *em410x_64_alloc(void) {
    em410x_codec *codec = malloc(sizeof(em410x_codec));
    codec->modem = malloc(sizeof(manchester));
    codec->modem->rp = em410x_64_period;
    return codec;
};

em410x_codec *em410x_32_alloc(void) {
    em410x_codec *codec = malloc(sizeof(em410x_codec));
    codec->modem = malloc(sizeof(manchester));
    codec->modem->rp = em410x_32_period;
    return codec;
};

em410x_codec *em410x_16_alloc(void) {
    em410x_codec *codec = malloc(sizeof(em410x_codec));
    codec->modem = malloc(sizeof(manchester));
    codec->modem->rp = em410x_16_period;
    return codec;
};

void em410x_free(em410x_codec *d) {
    if (d->modem) {
        free(d->modem);
        d->modem = NULL;
    }
    free(d);
};

uint8_t *em410x_get_data(em410x_codec *d) { return d->data; };

void em410x_decoder_start(em410x_codec *d, uint8_t format) {
    memset(d->data, 0, EM_DATA_SIZE_MAX);
    d->raw = 0;
    d->raw_length = 0;
    d->total_length = 0;
    d->epilogue = 0;
    manchester_reset(d->modem);
};

bool em410x_decode_feed(em410x_codec *d, bool bit) {
    d->raw <<= 1;
    d->raw_length++;
    if (bit) {
        d->raw |= 0x01;
    }
    if (d->raw_length < EM_RAW_SIZE) {
        return false;
    }

    // check header
    uint8_t v = (d->raw >> (EM_RAW_SIZE - 8)) & 0xff;
    if (v != 0xff) {
        return false;
    }
    v = (d->raw >> (EM_RAW_SIZE - 9)) & 0xff;
    if (v != 0xff) {
        return false;
    }

    // check stop bit
    if (d->raw & 0x01) {
        return false;
    }

    uint8_t pc = 0;
    for (int i = 0; i < EM_ROW_COUNT + 1; i++) {
        uint8_t row = d->raw >> (EM_RAW_SIZE - 9 - (i + 1) * EM_BITS_PER_ROW_COUNT) & 0x1f;
        uint8_t data = (row >> 1) & 0x0f;
        pc ^= data;
        if (i == 10) {
            break;
        }

        if (!oddparity8(row)) {  // row parity
            return false;
        }

        if (i % 2) {
            d->data[i >> 1] |= data;
        } else {
            d->data[i >> 1] = data << 4;
        }
    }
    return pc == 0x00;  // column parity
}

bool em410x_decoder_feed(em410x_codec *d, uint16_t interval) {
    bool bits[2] = {0};
    int8_t bitlen = 0;
    manchester_feed(d->modem, (uint8_t)interval, bits, &bitlen);
    if (bitlen == -1) {
        d->raw = 0;
        d->raw_length = 0;
        d->total_length = 0;
        d->epilogue = 0;
        return false;
    }
    for (int i = 0; i < bitlen; i++) {
        if (em410x_decode_feed(d, bits[i])) {
            return true;
        }
    }
    return false;
};

void em410x_electra_decoder_start(em410x_codec *d, uint8_t format) {
    em410x_decoder_start(d, format);
}

static bool em410x_electra_decode_feed(em410x_codec *d, bool bit) {
    bool carry_bit = (d->epilogue >> 63) & 0x01;

    if (d->total_length < EM_RAW_SIZE + EM_RAW_SIZE) {
        d->total_length++;
    }
    d->raw = (d->raw << 1) | carry_bit;
    d->epilogue = (d->epilogue << 1) | (bit ? 1 : 0);

    if (d->total_length < EM_RAW_SIZE + EM_RAW_SIZE) {
        return false;
    }

    if ((d->raw & EM_ENCODED_DATA_HEADER) != EM_ENCODED_DATA_HEADER) {
        return false;
    }

    if (d->raw & 0x01) {
        return false;
    }

    uint8_t pc = 0;
    for (int i = 0; i < EM_ROW_COUNT + 1; i++) {
        uint8_t row = d->raw >> (EM_RAW_SIZE - 9 - (i + 1) * EM_BITS_PER_ROW_COUNT) & 0x1f;
        uint8_t data = (row >> 1) & 0x0f;
        pc ^= data;
        if (i == 10) {
            break;
        }

        if (!oddparity8(row)) {  // row parity
            return false;
        }

        if (i % 2) {
            d->data[i >> 1] |= data;
        } else {
            d->data[i >> 1] = data << 4;
        }
    }

    // if we only saw the same frame twice, treat as standard EM410X
    if (d->raw == d->epilogue) {
        return false;
    }

    for (int i = 0; i < EM_ELECTRA_EPILOGUE_SIZE; i++) {
        d->data[EM_DATA_SIZE_BASE + i] = (d->epilogue >> ((EM_ELECTRA_EPILOGUE_SIZE - 1 - i) * 8)) & 0xFF;
    }

    return pc == 0x00;
}

bool em410x_electra_decoder_feed(em410x_codec *d, uint16_t interval) {
    bool bits[2] = {0};
    int8_t bitlen = 0;
    manchester_feed(d->modem, (uint8_t)interval, bits, &bitlen);
    if (bitlen == -1) {
        em410x_decoder_start(d, 0);
        return false;
    }
    for (int i = 0; i < bitlen; i++) {
        if (em410x_electra_decode_feed(d, bits[i])) {
            return true;
        }
    }
    return false;
};

const nrf_pwm_sequence_t *em410x_modulator(em410x_codec *d, uint8_t *buf) {
    uint64_t lo = em410x_raw_data(buf);
    for (int i = 0; i < EM_RAW_SIZE; i++) {
        uint16_t msb = 0x00;
        if (IS_SET(lo, EM_RAW_SIZE - i - 1)) {
            msb = (1 << 15);
        }
        m_em410x_pwm_seq_vals_base[i].channel_0 = msb | 32;
        m_em410x_pwm_seq_vals_base[i].counter_top = 64;
    }
    return &m_em410x_pwm_seq_base;
};

const nrf_pwm_sequence_t *em410x_electra_modulator(em410x_codec *d, uint8_t *buf) {
    uint64_t data[] = {em410x_raw_data(buf), em410x_raw_epilogue(buf)};
    uint16_t output_index = 0;

    for (int frame = 0; frame < 2; frame++) {
        for (int i = 0; i < EM_RAW_SIZE; i++) {
            uint16_t msb = 0x00;
            if (IS_SET(data[frame], EM_RAW_SIZE - i - 1)) {
                msb = (1 << 15);
            }
            m_em410x_pwm_seq_vals_electra[output_index].channel_0 = msb | 32;
            m_em410x_pwm_seq_vals_electra[output_index].counter_top = 64;
            output_index++;
        }
    }

    return &m_em410x_pwm_seq_electra;
};

// EM-Micro, EM410x/64 (std)
const protocol em410x_electra = {
    .tag_type = TAG_TYPE_EM410X_ELECTRA,
    .data_size = EM_DATA_SIZE_ELECTRA,
    .alloc = (codec_alloc)em410x_64_alloc,
    .free = (codec_free)em410x_free,
    .get_data = (codec_get_data)em410x_get_data,
    .modulator = (modulator)em410x_electra_modulator,
    .decoder =
        {
            .start = (decoder_start)em410x_electra_decoder_start,
            .feed = (decoder_feed)em410x_electra_decoder_feed,
        },
};

// EM-Micro, EM410x/64 (std)
const protocol em410x_64 = {
    .tag_type = TAG_TYPE_EM410X_64,
    .data_size = EM_DATA_SIZE_BASE,
    .alloc = (codec_alloc)em410x_64_alloc,
    .free = (codec_free)em410x_free,
    .get_data = (codec_get_data)em410x_get_data,
    .modulator = (modulator)em410x_modulator,
    .decoder =
        {
            .start = (decoder_start)em410x_decoder_start,
            .feed = (decoder_feed)em410x_decoder_feed,
        },
};

// EM-Micro, EM410x/32
const protocol em410x_32 = {
    .tag_type = TAG_TYPE_EM410X_32,
    .data_size = EM_DATA_SIZE_BASE,
    .alloc = (codec_alloc)em410x_32_alloc,
    .free = (codec_free)em410x_free,
    .get_data = (codec_get_data)em410x_get_data,
    .modulator = (modulator)em410x_modulator,
    .decoder =
        {
            .start = (decoder_start)em410x_decoder_start,
            .feed = (decoder_feed)em410x_decoder_feed,
        },
};

// EM-Micro, EM410x/16
const protocol em410x_16 = {
    .tag_type = TAG_TYPE_EM410X_16,
    .data_size = EM_DATA_SIZE_BASE,
    .alloc = (codec_alloc)em410x_16_alloc,
    .free = (codec_free)em410x_free,
    .get_data = (codec_get_data)em410x_get_data,
    .modulator = (modulator)em410x_modulator,
    .decoder =
        {
            .start = (decoder_start)em410x_decoder_start,
            .feed = (decoder_feed)em410x_decoder_feed,
        },
};

// Encode EM410X card number to T55xx blocks.
uint8_t em410x_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    uint64_t raw = em410x_raw_data(uid);
    blks[0] = T5577_EM410X_64_CONFIG;
    blks[1] = raw >> 32;
    blks[2] = raw & 0xffffffff;
    return EM_T55XX_BLOCK_COUNT;
}

uint8_t em410x_electra_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    uint64_t raw_data = em410x_raw_data(uid);
    uint64_t raw_epilogue = em410x_raw_epilogue(uid);

    blks[0] = T5577_EM410X_ELECTRA_CONFIG;
    blks[1] = raw_data >> 32;
    blks[2] = raw_data & 0xffffffff;
    blks[3] = raw_epilogue >> 32;
    blks[4] = raw_epilogue & 0xffffffff;

    return EM_T55XX_ELECTRA_BLOCK_COUNT;
}
