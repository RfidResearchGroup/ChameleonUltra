#include "pac.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "nrf_pwm.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"

#define PAC_DATA_SIZE       8   // 8-byte ASCII card ID

// NRZ at RF/32: 32 carrier cycles per bit.
// With SAADC sampling at 1 sample per carrier cycle, 32 samples = 1 bit.
#define PAC_RF_PER_BIT      32
#define PAC_HALF_BIT        16  // Half-bit for rounding interval → nbits
#define PAC_MAX_BITS_RUN    20  // Max consecutive same-polarity bits we accept

// PAC frame is exactly 128 bits on T55xx (4 blocks × 32 bits):
//   8-bit sync marker (0xFF) + 12 × 10-bit UART frames = 128 bits
#define PAC_FRAME_BITS      128
#define PAC_PREAMBLE_BITS   19

// Preamble: 1111111100100000010 (19 bits) = 0x7F902
#define PAC_PREAMBLE        0x7F902UL
#define PAC_PREAMBLE_INV    0x006FDUL  // Bitwise inverse masked to 19 bits

#define PAC_UART_FRAME_BITS 10
#define PAC_PAYLOAD_BYTES   12  // STX + '2' + '0' + 8 card ID + XOR checksum
#define PAC_STX             0x02

// ADC demodulation parameters
#define PAC_PRESCAN_SAMPLES 128  // Samples for raw min/max detection (spike cap)
#define PAC_WARMUP_SAMPLES  600  // Samples for threshold calibration (~5ms)
#define PAC_SPIKE_MULT      3    // Samples > raw_min * SPIKE_MULT are spike transients
#define PAC_THRESH_FUZZ     75   // PM3-style: threshold at 75% of signal range

typedef struct {
    // NRZ shift register (128 bits)
    uint64_t raw_hi;        // upper 64 bits
    uint64_t raw_lo;        // lower 64 bits
    bool polarity;          // current NRZ level (toggled on each edge)
    uint16_t bit_count;     // total bits shifted in (capped at PAC_FRAME_BITS)
    uint8_t card_id[PAC_DATA_SIZE];

    // ADC → NRZ demodulation state (per-sample threshold with dead zone)
    uint32_t total_samples; // total samples processed
    int16_t raw_min;        // minimum raw sample seen (for spike detection)
    int32_t spike_cap;      // clip level: raw values above this are transients
    int16_t clip_max;       // max of clipped samples during warmup
    int16_t clip_min;       // min of clipped samples during warmup
    int16_t thresh_high;    // above this → bit=1
    int16_t thresh_low;     // below this → bit=0, between → keep previous
    bool adc_state;         // current demodulated binary level
    bool has_signal;        // true after first threshold crossing
    uint32_t sample_count;  // samples since last transition
} pac_codec;

// Shift one bit into the 128-bit register.
static void shift_bit(pac_codec *d, bool bit) {
    d->raw_hi = (d->raw_hi << 1) | (d->raw_lo >> 63);
    d->raw_lo = (d->raw_lo << 1) | (bit ? 1 : 0);
}

// Extract a single bit from the 128-bit register.
// Position 0 = MSB of raw_hi (oldest), position 127 = LSB of raw_lo (newest).
static bool get_bit(pac_codec *d, uint16_t pos) {
    if (pos < 64) {
        return (d->raw_hi >> (63 - pos)) & 1;
    }
    return (d->raw_lo >> (127 - pos)) & 1;
}

// Decode a 10-bit UART frame at bit position 'start'.
// Frame: start(0) + 7 data bits LSB-first + odd parity + stop(1).
static int decode_uart_byte(pac_codec *d, uint16_t start, bool inverted) {
    #define RD(pos) (inverted ? !get_bit(d, (pos)) : get_bit(d, (pos)))

    if (RD(start)) {
        return -1;
    }

    uint8_t byte_val = 0;
    uint8_t ones = 0;
    for (int i = 0; i < 7; i++) {
        if (RD(start + 1 + i)) {
            byte_val |= (1 << i);
            ones++;
        }
    }

    if (RD(start + 8)) {
        ones++;
    }
    if ((ones & 1) == 0) {
        return -1;
    }

    if (!RD(start + 9)) {
        return -1;
    }

    #undef RD
    return byte_val;
}

// Check if the 128-bit register contains a valid PAC frame.
static bool try_decode_frame(pac_codec *d, bool inverted) {
    uint32_t preamble = 0;
    for (int i = 0; i < PAC_PREAMBLE_BITS; i++) {
        preamble = (preamble << 1) | (get_bit(d, i) ? 1 : 0);
    }

    uint32_t expected = inverted ? PAC_PREAMBLE_INV : PAC_PREAMBLE;
    if (preamble != expected) {
        return false;
    }

    uint8_t decoded[PAC_PAYLOAD_BYTES];
    for (int i = 0; i < PAC_PAYLOAD_BYTES; i++) {
        uint16_t frame_start = 8 + i * PAC_UART_FRAME_BITS;
        int val = decode_uart_byte(d, frame_start, inverted);
        if (val < 0) {
            return false;
        }
        decoded[i] = (uint8_t)val;
    }

    if (decoded[0] != PAC_STX) {
        return false;
    }

    uint8_t xor_check = 0;
    for (int i = 3; i < 3 + PAC_DATA_SIZE; i++) {
        xor_check ^= decoded[i];
    }
    if (xor_check != decoded[11]) {
        return false;
    }

    memcpy(d->card_id, &decoded[3], PAC_DATA_SIZE);
    return true;
}

// Process a demodulated NRZ edge interval (in samples = carrier cycles).
static bool pac_process_interval(pac_codec *d, uint32_t interval) {
    uint32_t nbits = (interval + PAC_HALF_BIT) / PAC_RF_PER_BIT;

    if (nbits < 1 || nbits > PAC_MAX_BITS_RUN) {
        d->raw_hi = 0;
        d->raw_lo = 0;
        d->polarity = false;
        d->bit_count = 0;
        return false;
    }

    for (uint32_t i = 0; i < nbits; i++) {
        shift_bit(d, d->polarity);
        if (d->bit_count < PAC_FRAME_BITS) {
            d->bit_count++;
        }
        if (d->bit_count >= PAC_FRAME_BITS) {
            if (try_decode_frame(d, false) || try_decode_frame(d, true)) {
                return true;
            }
        }
    }

    d->polarity = !d->polarity;
    return false;
}

static pac_codec *pac_alloc(void) {
    pac_codec *codec = malloc(sizeof(pac_codec));
    return codec;
}

static void pac_free(pac_codec *d) {
    free(d);
}

static uint8_t *pac_get_data(pac_codec *d) {
    return d->card_id;
}

static void pac_decoder_start(pac_codec *d, uint8_t format) {
    memset(d, 0, sizeof(pac_codec));
    d->raw_min = 32767;        // INT16_MAX: first sample updates it
    d->spike_cap = 0x7FFFFFFF; // INT32_MAX: no capping until prescan completes
    d->clip_max = -32768;      // INT16_MIN: first clipped sample updates it
    d->clip_min = 32767;       // INT16_MAX: first clipped sample updates it
}

// Feed a raw ADC sample (one per carrier cycle at 125kHz).
// PM3-inspired approach: clip spikes, then per-sample threshold with dead zone.
// No moving average — edges are detected directly on clipped samples, giving
// much tighter timing than the ~16 sample group delay of a moving average.
static bool pac_decoder_feed(pac_codec *d, uint16_t raw_sample) {
    int16_t sample = (int16_t)raw_sample;
    d->total_samples++;

    // Phase 1: Prescan — track raw minimum to characterize data levels.
    // The NRZ data levels are the lowest values; spikes are 5-15x higher.
    if (d->total_samples <= PAC_PRESCAN_SAMPLES) {
        if (sample < d->raw_min && sample > 0) {
            d->raw_min = sample;
        }
        if (d->total_samples == PAC_PRESCAN_SAMPLES) {
            d->spike_cap = (int32_t)d->raw_min * PAC_SPIKE_MULT;
        }
        return false;
    }

    // Clip spikes: LC ringing transients are replaced with the cap level.
    if (sample > d->spike_cap) {
        sample = d->spike_cap;
    }

    uint32_t warmup_samples = d->total_samples - PAC_PRESCAN_SAMPLES;

    // Phase 2: Warmup — track min/max of clipped samples to find NRZ levels.
    if (warmup_samples <= PAC_WARMUP_SAMPLES) {
        if (sample > d->clip_max) d->clip_max = sample;
        if (sample < d->clip_min) d->clip_min = sample;
        if (warmup_samples == PAC_WARMUP_SAMPLES) {
            // PM3-style thresholds: 75% fuzz creates a dead zone between levels.
            // high = 75% of peak, low = bottom + 25% of range.
            int16_t range = d->clip_max - d->clip_min;
            d->thresh_high = d->clip_min + (range * PAC_THRESH_FUZZ) / 100;
            d->thresh_low = d->clip_min + (range * (100 - PAC_THRESH_FUZZ)) / 100;
        }
        return false;
    }

    // Phase 3: Per-sample threshold with dead zone (PM3 nrzRawDemod style).
    // Values above thresh_high → 1, below thresh_low → 0, between → keep previous.
    // No moving average means near-zero edge timing jitter.
    d->sample_count++;

    bool new_state = d->adc_state;
    if (sample >= d->thresh_high) {
        new_state = true;
    } else if (sample <= d->thresh_low) {
        new_state = false;
    } else {
        return false; // Dead zone: no state change
    }

    if (!d->has_signal) {
        d->has_signal = true;
        d->adc_state = new_state;
        d->sample_count = 0;
        return false;
    }

    if (new_state == d->adc_state) {
        return false;
    }

    // Transition detected — process the interval
    uint32_t interval = d->sample_count;
    d->sample_count = 0;
    d->adc_state = new_state;

    return pac_process_interval(d, interval);
}

// --- Modulator (emulation) ---

static nrf_pwm_values_wave_form_t m_pac_pwm_seq_vals[PAC_FRAME_BITS] = {};

static const nrf_pwm_sequence_t m_pac_pwm_seq = {
    .values.p_wave_form = m_pac_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_pac_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

// Build the 128-bit NRZ bitstream from 8-byte card ID.
// Frame: 0xFF sync (8 bits) + 12 × 10-bit UART frames = 128 bits.
// UART frame: start(0) + 7 data bits LSB-first + odd parity + stop(1).
// Payload bytes: STX(0x02), '2', '0', card_id[0..7], XOR checksum.
static void pac_build_bitstream(const uint8_t *card_id, uint8_t *bits_out) {
    uint8_t payload[PAC_PAYLOAD_BYTES];
    payload[0] = PAC_STX;
    payload[1] = '2';
    payload[2] = '0';
    memcpy(&payload[3], card_id, PAC_DATA_SIZE);

    // XOR checksum over card ID bytes (indices 3..10)
    uint8_t xor_check = 0;
    for (int i = 3; i < 3 + PAC_DATA_SIZE; i++) {
        xor_check ^= payload[i];
    }
    payload[11] = xor_check;

    int bit_pos = 0;

    // 8-bit sync marker: 0xFF (all ones)
    for (int i = 0; i < 8; i++) {
        bits_out[bit_pos++] = 1;
    }

    // 12 UART frames
    for (int f = 0; f < PAC_PAYLOAD_BYTES; f++) {
        uint8_t byte_val = payload[f];

        // Start bit (0)
        bits_out[bit_pos++] = 0;

        // 7 data bits, LSB first
        uint8_t ones = 0;
        for (int i = 0; i < 7; i++) {
            uint8_t bit = (byte_val >> i) & 1;
            bits_out[bit_pos++] = bit;
            ones += bit;
        }

        // Odd parity: set so total ones (data + parity) is odd
        uint8_t parity = (ones & 1) ? 0 : 1;
        bits_out[bit_pos++] = parity;

        // Stop bit (1)
        bits_out[bit_pos++] = 1;
    }
}

static const nrf_pwm_sequence_t *pac_modulator(pac_codec *d, uint8_t *buf) {
    uint8_t bits[PAC_FRAME_BITS];
    pac_build_bitstream(buf, bits);

    // NRZ: output must be CONSTANT within each bit period (no mid-bit transition).
    // Per nRF52840 PS: compare = 0 → pin held LOW; compare >= counter_top → pin held HIGH.
    // No polarity bits needed — avoids edge-case ambiguity with compare = 0.
    for (int i = 0; i < PAC_FRAME_BITS; i++) {
        m_pac_pwm_seq_vals[i].channel_0 = bits[i] ? PAC_RF_PER_BIT : 0;
        m_pac_pwm_seq_vals[i].counter_top = PAC_RF_PER_BIT;
    }
    return &m_pac_pwm_seq;
}

#define PAC_T55XX_BLOCK_COUNT 5  // 1 config + 4 data blocks

uint8_t pac_t55xx_writer(uint8_t *data, uint32_t *blks) {
    uint8_t bits[PAC_FRAME_BITS];
    pac_build_bitstream(data, bits);

    blks[0] = T5577_PAC_CONFIG;
    for (int b = 0; b < 4; b++) {
        uint32_t word = 0;
        for (int i = 0; i < 32; i++) {
            word = (word << 1) | bits[b * 32 + i];
        }
        blks[b + 1] = word;
    }
    return PAC_T55XX_BLOCK_COUNT;
}

const protocol pac = {
    .tag_type = TAG_TYPE_PAC,
    .data_size = PAC_DATA_SIZE,
    .alloc = (codec_alloc)pac_alloc,
    .free = (codec_free)pac_free,
    .get_data = (codec_get_data)pac_get_data,
    .modulator = (modulator)pac_modulator,
    .decoder =
        {
            .start = (decoder_start)pac_decoder_start,
            .feed = (decoder_feed)pac_decoder_feed,
        },
};
