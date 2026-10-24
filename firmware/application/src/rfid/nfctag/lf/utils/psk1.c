#include "psk1.h"

#include "nordic_common.h"

// Extract bit at index `bit_idx` from a MSB-first bit stream stored in
// frame_bytes. bit_idx=0 is the MSB of frame_bytes[0].
static inline bool read_bit_msb_first(const uint8_t *frame_bytes, size_t bit_idx) {
    return (frame_bytes[bit_idx / 8] >> (7 - (bit_idx % 8))) & 1U;
}

// Extract the last bit of the frame (LSB of the last byte that contains a
// bit). For frames whose bit_count is not a multiple of 8 this still points
// at the final transmitted bit.
static inline bool read_last_bit(const uint8_t *frame_bytes, size_t bit_count) {
    size_t last_idx = bit_count - 1;
    return read_bit_msb_first(frame_bytes, last_idx);
}

size_t lf_psk1_build_sequence(const uint8_t *frame_bytes,
                              size_t bit_count,
                              nrf_pwm_values_wave_form_t *out_buf,
                              size_t out_capacity) {
    size_t required = bit_count * LF_PSK1_RF32_SUBCYCLES_PER_BIT;
    if (required > out_capacity) {
        return 0;
    }

    bool phase = false;
    bool last_bit = read_last_bit(frame_bytes, bit_count);

    size_t k = 0;
    for (size_t bit_idx = 0; bit_idx < bit_count; bit_idx++) {
        bool cur_bit = read_bit_msb_first(frame_bytes, bit_idx);
        if (cur_bit != last_bit) {
            phase = !phase;
        }
        last_bit = cur_bit;

        uint16_t pol = phase ? (1U << 15) : 0;
        for (size_t c = 0; c < LF_PSK1_RF32_SUBCYCLES_PER_BIT; c++) {
            out_buf[k].channel_0 = pol | LF_PSK1_SUBCARRIER_DUTY;
            out_buf[k].channel_1 = 0;
            out_buf[k].channel_2 = 0;
            out_buf[k].counter_top = LF_PSK1_SUBCARRIER_TOP;
            k++;
        }
    }

    return k;
}

static nrf_pwm_values_wave_form_t m_psk1_seq_vals[LF_PSK1_MAX_FRAME_BITS * LF_PSK1_RF32_SUBCYCLES_PER_BIT] = {};

static nrf_pwm_sequence_t m_psk1_seq = {
    .values.p_wave_form = m_psk1_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_psk1_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

const nrf_pwm_sequence_t *lf_psk1_sequence(const uint8_t *frame_bytes, size_t bit_count) {
    size_t n = lf_psk1_build_sequence(frame_bytes, bit_count, m_psk1_seq_vals, ARRAY_SIZE(m_psk1_seq_vals));
    m_psk1_seq.length = (uint16_t)(n * 4);   // 4 uint16 fields per wave-form entry
    return &m_psk1_seq;
}
