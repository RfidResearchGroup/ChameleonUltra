#include "hidprox.h"

#include <stdlib.h>
#include <string.h>

#include "hex_utils.h"
#include "nordic_common.h"
#include "parity.h"
#include "protocols.h"
#include "t55xx.h"
#include "tag_base_type.h"
#include "wiegand.h"

#define HIDPROX_SOF (0x1d)
#define HIDPROX_T55XX_BLOCK_COUNT (4)
#define DEMOD_BUFFER_SIZE (32)
#define HIDPROX_RAW_SIZE (96)

#define LF_FSK2a_PWM_LO_FREQ_LOOP (5)
#define LF_FSK2a_PWM_LO_FREQ_TOP_VALUE (10)
#define LF_FSK2a_PWM_HI_FREQ_LOOP (6)
#define LF_FSK2a_PWM_HI_FREQ_TOP_VALUE (8)

static nrf_pwm_values_wave_form_t m_hidprox_pwm_seq_vals[HIDPROX_RAW_SIZE * 6] = {};

nrf_pwm_sequence_t m_hidprox_pwm_seq = {
    .values.p_wave_form = m_hidprox_pwm_seq_vals,
    .length = NRF_PWM_VALUES_LENGTH(m_hidprox_pwm_seq_vals),
    .repeats = 0,
    .end_delay = 0,
};

void decoder_reset(hidprox_codec *d) {
    d->sof = 0;
    d->state = STATE_SOF;
    d->raw = 0;
    d->raw_length = 0;
    d->bit = false;
}

void hidprox_decoder_start(hidprox_codec *d, uint8_t format_hint) {
    memset(d->data, 0, HIDPROX_DATA_SIZE);
    decoder_reset(d);
    d->format_hint = format_hint;
}

hidprox_codec *hidprox_codec_alloc(void) {
    hidprox_codec *d = malloc(sizeof(hidprox_codec));
    d->card = NULL;
    d->modem = fsk_alloc();
    return d;
}

void hidprox_codec_free(hidprox_codec *d) {
    if (d->modem) {
        fsk_free(d->modem);
        d->modem = NULL;
    }
    if (d->card) {
        free(d->card);
        d->card = NULL;
    }
    free(d);
}

// ref: https://github.com/RfidResearchGroup/proxmark3/blob/810eaeac250f35eca8819aa9c23cb57c5276b3e6/client/src/wiegand_formatutils.c#L131
static uint8_t hidprox_codec_get_length(hidprox_codec *d) {
    //! TODO direct XOR check
    if (!(d->raw >> 37) && 0x01) {
        return 37;
    }
    uint16_t bits = (d->raw >> 26) & 0x7ff;
    uint8_t length = 25;
    while (bits) {
        bits >>= 1;
        length++;
    }
    return length;
}

uint8_t *hidprox_get_data(hidprox_codec *d) {
    if (d->card == NULL) {
        return d->data;
    }
    // total 13 bytes
    d->data[0] = d->card->format;
    num_to_bytes(d->card->facility_code, 4, d->data + 1);  // 4 bytes
    num_to_bytes(d->card->card_number, 5, d->data + 5);    // 5 bytes
    num_to_bytes(d->card->issue_level, 1, d->data + 10);   // 1 bytes
    num_to_bytes(d->card->oem, 2, d->data + 11);           // 2 bytes
    return d->data;
};

bool hidprox_decode_feed(hidprox_codec *d, bool bit) {
    if (d->state == STATE_SOF) {
        d->sof <<= 1;
        if (bit) {
            SET_BIT(d->sof, 0);
        }
        if (d->sof == HIDPROX_SOF) {  // found start of frame
            d->state = STATE_DATA_LO;
        }
        return false;
    }

    if (d->state == STATE_DATA_LO) {
        d->bit = bit;
        d->state = STATE_DATA_HI;
        return false;
    }

    if (d->state == STATE_DATA_HI) {
        if (bit == d->bit) {  // invalid manchester bit
            decoder_reset(d);
            return false;
        }

        d->raw <<= 1;
        d->raw_length++;
        if (d->bit && !bit) {
            SET_BIT(d->raw, 0);
        }

        if (d->raw_length < 44) {
            d->state = STATE_DATA_LO;
            return false;
        }

        d->state = STATE_DONE;

        uint8_t length = hidprox_codec_get_length(d);
        wiegand_card_t *card = unpack(d->format_hint, length, 0, d->raw);
        if (card == NULL) {
            decoder_reset(d);
            return false;
        }
        d->card = card;
        return true;
    }

    return false;
}

bool hidprox_decoder_feed(hidprox_codec *d, uint16_t val) {
    bool bit = false;
    if (!fsk_feed(d->modem, val, &bit)) {
        return false;
    }
    return hidprox_decode_feed(d, bit);
}

void hidprox_raw_data(wiegand_card_t *card, uint32_t *hi, uint32_t *mid, uint32_t *bot) {
    *hi = 0;
    *mid = 0;
    *bot = 0;
    uint64_t data = pack(card);
    if (data == 0) {
        return;
    }
    *hi = HIDPROX_SOF;
    for (uint8_t i = 0; i < 44; i++) {
        uint32_t *blk;
        if (i < 12) {
            blk = hi;
        } else if (i < 28) {
            blk = mid;
        } else {
            blk = bot;
        }
        *blk <<= 2;
        if ((data >> (43 - i)) & 0x01) {
            *blk |= 0x02;
        } else {
            *blk |= 0x01;
        }
    }
}

// fsk2a modulator
const nrf_pwm_sequence_t *hidprox_modulator(hidprox_codec *d, uint8_t *buf) {
    uint64_t cn = buf[5];
    cn = (cn << 32) | (bytes_to_num(buf + 6, 4));
    wiegand_card_t card = {
        .facility_code = bytes_to_num(buf + 1, 4),
        .card_number = cn,
        .issue_level = buf[10],
        .oem = bytes_to_num(buf + 11, 2),
        .format = buf[0],
    };

    uint32_t hi, mid, bot;
    hidprox_raw_data(&card, &hi, &mid, &bot);
    int k = 0;
    for (int i = 0; i < HIDPROX_RAW_SIZE; i++) {
        bool bit = false;
        if (i < 32) {
            bit = (hi >> (31 - i)) & 1;
        } else if (i < 64) {
            bit = (mid >> (63 - i)) & 1;
        } else {
            bit = (bot >> (95 - i)) & 1;
        }
        if (!bit) {
            for (int j = 0; j < LF_FSK2a_PWM_HI_FREQ_LOOP; j++) {
                m_hidprox_pwm_seq_vals[k].channel_0 = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE / 2;
                m_hidprox_pwm_seq_vals[k].counter_top = LF_FSK2a_PWM_HI_FREQ_TOP_VALUE;
                k++;
            }
        } else {
            for (int j = 0; j < LF_FSK2a_PWM_LO_FREQ_LOOP; j++) {
                m_hidprox_pwm_seq_vals[k].channel_0 = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE / 2;
                m_hidprox_pwm_seq_vals[k].counter_top = LF_FSK2a_PWM_LO_FREQ_TOP_VALUE;
                k++;
            }
        }
    }
    m_hidprox_pwm_seq.length = k * 4;
    return &m_hidprox_pwm_seq;
};

const protocol hidprox = {
    .tag_type = TAG_TYPE_HID_PROX,
    .data_size = HIDPROX_DATA_SIZE,
    .alloc = (codec_alloc)hidprox_codec_alloc,
    .free = (codec_free)hidprox_codec_free,
    .get_data = (codec_get_data)hidprox_get_data,
    .modulator = (modulator)hidprox_modulator,
    .decoder =
        {
            .start = (decoder_start)hidprox_decoder_start,
            .feed = (decoder_feed)hidprox_decoder_feed,
        },
};

uint8_t hidprox_t55xx_writer(wiegand_card_t *card, uint32_t *blks) {
    blks[0] = T5577_HIDPROX_CONFIG;
    hidprox_raw_data(card, &blks[1], &blks[2], &blks[3]);
    return HIDPROX_T55XX_BLOCK_COUNT;
}