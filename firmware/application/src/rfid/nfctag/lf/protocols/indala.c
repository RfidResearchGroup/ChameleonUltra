#include "indala.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"
#include "utils/psk1.h"

#define NRF_LOG_MODULE_NAME indala
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#define INDALA_RAW_SIZE (64)  // 64-bit frame
#define INDALA_DATA_SIZE (8)  // 8 bytes stored

#define INDALA_T55XX_BLOCK_COUNT (3) // config + 2 data blocks

#define INDALA_PWM_ENTRIES (INDALA_RAW_SIZE * LF_PSK1_RF32_SUBCYCLES_PER_BIT)

static nrf_pwm_values_wave_form_t m_indala_pwm_seq_vals[INDALA_PWM_ENTRIES] = {};

static nrf_pwm_sequence_t m_indala_pwm_seq = {
    .values.p_wave_form = m_indala_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_indala_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

typedef struct {
    uint8_t data[INDALA_DATA_SIZE];
} indala_codec;

static indala_codec *indala_alloc(void) {
    indala_codec *d = malloc(sizeof(indala_codec));
    memset(d->data, 0, INDALA_DATA_SIZE);
    return d;
}

static void indala_free(indala_codec *d) {
    free(d);
}

static uint8_t *indala_get_data(indala_codec *d) {
    return d->data;
}

// Reading is done by indala_read() (reader/lf/lf_indala_data.c), not through this decoder.
static void indala_decoder_start(indala_codec *d, uint8_t format) {
    (void)d;
    (void)format;
}

static bool indala_decoder_feed(indala_codec *d, uint16_t val) {
    (void)d;
    (void)val;
    return false;
}

// buf is the 8-byte frame, MSB first on air. Same PSK1 RF/32 fc/2 air layer as
// IDTECK, so it uses the shared PSK1 sequence builder (utils/psk1.h).
static const nrf_pwm_sequence_t *indala_modulator(indala_codec *d, uint8_t *buf) {
    (void)d;

    size_t n = lf_psk1_build_sequence(buf, INDALA_RAW_SIZE,
                                      m_indala_pwm_seq_vals, INDALA_PWM_ENTRIES);
    m_indala_pwm_seq.length = (uint16_t)(n * 4);   // 4 uint16 fields per wave-form entry
    return &m_indala_pwm_seq;
}

const protocol indala = {
    .tag_type = TAG_TYPE_INDALA,
    .data_size = INDALA_DATA_SIZE,
    .alloc = (codec_alloc)indala_alloc,
    .free = (codec_free)indala_free,
    .get_data = (codec_get_data)indala_get_data,
    .modulator = (modulator)indala_modulator,
    .decoder =
        {
            .start = (decoder_start)indala_decoder_start,
            .feed = (decoder_feed)indala_decoder_feed,
        },
};

// Encode Indala 64-bit data to T55xx blocks
uint8_t indala_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    blks[0] = T5577_INDALA_64_CONFIG;
    blks[1] = (uid[0] << 24) | (uid[1] << 16) | (uid[2] << 8) | uid[3];
    blks[2] = (uid[4] << 24) | (uid[5] << 16) | (uid[6] << 8) | uid[7];
    return INDALA_T55XX_BLOCK_COUNT;
}
