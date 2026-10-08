#include "pyramid.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"

// Farpointe/Keri Pyramid, FSK2a, RF/50, 128-bit frame with 8-bit Dallas/Maxim
// checksum. The frame builder, parity insertion and CRC are ported verbatim
// from the Proxmark3/Flipper encoders so the emitted stream is bit-identical to
// a genuine tag; keeping that math intact is the whole point, so it is not
// "simplified".

#define PYRAMID_FRAME_BITS (128)
#define PYRAMID_FRAME_BYTES (16)
// Flipper keeps a trailing preamble copy to detect the frame boundary on read.
#define PYRAMID_DECODE_BYTES (19)

// FSK2a PWM timing, shared convention with hidprox.c (fc/8 and fc/10 at RF/50).
#define LF_FSK2a_PWM_LO_FREQ_LOOP (5)
#define LF_FSK2a_PWM_LO_FREQ_TOP_VALUE (10)
#define LF_FSK2a_PWM_HI_FREQ_LOOP (6)
#define LF_FSK2a_PWM_HI_FREQ_TOP_VALUE (8)

// --- minimal bit helpers, ported from Flipper bit_lib (MSB-first bit arrays) ---

typedef enum {
    PAR_EVEN,
    PAR_ODD,
    PAR_ALWAYS0,
    PAR_ALWAYS1,
} bit_parity_t;

static void bl_set_bit(uint8_t *data, size_t pos, bool bit) {
    if (bit) {
        data[pos / 8] |= 1UL << (7 - (pos % 8));
    } else {
        data[pos / 8] &= ~(1UL << (7 - (pos % 8)));
    }
}

static bool bl_get_bit(const uint8_t *data, size_t pos) {
    return (data[pos / 8] >> (7 - (pos % 8))) & 1;
}

static void bl_set_bits(uint8_t *data, size_t pos, uint8_t byte, uint8_t length) {
    for (uint8_t i = 0; i < length; ++i) {
        bl_set_bit(data, pos + i, (byte >> ((length - 1) - i)) & 1);
    }
}

static uint8_t bl_get_bits(const uint8_t *data, size_t pos, uint8_t length) {
    uint8_t shift = pos % 8;
    if (shift == 0) {
        return data[pos / 8] >> (8 - length);
    } else if (shift + length <= 8) {
        return (uint8_t)(data[pos / 8] << shift) >> (8 - length);
    }
    uint8_t value = (data[pos / 8] << shift);
    value |= data[pos / 8 + 1] >> (8 - shift);
    return value >> (8 - length);
}

static uint16_t bl_get_bits_16(const uint8_t *data, size_t pos, uint8_t length) {
    if (length <= 8) {
        return bl_get_bits(data, pos, length);
    }
    uint16_t value = bl_get_bits(data, pos, 8) << (length - 8);
    value |= bl_get_bits(data, pos + 8, length - 8);
    return value;
}

static uint32_t bl_get_bits_32(const uint8_t *data, size_t pos, uint8_t length) {
    uint32_t value = 0;
    if (length <= 8) {
        value = bl_get_bits(data, pos, length);
    } else if (length <= 16) {
        value = bl_get_bits(data, pos, 8) << (length - 8);
        value |= bl_get_bits(data, pos + 8, length - 8);
    } else if (length <= 24) {
        value = bl_get_bits(data, pos, 8) << (length - 8);
        value |= bl_get_bits(data, pos + 8, 8) << (length - 16);
        value |= bl_get_bits(data, pos + 16, length - 16);
    } else {
        value = (uint32_t)bl_get_bits(data, pos, 8) << (length - 8);
        value |= (uint32_t)bl_get_bits(data, pos + 8, 8) << (length - 16);
        value |= (uint32_t)bl_get_bits(data, pos + 16, 8) << (length - 24);
        value |= bl_get_bits(data, pos + 24, length - 24);
    }
    return value;
}

static void bl_copy_bits(uint8_t *data, size_t pos, size_t length, const uint8_t *src, size_t src_pos) {
    for (size_t i = 0; i < length; ++i) {
        bl_set_bit(data, pos + i, bl_get_bit(src, src_pos + i));
    }
}

static void bl_push_bit(uint8_t *data, size_t data_size, bool bit) {
    size_t last = data_size - 1;
    for (size_t i = 0; i < last; ++i) {
        data[i] = (data[i] << 1) | ((data[i + 1] >> 7) & 1);
    }
    data[last] = (data[last] << 1) | bit;
}

static void bl_reverse_bits(uint8_t *data, size_t pos, uint8_t length) {
    size_t i = 0, j = length - 1;
    while (i < j) {
        bool tmp = bl_get_bit(data, pos + i);
        bl_set_bit(data, pos + i, bl_get_bit(data, pos + j));
        bl_set_bit(data, pos + j, tmp);
        i++;
        j--;
    }
}

static bool bl_test_parity_32(uint32_t bits, bit_parity_t parity) {
    return parity == PAR_EVEN ? __builtin_parity(bits) : !__builtin_parity(bits);
}

static void bl_add_parity(const uint8_t *data, size_t pos, uint8_t *dest, size_t dest_pos,
                          uint8_t source_length, uint8_t parity_length, bit_parity_t parity) {
    uint32_t parity_word = 0;
    size_t j = 0;
    for (int word = 0; word < source_length; word += parity_length - 1) {
        for (int bit = 0; bit < parity_length - 1; bit++) {
            parity_word = (parity_word << 1) | bl_get_bit(data, pos + word + bit);
            bl_set_bit(dest, dest_pos + j++, bl_get_bit(data, pos + word + bit));
        }
        switch (parity) {
            case PAR_ALWAYS0:
                bl_set_bit(dest, dest_pos + j++, 0);
                break;
            case PAR_ALWAYS1:
                bl_set_bit(dest, dest_pos + j++, 1);
                break;
            default:
                bl_set_bit(dest, dest_pos + j++, (bl_test_parity_32(parity_word, PAR_ODD) ^ parity) ^ 1);
                break;
        }
        parity_word = 0;
    }
}

static size_t bl_remove_bit_every_nth(uint8_t *data, size_t pos, uint8_t length, uint8_t n) {
    size_t counter = 0, result = 0;
    uint8_t buf = 0, cnt = 0;
    while (counter < length) {
        if ((counter + 1) % n != 0) {
            buf = (buf << 1) | bl_get_bit(data, pos + counter);
            cnt++;
        }
        if (cnt == 8) {
            bl_set_bits(data, pos + result, buf, 8);
            cnt = 0;
            buf = 0;
            result += 8;
        }
        counter++;
    }
    if (cnt != 0) {
        bl_set_bits(data, pos + result, buf, cnt);
        result += cnt;
    }
    return result;
}

static uint8_t bl_crc8(const uint8_t *data, size_t size, uint8_t poly, uint8_t init,
                       bool ref_in, bool ref_out, uint8_t xor_out) {
    uint8_t crc = init;
    for (size_t i = 0; i < size; ++i) {
        uint8_t byte = data[i];
        if (ref_in) {
            bl_reverse_bits(&byte, 0, 8);
        }
        crc ^= byte;
        for (size_t j = 8; j > 0; --j) {
            crc = (crc & 0x80) ? (crc << 1) ^ poly : (crc << 1);
        }
    }
    if (ref_out) {
        bl_reverse_bits(&crc, 0, 8);
    }
    return crc ^ xor_out;
}

// --- Pyramid frame math (ported from Flipper protocol_pyramid.c) ---

static bool pyr_get_parity(const uint8_t *bits, size_t pos, uint8_t type, int length) {
    int x = 0;
    for (; length > 0; --length) {
        x += bl_get_bit(bits, pos + length - 1);
    }
    return (x % 2) ^ type;
}

static void pyr_add_wiegand_parity(uint8_t *target, uint8_t tpos, uint8_t *src, uint8_t length) {
    bl_set_bit(target, tpos, pyr_get_parity(src, 0, 0 /* even */, length / 2));
    bl_copy_bits(target, tpos + 1, length, src, 0);
    bl_set_bit(target, tpos + length + 1, pyr_get_parity(src, length / 2, 1 /* odd */, length / 2));
}

void pyramid_encode_frame(const uint8_t *id, uint8_t *frame) {
    memset(frame, 0, PYRAMID_FRAME_BYTES);

    uint8_t pre[PYRAMID_FRAME_BYTES];
    memset(pre, 0, sizeof(pre));

    bl_set_bit(pre, 79, 1);  // format start bit

    uint8_t wiegand[3] = {0};
    bl_copy_bits(wiegand, 0, 8, id, 8);    // facility code  (id byte 1)
    bl_copy_bits(wiegand, 8, 16, id, 16);  // card number    (id bytes 2..3)

    pyr_add_wiegand_parity(pre, 80, wiegand, 24);
    bl_add_parity(pre, 8, frame, 8, 102, 8, PAR_ODD);

    uint8_t cs[13];
    for (uint8_t i = 0; i < 13; i++) {
        cs[i] = bl_get_bits(frame, 16 + (i * 8), 8);
    }
    uint8_t crc = bl_crc8(cs, 13, 0x31, 0x00, true, true, 0x00);
    bl_set_bits(frame, 120, crc, 8);
}

static bool pyr_can_be_decoded(uint8_t *data) {
    if (bl_get_bits_16(data, 0, 16) != 0b0000000000000001 || bl_get_bits(data, 16, 8) != 0b00000001) {
        return false;
    }
    if (bl_get_bits_16(data, 128, 16) != 0b0000000000000001 || bl_get_bits(data, 136, 8) != 0b00000001) {
        return false;
    }
    uint8_t checksum = bl_get_bits(data, 120, 8);
    uint8_t cs[13];
    for (uint8_t i = 0; i < 13; i++) {
        cs[i] = bl_get_bits(data, 16 + (i * 8), 8);
    }
    if (checksum != bl_crc8(cs, 13, 0x31, 0x00, true, true, 0x00)) {
        return false;
    }
    bl_remove_bit_every_nth(data, 8, 15 * 8, 8);
    int j;
    for (j = 0; j < 105; ++j) {
        if (bl_get_bit(data, j)) break;
    }
    return (105 - j) == 26;  // only 26-bit format supported
}

static void pyr_decode(const uint8_t *encoded, uint8_t *id) {
    bl_set_bits(id, 0, 26, 8);               // format length
    bl_copy_bits(id, 8, 8, encoded, 73 + 8);  // facility code
    bl_copy_bits(id, 16, 16, encoded, 81 + 8); // card number
}

// --- codec / protocol glue (mirrors hidprox.c) ---

typedef struct {
    uint8_t data[LF_PYRAMID_TAG_ID_SIZE];
    uint8_t encoded[PYRAMID_DECODE_BYTES];
    fsk_t *modem;
} pyramid_codec;

static pyramid_codec *pyramid_codec_alloc(void) {
    pyramid_codec *d = malloc(sizeof(pyramid_codec));
    d->modem = fsk_alloc(FSK_BITRATE_HID);
    return d;
}

static void pyramid_codec_free(pyramid_codec *d) {
    if (d->modem) {
        fsk_free(d->modem);
        d->modem = NULL;
    }
    free(d);
}

static void pyramid_decoder_start(pyramid_codec *d, uint8_t format_hint) {
    (void)format_hint;
    memset(d->data, 0, sizeof(d->data));
    memset(d->encoded, 0, sizeof(d->encoded));
}

static uint8_t *pyramid_get_data(pyramid_codec *d) {
    return d->data;
}

static bool pyramid_decoder_feed(pyramid_codec *d, uint16_t val) {
    bool bit = false;
    if (!fsk_feed(d->modem, val, &bit)) {
        return false;
    }
    bl_push_bit(d->encoded, PYRAMID_DECODE_BYTES, bit);
    if (pyr_can_be_decoded(d->encoded)) {
        pyr_decode(d->encoded, d->data);
        return true;
    }
    return false;
}

static nrf_pwm_values_wave_form_t m_pyramid_pwm_seq_vals[PYRAMID_FRAME_BITS * LF_FSK2a_PWM_HI_FREQ_LOOP] = {};

static nrf_pwm_sequence_t m_pyramid_pwm_seq = {
    .values.p_wave_form = m_pyramid_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_pyramid_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

static const nrf_pwm_sequence_t *pyramid_modulator(pyramid_codec *d, uint8_t *buf) {
    memcpy(d->data, buf, LF_PYRAMID_TAG_ID_SIZE);

    uint8_t frame[PYRAMID_FRAME_BYTES];
    pyramid_encode_frame(d->data, frame);

    int k = 0;
    for (int i = 0; i < PYRAMID_FRAME_BITS; i++) {
        bool bit = bl_get_bit(frame, i);
        if (!bit) {
            for (int j = 0; j < LF_FSK2a_PWM_HI_FREQ_LOOP; j++) {
                m_pyramid_pwm_seq_vals[k].channel_0 = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE / 2;
                m_pyramid_pwm_seq_vals[k].counter_top = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE;
                k++;
            }
        } else {
            for (int j = 0; j < LF_FSK2a_PWM_LO_FREQ_LOOP; j++) {
                m_pyramid_pwm_seq_vals[k].channel_0 = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE / 2;
                m_pyramid_pwm_seq_vals[k].counter_top = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE;
                k++;
            }
        }
    }
    m_pyramid_pwm_seq.length = k * 4;
    return &m_pyramid_pwm_seq;
}

const protocol pyramid = {
    .tag_type = TAG_TYPE_PYRAMID,
    .data_size = LF_PYRAMID_TAG_ID_SIZE,
    .alloc = (codec_alloc)pyramid_codec_alloc,
    .free = (codec_free)pyramid_codec_free,
    .get_data = (codec_get_data)pyramid_get_data,
    .modulator = (modulator)pyramid_modulator,
    .decoder =
    {
        .start = (decoder_start)pyramid_decoder_start,
        .feed = (decoder_feed)pyramid_decoder_feed,
    },
};

uint8_t pyramid_t55xx_writer(const uint8_t *id, uint32_t *blks) {
    uint8_t frame[PYRAMID_FRAME_BYTES];
    pyramid_encode_frame(id, frame);
    blks[0] = T5577_PYRAMID_CONFIG;
    blks[1] = bl_get_bits_32(frame, 0, 32);
    blks[2] = bl_get_bits_32(frame, 32, 32);
    blks[3] = bl_get_bits_32(frame, 64, 32);
    blks[4] = bl_get_bits_32(frame, 96, 32);
    return 5;
}
