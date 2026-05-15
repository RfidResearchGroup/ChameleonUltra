#include "idteck.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf_pwm.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"
#include "utils/psk1.h"

// IDTECK: 64-bit PSK1 frame at RF/32. The 32-bit fixed preamble 0x4944544B
// ("IDTK") occupies the first four bytes of the frame; the remaining four
// bytes are the card payload (a one-byte checksum followed by a 24-bit card
// number in a byte-reversed layout, matching the format used by common
// IDTECK readers; see cmdlfidteck.c in the Proxmark3 client for details).

#define IDTECK_PWM_ENTRIES        (IDTECK_BIT_COUNT * LF_PSK1_RF32_SUBCYCLES_PER_BIT)
#define IDTECK_T55XX_BLOCK_COUNT  (3)   // config word + 2 data blocks

static nrf_pwm_values_wave_form_t m_idteck_pwm_seq_vals[IDTECK_PWM_ENTRIES] = {};

static nrf_pwm_sequence_t m_idteck_pwm_seq = {
    .values.p_wave_form = m_idteck_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_idteck_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

static idteck_codec *idteck_alloc(void) {
    idteck_codec *d = malloc(sizeof(idteck_codec));
    memset(d->data, 0, IDTECK_DATA_SIZE);
    return d;
}

static void idteck_free(idteck_codec *d) {
    free(d);
}

static uint8_t *idteck_get_data(idteck_codec *d) {
    return d->data;
}

// PSK demodulation is not implemented. The tag-emulation ADC path runs at
// 125kHz and is envelope-filtered, so recovering subcarrier phase for read
// would require a dedicated decoder (edge-timing based). These stubs keep the
// protocol struct complete; a future change can fill them in without touching
// the struct layout.
static void idteck_decoder_start(idteck_codec *d, uint8_t format) {
    (void)d;
    (void)format;
}

static bool idteck_decoder_feed(idteck_codec *d, uint16_t val) {
    (void)d;
    (void)val;
    return false;
}

// buf is the 8-byte frame to transmit, MSB first on air. For standard IDTECK
// the first four bytes are the fixed preamble and the last four are the card
// payload; the CLI layer is responsible for composing them.
static const nrf_pwm_sequence_t *idteck_modulator(idteck_codec *d, uint8_t *buf) {
    (void)d;

    size_t n = lf_psk1_build_sequence(buf, IDTECK_BIT_COUNT,
                                      m_idteck_pwm_seq_vals, IDTECK_PWM_ENTRIES);
    m_idteck_pwm_seq.length = (uint16_t)(n * 4);   // 4 uint16 fields per wave-form entry
    return &m_idteck_pwm_seq;
}

const protocol idteck = {
    .tag_type = TAG_TYPE_IDTECK,
    .data_size = IDTECK_DATA_SIZE,
    .alloc = (codec_alloc)idteck_alloc,
    .free = (codec_free)idteck_free,
    .get_data = (codec_get_data)idteck_get_data,
    .modulator = (modulator)idteck_modulator,
    .decoder =
        {
            .start = (decoder_start)idteck_decoder_start,
            .feed = (decoder_feed)idteck_decoder_feed,
        },
};

// T5577 writer: block 0 holds the PSK1 RF/32 configuration, blocks 1-2 hold
// the 64-bit frame big-endian.
uint8_t idteck_t55xx_writer(uint8_t *uid, uint32_t *blks) {
    uint32_t hi = 0, lo = 0;
    for (int i = 0; i < 4; i++) hi = (hi << 8) | uid[i];
    for (int i = 4; i < 8; i++) lo = (lo << 8) | uid[i];
    blks[0] = T5577_IDTECK_CONFIG;
    blks[1] = hi;
    blks[2] = lo;
    return IDTECK_T55XX_BLOCK_COUNT;
}
