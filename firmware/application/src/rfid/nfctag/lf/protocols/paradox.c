#include "paradox.h"

#include <stdlib.h>
#include <string.h>

#include "nrf_pwm.h"
#include "tag_base_type.h"
#include "utils/fskdemod.h"

// Paradox is FSK2a, RF/50, with 96 on-air bits. The first eight bits are the
// unencoded 00001111 preamble; the remaining 88 bits contain Manchester data.
#define PARADOX_FRAME_BITS 96

// These timings match the established HID Prox FSK2a carrier pair. With the
// 125 kHz PWM base clock they produce 10 carrier cycles in 50 ticks and 8
// carrier cycles in 48 ticks, respectively.
#define PARADOX_PWM_LO_FREQ_LOOP 5
#define PARADOX_PWM_LO_FREQ_TOP_VALUE 10
#define PARADOX_PWM_HI_FREQ_LOOP 6
#define PARADOX_PWM_HI_FREQ_TOP_VALUE 8

typedef struct {
    fsk_t *modem;
    uint8_t bits[PARADOX_FRAME_BITS * 2];
    uint16_t bit_length;
    uint8_t data[PARADOX_DATA_SIZE];
} paradox_codec_t;

static nrf_pwm_values_wave_form_t
    m_paradox_pwm_seq_vals[PARADOX_FRAME_BITS * PARADOX_PWM_HI_FREQ_LOOP] = {};

static nrf_pwm_sequence_t m_paradox_pwm_seq = {
    .values.p_wave_form = m_paradox_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_paradox_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

// Encode 16 input bits MSB-first using IEEE Manchester: 0 -> 01, 1 -> 10.
static uint32_t manchester_encode_u16(uint16_t value) {
    uint32_t encoded = 0;
    for (uint8_t i = 0; i < 16; i++) {
        encoded <<= 2;
        encoded |= (value & (uint16_t)(0x8000u >> i)) ? 0x02u : 0x01u;
    }
    return encoded;
}

// CRC-8/MAXIM-DOW: poly=0x31 (reflected 0x8c), init=0, refin/refout=true.
static uint8_t crc8_maxim(const uint8_t *data, size_t length) {
    uint8_t crc = 0;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? (uint8_t)((crc >> 1) ^ 0x8cu)
                             : (uint8_t)(crc >> 1);
        }
    }
    return crc;
}

bool paradox_encode_raw(uint8_t facility_code, uint16_t card_number,
                        uint8_t raw[PARADOX_RAW_SIZE]) {
    if (raw == NULL) {
        return false;
    }

    // One extra byte is required while the Manchester stream is shifted left
    // by four bits after the checksum has been calculated.
    uint8_t frame[PARADOX_RAW_SIZE + 1] = {0};
    frame[0] = 0x0f;
    frame[1] = 0x05;
    frame[2] = 0x55;
    frame[3] = 0x55;

    uint32_t encoded = manchester_encode_u16(facility_code);
    frame[4] = (uint8_t)(encoded >> 8);
    frame[5] = (uint8_t)encoded;

    encoded = manchester_encode_u16(card_number);
    frame[6] = (uint8_t)(encoded >> 24);
    frame[7] = (uint8_t)(encoded >> 16);
    frame[8] = (uint8_t)(encoded >> 8);
    frame[9] = (uint8_t)encoded;

    const uint8_t checksum = (uint8_t)(crc8_maxim(frame + 1, 9) ^ 0x06u);
    encoded = manchester_encode_u16(checksum);
    frame[10] = (uint8_t)(encoded >> 8);
    frame[11] = (uint8_t)encoded;

    // The data following the preamble begins four bits into frame[1]. Align it
    // to the transmitted byte stream and append the terminal 1010 pattern.
    for (uint8_t i = 1; i < PARADOX_RAW_SIZE; i++) {
        frame[i] = (uint8_t)((frame[i] << 4) | (frame[i + 1] >> 4));
    }
    frame[PARADOX_RAW_SIZE - 1] |= 0x0au;

    memcpy(raw, frame, PARADOX_RAW_SIZE);
    return true;
}

static void *paradox_codec_alloc(void) {
    paradox_codec_t *codec = calloc(1, sizeof(paradox_codec_t));
    if (codec != NULL) {
        codec->modem = fsk_alloc(FSK_BITRATE_HID);
        if (codec->modem == NULL) {
            free(codec);
            return NULL;
        }
    }
    return codec;
}

static void paradox_codec_free(void *codec) {
    paradox_codec_t *paradox_codec = codec;
    if (paradox_codec != NULL) {
        fsk_free(paradox_codec->modem);
        free(paradox_codec);
    }
}

static uint8_t *paradox_get_data(void *codec) {
    return ((paradox_codec_t *)codec)->data;
}

static void paradox_decoder_start(void *codec, uint8_t format_hint) {
    (void)format_hint;
    paradox_codec_t *paradox_codec = codec;
    paradox_codec->bit_length = 0;
    paradox_codec->modem->c = 0;
    memset(paradox_codec->modem->samples, 0,
           sizeof(paradox_codec->modem->samples));
    memset(paradox_codec->bits, 0, sizeof(paradox_codec->bits));
    memset(paradox_codec->data, 0, sizeof(paradox_codec->data));
}

static bool paradox_decode_frame(paradox_codec_t *codec, uint16_t offset,
                                 bool inverted) {
    uint8_t raw[PARADOX_RAW_SIZE] = {0};
    for (uint16_t i = 0; i < PARADOX_FRAME_BITS; i++) {
        uint8_t bit = codec->bits[offset + i] ^ (inverted ? 1u : 0u);
        raw[i / 8] |= (uint8_t)(bit << (7 - (i % 8)));
    }
    if (raw[0] != 0x0f) {
        return false;
    }

    uint64_t decoded = 0;
    for (uint16_t i = 8; i < PARADOX_FRAME_BITS; i += 2) {
        uint8_t first = (raw[i / 8] >> (7 - (i % 8))) & 1u;
        uint8_t second = (raw[(i + 1) / 8] >> (7 - ((i + 1) % 8))) & 1u;
        if (first == second) {
            return false;
        }
        decoded = (decoded << 1) | (first && !second ? 1u : 0u);
    }

    // The Manchester payload is 10 zero bits, FC, CN, CRC, then two one bits.
    if ((decoded >> 34) != 0 || (decoded & 0x03u) != 0x03u) {
        return false;
    }
    uint8_t facility_code = (uint8_t)(decoded >> 26);
    uint16_t card_number = (uint16_t)(decoded >> 10);

    uint8_t expected[PARADOX_RAW_SIZE];
    paradox_encode_raw(facility_code, card_number, expected);
    if (memcmp(raw, expected, sizeof(raw)) != 0) {
        return false;
    }

    codec->data[0] = facility_code;
    codec->data[1] = (uint8_t)(card_number >> 8);
    codec->data[2] = (uint8_t)card_number;
    codec->data[3] = 0;
    return true;
}

static bool paradox_decoder_feed(void *codec, uint16_t value) {
    paradox_codec_t *paradox_codec = codec;
    if (paradox_codec == NULL || paradox_codec->modem == NULL) {
        return false;
    }

    bool bit;
    if (!fsk_feed(paradox_codec->modem, value, &bit)) {
        return false;
    }

    if (paradox_codec->bit_length == sizeof(paradox_codec->bits)) {
        memmove(paradox_codec->bits, paradox_codec->bits + PARADOX_FRAME_BITS,
                sizeof(paradox_codec->bits) - PARADOX_FRAME_BITS);
        paradox_codec->bit_length -= PARADOX_FRAME_BITS;
    }
    paradox_codec->bits[paradox_codec->bit_length++] = bit ? 1u : 0u;

    if (paradox_codec->bit_length < PARADOX_FRAME_BITS) {
        return false;
    }
    uint16_t offset = paradox_codec->bit_length - PARADOX_FRAME_BITS;
    return paradox_decode_frame(paradox_codec, offset, false) ||
           paradox_decode_frame(paradox_codec, offset, true);
}

static inline void paradox_emit_bit(uint16_t *index, bool bit) {
    if (!bit) {
        for (uint8_t i = 0; i < PARADOX_PWM_HI_FREQ_LOOP; i++) {
            m_paradox_pwm_seq_vals[*index].channel_0 = PARADOX_PWM_HI_FREQ_TOP_VALUE / 2;
            m_paradox_pwm_seq_vals[*index].counter_top = PARADOX_PWM_HI_FREQ_TOP_VALUE;
            (*index)++;
        }
    } else {
        for (uint8_t i = 0; i < PARADOX_PWM_LO_FREQ_LOOP; i++) {
            m_paradox_pwm_seq_vals[*index].channel_0 = PARADOX_PWM_LO_FREQ_TOP_VALUE / 2;
            m_paradox_pwm_seq_vals[*index].counter_top = PARADOX_PWM_LO_FREQ_TOP_VALUE;
            (*index)++;
        }
    }
}

static nrf_pwm_sequence_t *paradox_modulator(void *codec, uint8_t *buffer) {
    (void)codec;

    uint8_t raw[PARADOX_RAW_SIZE];
    paradox_encode_raw(buffer[0], (uint16_t)(((uint16_t)buffer[1] << 8) | buffer[2]), raw);

    uint16_t index = 0;
    for (uint8_t byte = 0; byte < PARADOX_RAW_SIZE; byte++) {
        for (int8_t bit = 7; bit >= 0; bit--) {
            paradox_emit_bit(&index, ((raw[byte] >> bit) & 1u) != 0);
        }
    }

    m_paradox_pwm_seq.length = (uint16_t)(index * 4u);
    return &m_paradox_pwm_seq;
}

const protocol paradox = {
    .tag_type = TAG_TYPE_PARADOX,
    .data_size = PARADOX_DATA_SIZE,
    .alloc = paradox_codec_alloc,
    .free = paradox_codec_free,
    .get_data = paradox_get_data,
    .decoder = {
        .start = paradox_decoder_start,
        .feed = paradox_decoder_feed,
    },
    .modulator = paradox_modulator,
};
