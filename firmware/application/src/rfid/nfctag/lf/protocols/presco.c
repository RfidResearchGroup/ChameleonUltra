#include "presco.h"

#include <stdlib.h>

#include "t55xx.h"

// Presco: Manchester RF/32, 128 bits MSB first, followed by a sequence
// terminator. Frame layout and terminator reference: Proxmark3 cmdlfpresco.c
// and armsrc/lfops.c (stAskSimBit). Store all 32 code bits, including the
// byte between the site code and user code whose meaning is unknown.
#define PRESCO_HEADER 0x10D00000UL
#define PRESCO_BIT_TICKS 32
#define PRESCO_PWM_ENTRIES (PRESCO_FRAME_BITS + 3)

static nrf_pwm_values_wave_form_t m_presco_values[PRESCO_PWM_ENTRIES];
static nrf_pwm_sequence_t m_presco_sequence = {
    .values.p_wave_form = m_presco_values,
    .length = NRF_PWM_VALUES_LENGTH(m_presco_values),
    .repeats = 0,
    .end_delay = 0,
};

static void *presco_alloc(void) {
    return calloc(1, PRESCO_DATA_SIZE);
}

static void presco_free(void *codec) {
    free(codec);
}

static uint8_t *presco_get_data(void *codec) {
    return codec;
}

// Reader support is separate from emulation; these callbacks do not accept
// any input until an acquisition path and decoder have been implemented.
static void presco_decoder_start(void *codec, uint8_t format) {
    (void)codec;
    (void)format;
}

static bool presco_decoder_feed(void *codec, uint16_t value) {
    (void)codec;
    (void)value;
    return false;
}

uint8_t presco_t55xx_writer(uint8_t *code, uint32_t *blocks) {
    blocks[0] = T5577_PRESCO_CONFIG;
    blocks[1] = PRESCO_HEADER;
    blocks[2] = 0;
    blocks[3] = 0;
    blocks[4] = ((uint32_t)code[0] << 24) | ((uint32_t)code[1] << 16) |
                ((uint32_t)code[2] << 8) | code[3];
    return 5;
}

static nrf_pwm_sequence_t *presco_modulator(void *codec, uint8_t *code) {
    (void)codec;
    uint32_t blocks[5];
    presco_t55xx_writer(code, blocks);
    for (size_t i = 0; i < PRESCO_FRAME_BITS; i++) {
        bool bit = (blocks[1 + i / 32] >> (31 - i % 32)) & 1u;
        // Same Manchester polarity as Viking: bit 1 is high then low;
        // bit 0 is low then high. PWM polarity is encoded in bit 15.
        m_presco_values[i].channel_0 = (bit ? 0x8000u : 0) | (PRESCO_BIT_TICKS / 2);
        m_presco_values[i].counter_top = PRESCO_BIT_TICKS;
    }

    // Terminator: .5 high, .5 low, 1.5 high, .5 low, 1 high (4 bits).
    // A compare above COUNTERTOP holds the final level throughout the period.
    m_presco_values[PRESCO_FRAME_BITS].channel_0 = 0x8000u | 16;
    m_presco_values[PRESCO_FRAME_BITS].counter_top = 32;
    m_presco_values[PRESCO_FRAME_BITS + 1].channel_0 = 0x8000u | 48;
    m_presco_values[PRESCO_FRAME_BITS + 1].counter_top = 64;
    m_presco_values[PRESCO_FRAME_BITS + 2].channel_0 = 0x8000u | 33;
    m_presco_values[PRESCO_FRAME_BITS + 2].counter_top = 32;
    return &m_presco_sequence;
}

const protocol presco = {
    .tag_type = TAG_TYPE_PRESCO,
    .data_size = PRESCO_DATA_SIZE,
    .alloc = presco_alloc,
    .free = presco_free,
    .get_data = presco_get_data,
    .decoder = {.start = presco_decoder_start, .feed = presco_decoder_feed},
    .modulator = presco_modulator,
};
