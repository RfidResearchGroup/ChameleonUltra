#include "ioprox.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "fskdemod.h"
#include "t55xx.h"
#include "tag_base_type.h"

#define IOPROX_SOF (0x1d)
#define IOPROX_T55XX_BLOCK_COUNT (3)
#define DEMOD_BUFFER_SIZE (32)
#define IOPROX_RAW_SIZE (96)

// NOTE: These LF_FSK2a_* defines are intentionally local to this .c file
// to allow per-protocol timing tuning (do not move to a shared header).
#define LF_FSK2a_PWM_LO_FREQ_LOOP (6)
#define LF_FSK2a_PWM_LO_FREQ_TOP_VALUE (11)
#define LF_FSK2a_PWM_HI_FREQ_LOOP (8)
#define LF_FSK2a_PWM_HI_FREQ_TOP_VALUE (8)

static nrf_pwm_values_wave_form_t m_ioprox_pwm_seq_vals[IOPROX_RAW_SIZE * 6] = {};

nrf_pwm_sequence_t m_ioprox_pwm_seq = {
    .values.p_wave_form = m_ioprox_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_ioprox_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

void ioprox_reset_bits(ioprox_codec_t *d) {
    d->bit_len = 0;
}

static inline void push_bit(ioprox_codec_t *d, uint8_t bit)
{
    if (d->bit_len < IOPROX_MAX_BITS) {
        d->bits[d->bit_len++] = bit;
        return;
    }
    // Buffer full: drop the oldest bit and append the new one
    memmove(d->bits, d->bits + 1, IOPROX_MAX_BITS - 1);
    d->bits[IOPROX_MAX_BITS - 1] = bit;
}

static inline uint8_t get_bit_inv(const uint8_t *bits, uint16_t pos, bool inv)
{
    uint8_t b = bits[pos] & 1u;
    return inv ? (uint8_t)(b ^ 1u) : b;
}

// Converts a bit array to a 32-bit big-endian integer.
static inline uint32_t bytebits_to_byte(const uint8_t *bits, uint16_t len)
{
    uint32_t val = 0;
    for (uint16_t i = 0; i < len; i++) {
        val = (val << 1) | (bits[i] & 1u);
    }
    return val;
}

// Reads 8 bits MSB-first from bits[start_pos], optionally inverting each bit.
static inline uint8_t bytebits_to_u8_msb_inv(const uint8_t *bits, uint16_t start_pos, bool inv)
{
    uint8_t v = 0;
    for (int i = 0; i < 8; i++) {
        v = (uint8_t)((v << 1) | get_bit_inv(bits, (uint16_t)(start_pos + i), inv));
    }
    return v;
}

// Unpacks a raw 8-byte card frame into the codec bit buffer (MSB-first).
bool ioprox_raw8_to_bits(const uint8_t *raw8, ioprox_codec_t *d) {
    d->bit_len = 0;
    for (int i = 0; i < 8; i++) {
        uint8_t byte = raw8[i];
        for (int j = 0; j < 8; j++) {
            d->bits[d->bit_len++] = (byte >> (7 - j)) & 0x01;
        }
    }
    return true;
}

// ioProx checksum: 0xFF - (sum(b1..b5) & 0xFF)
static inline uint8_t ioprox_checksum5(uint8_t b1, uint8_t b2, uint8_t b3, uint8_t b4, uint8_t b5)
{
    uint16_t sum = (uint16_t)b1 + b2 + b3 + b4 + b5;
    return (uint8_t)(0xFFu - (uint8_t)(sum & 0xFFu));
}

// Returns true if 10 starting bits form a valid ioProx preamble.
// Preamble is 9 zeros followed by 1 one (inverted when inv=true).
static bool preamble_match(const uint8_t *d, uint16_t off, bool inv)
{
    for (int k = 0; k < 9; k++) {
        if ((d[off + k] & 1u) != (inv ? 1u : 0u)) return false;
    }
    return ((d[off + 9] & 1u) == (inv ? 0u : 1u));
}

// Decodes a 64-bit ioProx frame starting at bit index idx into d->data.
// Returns true only if the checksum passes.
//
// Frame layout (8+1 bit-framing, 7 groups):
//   b0: SOF byte (0x00)
//   b1: 0xF0 (fixed header)
//   b2: facility code
//   b3: version
//   b4: card number high byte
//   b5: card number low byte
//   b6: checksum (0xFF - sum(b1..b5))
//
// Output d->data layout (16 bytes):
//   [0]     version
//   [1]     facility code
//   [2-3]   card number (big-endian)
//   [4-11]  raw8 frame bytes (for debugging and storage)
//   [12-15] reserved (0x00)
static bool decode_and_pack(ioprox_codec_t *d, uint16_t idx, bool inv)
{
    uint8_t  b1, b2, b3, b4, b5, b6;
    uint16_t number;
    uint32_t raw_block1;
    uint32_t raw_block2;

    b1 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 9),  inv);
    b2 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 18), inv);
    b3 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 27), inv);
    b4 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 36), inv);
    b5 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 45), inv);
    b6 = bytebits_to_u8_msb_inv(d->bits, (uint16_t)(idx + 54), inv);

    if (ioprox_checksum5(b1, b2, b3, b4, b5) != b6) {
        return false;
    }

    number     = (uint16_t)(((uint16_t)b4 << 8) | b5);
    raw_block1 = bytebits_to_byte(d->bits + idx,      32);
    raw_block2 = bytebits_to_byte(d->bits + idx + 32, 32);

    memset(d->data, 0, sizeof(d->data));

    d->data[0] = b3;                       // version
    d->data[1] = b2;                       // facility code
    d->data[2] = (uint8_t)(number >> 8);   // card number high byte
    d->data[3] = (uint8_t)(number & 0xFF); // card number low byte

    // Raw frame bytes (human-readable, stable across re-reads)
    d->data[4]  = (uint8_t)(raw_block1 >> 24);
    d->data[5]  = (uint8_t)(raw_block1 >> 16);
    d->data[6]  = (uint8_t)(raw_block1 >>  8);
    d->data[7]  = (uint8_t)(raw_block1);
    d->data[8]  = (uint8_t)(raw_block2 >> 24);
    d->data[9]  = (uint8_t)(raw_block2 >> 16);
    d->data[10] = (uint8_t)(raw_block2 >>  8);
    d->data[11] = (uint8_t)(raw_block2);
    // d->data[12..15] zeroed by memset above

    return true;
}

// Decodes a raw 8-byte ioProx card frame into the 16-byte output buffer.
// Returns true if the frame checksum is valid.
bool ioprox_decode_raw_to_data(const uint8_t *raw8, uint8_t *output) {
    ioprox_codec_t codec;
    memset(&codec, 0, sizeof(codec));
    ioprox_raw8_to_bits(raw8, &codec);
    if (decode_and_pack(&codec, 0, false)) {
        memcpy(output, codec.data, 16);
        return true;
    }
    return false;
}

// Writes 8 bits of v MSB-first into bits[] starting at position pos.
static void write_bits_msb(uint8_t *bits, uint16_t pos, uint8_t v)
{
    for (uint8_t i = 0; i < 8; i++) {
        bits[pos + i] = (v >> (7 - i)) & 1;
    }
}

// Encodes ioProx card parameters into the 16-byte output buffer.
// The encoded frame uses 8+1 bit framing (8 data bits + 1 separator per group).
// Returns false if output pointer is NULL.
bool ioprox_encode_params_to_data(uint8_t version, uint8_t facility, uint16_t number, uint8_t *output)
{
    if (!output) return false;

    uint8_t b0 = 0x00;
    uint8_t b1 = 0xF0;
    uint8_t b2 = facility;
    uint8_t b3 = version;
    uint8_t b4 = (uint8_t)(number >> 8);
    uint8_t b5 = (uint8_t)(number & 0xFF);
    uint8_t b6 = ioprox_checksum5(b1, b2, b3, b4, b5);

    uint8_t bits[64] = {0};

    // Pack 7 data bytes using 8+1 framing (data bits + separator)
    write_bits_msb(bits,  0, b0); bits[ 8] = 0;
    write_bits_msb(bits,  9, b1); bits[17] = 1;
    write_bits_msb(bits, 18, b2); bits[26] = 1;
    write_bits_msb(bits, 27, b3); bits[35] = 1;
    write_bits_msb(bits, 36, b4); bits[44] = 1;
    write_bits_msb(bits, 45, b5); bits[53] = 1;
    write_bits_msb(bits, 54, b6);
    bits[62] = 1;
    bits[63] = 1;

    // Decoded fields
    output[0] = b3; // version
    output[1] = b2; // facility code
    output[2] = b4; // card number high byte
    output[3] = b5; // card number low byte

    // Raw bitstream packed into bytes [4..11]
    memset(output + 4, 0, 8);
    for (int i = 0; i < 64; i++) {
        if (bits[i]) {
            output[4 + (i / 8)] |= (uint8_t)(1u << (7 - (i % 8)));
        }
    }

    return true;
}

// Scans the tail of the bit buffer for a valid ioProx frame.
// To keep CPU load low, only the last ~5 frames (320 bits) are scanned.
// Returns true if a valid frame (checksum OK) was decoded into d->data.
static bool scan_tail(ioprox_codec_t *d)
{
    // Need at least 128 bits to verify two consecutive 64-bit frames
    if (d->bit_len < 128) return false;

    // Limit scan to the last 320 bits (~5 frames) for performance
    uint16_t start_from = (d->bit_len > 320) ? (uint16_t)(d->bit_len - 320) : 0;

    for (uint16_t i = start_from; (uint16_t)(i + 64 + 10) <= d->bit_len; i++) {
        for (int inv_i = 0; inv_i <= 1; inv_i++) {
            bool inv = (inv_i == 1);

            // Require two consecutive preambles 64 bits apart (Proxmark-style sync check)
            if (!preamble_match(d->bits, i, inv)) continue;
            if (!preamble_match(d->bits, i + 64, inv)) continue;

            // Try small phase offsets to tolerate minor bit-alignment jitter
            for (int8_t phase = -2; phase <= 2; phase++) {
                int32_t idx_i = (int32_t)i + (int32_t)phase;

                if (idx_i < 0) continue;
                if (idx_i + 64 > d->bit_len) continue;

                uint16_t idx = (uint16_t)idx_i;

                // Validate separator/stop bits within the frame
                int bad = 0;
                if (((d->bits[idx +  8] & 1u) ^ inv) != 0) bad++;
                if (((d->bits[idx + 17] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 26] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 35] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 44] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 53] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 62] & 1u) ^ inv) != 1) bad++;
                if (((d->bits[idx + 63] & 1u) ^ inv) != 1) bad++;

                // Tolerate up to 2 bad bits to handle noise
                if (bad > 2) continue;

                if (decode_and_pack(d, idx, inv)) {
                    return true;
                }
            }
        }
    }
    return false;
}

// --- Protocol callbacks ---

static void *ioprox_codec_alloc(void)
{
    ioprox_codec_t *d = (ioprox_codec_t *)malloc(sizeof(ioprox_codec_t));
    if (!d) return NULL;
    memset(d, 0, sizeof(*d));
    d->modem = fsk_alloc(FSK_BITRATE_IOPROX);
    return d;
}

static void ioprox_codec_free(void *codec)
{
    ioprox_codec_t *d = (ioprox_codec_t *)codec;
    if (!d) return;
    if (d->modem) {
        fsk_free(d->modem);
        d->modem = NULL;
    }
    free(d);
}

static uint8_t *ioprox_get_data(void *codec)
{
    ioprox_codec_t *d = (ioprox_codec_t *)codec;
    return d->data;
}

static void ioprox_decoder_start(void *codec, uint8_t format_hint)
{
    (void)format_hint;
    ioprox_codec_t *d = (ioprox_codec_t *)codec;
    d->bit_len = 0;
    memset(d->bits, 0, sizeof(d->bits));
    memset(d->data, 0, sizeof(d->data));
}

static bool ioprox_decoder_feed(void *codec, uint16_t val)
{
    ioprox_codec_t *d = (ioprox_codec_t *)codec;
    if (!d || !d->modem) return false;

    bool bit = false;
    if (!fsk_feed(d->modem, val, &bit)) {
        return false;
    }

    push_bit(d, (uint8_t)(bit ? 1u : 0u));

    if (d->bit_len >= 128) {
        if (scan_tail(d)) {
            ioprox_reset_bits(d);
            return true;
        }

        // Discard oldest 64 bits when buffer grows too large to prevent stale noise buildup
        if (d->bit_len >= 512) {
            memmove(d->bits, d->bits + 64, 512 - 64);
            d->bit_len -= 64;
        }
    }

    return false;
}

static inline void ioprox_emit_bit(int *k, bool bit)
{
    if (!bit) {
        for (int j = 0; j < LF_FSK2a_PWM_HI_FREQ_LOOP; j++) {
            m_ioprox_pwm_seq_vals[*k].channel_0    = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE / 2;
            m_ioprox_pwm_seq_vals[*k].counter_top  = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE;
            (*k)++;
        }
    } else {
        for (int j = 0; j < LF_FSK2a_PWM_LO_FREQ_LOOP; j++) {
            m_ioprox_pwm_seq_vals[*k].channel_0    = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE / 2;
            m_ioprox_pwm_seq_vals[*k].counter_top  = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE;
            (*k)++;
        }
    }
}

// FSK2a modulator: converts the 8 raw card bytes into a PWM sequence for LF transmission.
const nrf_pwm_sequence_t *ioprox_modulator(ioprox_codec_t *d, uint8_t *buf)
{
    (void)d;

    // Raw card data starts at buf[4] (bytes 0-3 are decoded fields)
    uint8_t *raw = &buf[4];

    int k = 0;

    // Emit 64 bits MSB-first
    for (int bi = 0; bi < 8; bi++) {
        uint8_t v = raw[bi];
        for (int bit = 7; bit >= 0; bit--) {
            ioprox_emit_bit(&k, ((v >> bit) & 1u) != 0);
        }
    }

    m_ioprox_pwm_seq.length = (uint16_t)(k * 4);
    return &m_ioprox_pwm_seq;
}

const protocol ioprox = {
    .tag_type = TAG_TYPE_IOPROX,
    .data_size = IOPROX_DATA_SIZE,
    .alloc = (codec_alloc)ioprox_codec_alloc,
    .free = (codec_free)ioprox_codec_free,
    .get_data = (codec_get_data)ioprox_get_data,
    .modulator = (modulator)ioprox_modulator,
    .decoder = {
        .start = (decoder_start)ioprox_decoder_start,
        .feed  = (decoder_feed)ioprox_decoder_feed,
    }
};

// Packs the raw card bitstream into T5577 blocks for writing.
// Block 0: T5577 config word for ioProx (FSK2a, RF/64, max block 2)
// Block 1: first 4 raw bytes
// Block 2: last 4 raw bytes
uint8_t ioprox_t55xx_writer(uint8_t *buf, uint32_t *blks) {
    uint8_t *raw = &buf[4];

    blks[0] = T5577_IOPROX_CONFIG;
    blks[1] = ((uint32_t)raw[0] << 24) |
              ((uint32_t)raw[1] << 16) |
              ((uint32_t)raw[2] <<  8) |
              ((uint32_t)raw[3]);
    blks[2] = ((uint32_t)raw[4] << 24) |
              ((uint32_t)raw[5] << 16) |
              ((uint32_t)raw[6] <<  8) |
              ((uint32_t)raw[7]);

    return IOPROX_T55XX_BLOCK_COUNT;
}
