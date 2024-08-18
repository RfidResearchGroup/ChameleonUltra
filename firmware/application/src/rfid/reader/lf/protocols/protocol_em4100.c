#include <protocols/protocol.h>
#include <stdlib.h>
#include <string.h>
#include <utils/manchester_decoder.h>
#include <utils/bit_buffer.h>
#include "lfrfid_protocols.h"

typedef uint64_t EM4100DecodedData;

#define EM_HEADER_POS (55)
#define EM_HEADER_MASK (0x1FFLLU << EM_HEADER_POS)

#define EM_FIRST_ROW_POS (50)

#define EM_ROW_COUNT (10)
#define EM_COLUMN_COUNT (4)
#define EM_BITS_PER_ROW_COUNT (EM_COLUMN_COUNT + 1)

#define EM_COLUMN_POS (4)
#define EM_STOP_POS (0)
#define EM_STOP_MASK (0x1LLU << EM_STOP_POS)

#define EM_HEADER_AND_STOP_MASK (EM_HEADER_MASK | EM_STOP_MASK)
#define EM_HEADER_AND_STOP_DATA (EM_HEADER_MASK)

#define EM4100_DECODED_DATA_SIZE (5)
#define EM4100_ENCODED_DATA_SIZE (sizeof(EM4100DecodedData))

#define EM4100_RAW_DATA_SIZE (50)

#define EM_READ_TIME1_BASE (0x40)
#define EM_READ_TIME2_BASE (0x60)
#define EM_READ_TIME3_BASE (0x80)
#define EM_READ_JITTER_TIME_BASE (0x10)

#define NRF_LOG_MODULE_NAME em4100
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

typedef struct {

    uint8_t data[EM4100_DECODED_DATA_SIZE];
    EM4100DecodedData encoded_data;
    uint8_t encoded_data_index;
    bool encoded_polarity;

    ManchesterState decoder_manchester_state;
    uint8_t clock_per_bit;
} ProtocolEM4100;

uint16_t protocol_em4100_get_time_divisor(ProtocolEM4100* proto) {
    switch(proto->clock_per_bit) {
    case 64:
        return 1;
    case 32:
        return 2;
    case 16:
        return 4;
    default:
        return 1;
    }
}

uint32_t protocol_em4100_get_t5577_bitrate(ProtocolEM4100* proto) {
    switch(proto->clock_per_bit) {
    case 64:
        return LFRFID_T5577_BITRATE_RF_64;
    case 32:
        return LFRFID_T5577_BITRATE_RF_32;
    case 16:
        return LFRFID_T5577_BITRATE_RF_16;
    default:
        return LFRFID_T5577_BITRATE_RF_64;
    }
}

uint8_t protocol_em4100_get_time1_low(ProtocolEM4100* proto) {
    return EM_READ_TIME1_BASE / protocol_em4100_get_time_divisor(proto) -
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

uint8_t protocol_em4100_get_time1_high(ProtocolEM4100* proto) {
    return EM_READ_TIME1_BASE / protocol_em4100_get_time_divisor(proto) +
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

bool protocol_em4100_get_time1(ProtocolEM4100* proto, uint8_t interval) {
    return 
        interval >= protocol_em4100_get_time1_low(proto) && 
        interval <= protocol_em4100_get_time1_high(proto);
}

uint8_t protocol_em4100_get_time2_low(ProtocolEM4100* proto) {
    return EM_READ_TIME2_BASE / protocol_em4100_get_time_divisor(proto) -
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

uint8_t protocol_em4100_get_time2_high(ProtocolEM4100* proto) {
    return EM_READ_TIME2_BASE / protocol_em4100_get_time_divisor(proto) +
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

bool protocol_em4100_get_time2(ProtocolEM4100* proto, uint8_t interval) {
    return 
        interval >= protocol_em4100_get_time2_low(proto) && 
        interval <= protocol_em4100_get_time2_high(proto);
}

uint8_t protocol_em4100_get_time3_low(ProtocolEM4100* proto) {
    return EM_READ_TIME3_BASE / protocol_em4100_get_time_divisor(proto) -
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

uint8_t protocol_em4100_get_time3_high(ProtocolEM4100* proto) {
    return EM_READ_TIME3_BASE / protocol_em4100_get_time_divisor(proto) +
           EM_READ_JITTER_TIME_BASE / protocol_em4100_get_time_divisor(proto);
}

bool protocol_em4100_get_time3(ProtocolEM4100* proto, uint8_t interval) {
    return 
        interval >= protocol_em4100_get_time3_low(proto) && 
        interval <= protocol_em4100_get_time3_high(proto);
}

ProtocolEM4100* protocol_em4100_alloc(void) {
    ProtocolEM4100* proto = malloc(sizeof(ProtocolEM4100));
    proto->clock_per_bit = 64;
    return (void*)proto;
};

ProtocolEM4100* protocol_em4100_16_alloc(void) {
    ProtocolEM4100* proto = malloc(sizeof(ProtocolEM4100));
    proto->clock_per_bit = 16;
    return (void*)proto;
};

ProtocolEM4100* protocol_em4100_32_alloc(void) {
    ProtocolEM4100* proto = malloc(sizeof(ProtocolEM4100));
    proto->clock_per_bit = 32;
    return (void*)proto;
};

void protocol_em4100_free(ProtocolEM4100* proto) {
    free(proto);
};

uint8_t* protocol_em4100_get_data(ProtocolEM4100* proto) {
    return proto->data;
};

static void em4100_decode(
    const uint8_t* encoded_data,
    const uint8_t encoded_data_size,
    uint8_t* decoded_data,
    const uint8_t decoded_data_size) {
    if (decoded_data_size >= EM4100_DECODED_DATA_SIZE ||
        encoded_data_size >= EM4100_ENCODED_DATA_SIZE)
        return;

    uint8_t decoded_data_index = 0;
    EM4100DecodedData card_data = *((EM4100DecodedData*)(encoded_data));

    // clean result
    memset(decoded_data, 0, decoded_data_size);

    // header
    for(uint8_t i = 0; i < 9; i++) {
        card_data = card_data << 1;
    }

    // nibbles
    uint8_t value = 0;
    for(uint8_t r = 0; r < EM_ROW_COUNT; r++) {
        uint8_t nibble = 0;
        for(uint8_t i = 0; i < 5; i++) {
            if(i < 4) nibble = (nibble << 1) | (card_data & (1LLU << 63) ? 1 : 0);
            card_data = card_data << 1;
        }
        value = (value << 4) | nibble;
        if(r % 2) {
            decoded_data[decoded_data_index] |= value;
            decoded_data_index++;
            value = 0;
        }
    }
}

/*static bool em4100_can_be_decoded(const uint8_t* encoded_data, const uint8_t encoded_data_size) {
    if (encoded_data_size >= EM4100_ENCODED_DATA_SIZE)
        return false;
    const EM4100DecodedData* card_data = (EM4100DecodedData*)encoded_data;

    // check header and stop bit
    if((*card_data & EM_HEADER_AND_STOP_MASK) != EM_HEADER_AND_STOP_DATA) return false;

    // check row parity
    for(uint8_t i = 0; i < EM_ROW_COUNT; i++) {
        uint8_t parity_sum = 0;

        for(uint8_t j = 0; j < EM_BITS_PER_ROW_COUNT; j++) {
            parity_sum += (*card_data >> (EM_FIRST_ROW_POS - i * EM_BITS_PER_ROW_COUNT + j)) & 1;
        }

        if((parity_sum % 2)) {
            return false;
        }
    }

    // check columns parity
    for(uint8_t i = 0; i < EM_COLUMN_COUNT; i++) {
        uint8_t parity_sum = 0;

        for(uint8_t j = 0; j < EM_ROW_COUNT + 1; j++) {
            parity_sum += (*card_data >> (EM_COLUMN_POS - i + j * EM_BITS_PER_ROW_COUNT)) & 1;
        }

        if((parity_sum % 2)) {
            return false;
        }
    }

    return true;
}*/

void protocol_em4100_decoder_start(ProtocolEM4100* proto) {
    memset(proto->data, 0, EM4100_DECODED_DATA_SIZE);
    proto->encoded_data = 0;
/*    manchester_advance(
        proto->decoder_manchester_state,
        ManchesterEventReset,
        &proto->decoder_manchester_state,
        NULL); */
};

bool protocol_em4100_decoder_feed(ProtocolEM4100* proto, bool level, uint32_t duration) {
    bool result = false;

  /*  ManchesterEvent event = ManchesterEventReset;

    if(duration > protocol_em4100_get_short_time_low(proto) &&
       duration < protocol_em4100_get_short_time_high(proto)) {
        if(!level) {
            event = ManchesterEventShortHigh;
        } else {
            event = ManchesterEventShortLow;
        }
    } else if(
        duration > protocol_em4100_get_long_time_low(proto) &&
        duration < protocol_em4100_get_long_time_high(proto)) {
        if(!level) {
            event = ManchesterEventLongHigh;
        } else {
            event = ManchesterEventLongLow;
        }
    }
*/
 /*   if(event != ManchesterEventReset) {
        bool data;
        bool data_ok = manchester_advance(
            proto->decoder_manchester_state, event, &proto->decoder_manchester_state, &data);

        if(data_ok) {
            proto->encoded_data = (proto->encoded_data << 1) | data;

            if(em4100_can_be_decoded((uint8_t*)&proto->encoded_data, sizeof(EM4100DecodedData))) {
                em4100_decode(
                    (uint8_t*)&proto->encoded_data,
                    sizeof(EM4100DecodedData),
                    proto->data,
                    EM4100_DECODED_DATA_SIZE);
                result = true;
            }
        }
    }
*/
    return result;
};

uint8_t read_bit_interval(ProtocolEM4100* proto, uint8_t interval) {
    if (protocol_em4100_get_time1(proto, interval))
        return 1;

    if (protocol_em4100_get_time2(proto, interval))
        return 2;

    if (protocol_em4100_get_time3(proto, interval))
        return 3;

    return 0;
}


uint8_t decode(ProtocolEM4100* proto, uint8_t* data, size_t datalen) {
    BitBuffer* bit_buffer = bit_buffer_alloc(EM4100_RAW_DATA_SIZE);

    uint8_t sync = 1;      //After the current interval process is processed, is it on the judgment line
    uint8_t cardindex = 0; //Record change number
    bool error = false;
    for (int i = 0; i < datalen; i++) {
        uint8_t bit_interval = read_bit_interval(proto, data[i]);

        NRF_LOG_INFO("-->[%d] %x %d", protocol_em4100_get_time_divisor(proto), data[i], bit_interval);

        if (bit_interval == 0) {
            bit_buffer_reset(bit_buffer);
            cardindex = 0;
            continue;
        }

        switch (sync) {
            case 1: //Synchronous state
                switch (bit_interval) {
                    case 0: //TheSynchronousState1T,Add1Digit0,StillSynchronize
                        bit_buffer_append_bit(bit_buffer, 0);
                        cardindex++;
                        break;
                    case 1: // Synchronous status 1.5T, add 1 digit 1, switch to non -synchronized state
                        bit_buffer_append_bit(bit_buffer, 1);
                        cardindex++;
                        sync = 0;
                        break;
                    case 2: //Synchronous2T,Add2Digits10,StillSynchronize
                        bit_buffer_append_bit(bit_buffer, 1);
                        cardindex++;
                        bit_buffer_append_bit(bit_buffer, 0);
                        cardindex++;
                        break;
                    default:
                        error = true;
                        break;
                }
                break;
            case 0: //Non -synchronous state
                switch (bit_interval) {
                    case 0: //1TInNonSynchronousState,Add1Digit1,StillNonSynchronous
                        bit_buffer_append_bit(bit_buffer, 1);
                        cardindex++;
                        break;
                    case 1: // In non -synchronous status 1.5T, add 2 digits 10, switch to the synchronous state
                        bit_buffer_append_bit(bit_buffer, 1);
                        cardindex++;
                        bit_buffer_append_bit(bit_buffer, 0);
                        cardindex++;
                        sync = 1;
                        break;
                    case 2: //The2TOfTheNonSynchronousState,ItIsImpossibleToOccur,ReportAnError
                        error = true;
                        break;
                    default:
                        error = true;
                        break;
                }
                break;
        }
        if (bit_buffer_get_size(bit_buffer) >= EM4100_RAW_DATA_SIZE * 8 || error)
            break;
    }

    bit_buffer_dump(bit_buffer);

    bit_buffer_free(bit_buffer);

    return 1;
}

void protocol_em4100_decoder_decode(ProtocolEM4100* proto, uint8_t* data, size_t datalen) {
    protocol_em4100_decoder_start(proto);

    decode(proto, data, datalen);
}

static void em4100_write_nibble(bool low_nibble, uint8_t data, EM4100DecodedData* encoded_data) {
    uint8_t parity_sum = 0;
    uint8_t start = 0;
    if(!low_nibble) start = 4;

    for(int8_t i = (start + 3); i >= start; i--) {
        parity_sum += (data >> i) & 1;
        *encoded_data = (*encoded_data << 1) | ((data >> i) & 1);
    }

    *encoded_data = (*encoded_data << 1) | ((parity_sum % 2) & 1);
}

bool protocol_em4100_encoder_start(ProtocolEM4100* proto) {
    // header
    proto->encoded_data = 0b111111111;

    // data
    for(uint8_t i = 0; i < EM4100_DECODED_DATA_SIZE; i++) {
        em4100_write_nibble(false, proto->data[i], &proto->encoded_data);
        em4100_write_nibble(true, proto->data[i], &proto->encoded_data);
    }

    // column parity and stop bit
    uint8_t parity_sum;

    for(uint8_t c = 0; c < EM_COLUMN_COUNT; c++) {
        parity_sum = 0;
        for(uint8_t i = 1; i <= EM_ROW_COUNT; i++) {
            uint8_t parity_bit = (proto->encoded_data >> (i * EM_BITS_PER_ROW_COUNT - 1)) & 1;
            parity_sum += parity_bit;
        }
        proto->encoded_data = (proto->encoded_data << 1) | ((parity_sum % 2) & 1);
    }

    // stop bit
    proto->encoded_data = (proto->encoded_data << 1) | 0;

    proto->encoded_data_index = 0;
    proto->encoded_polarity = true;

    return true;
};

LevelDuration protocol_em4100_encoder_yield(ProtocolEM4100* proto) {
    bool level = (proto->encoded_data >> (63 - proto->encoded_data_index)) & 1;
    uint32_t duration = proto->clock_per_bit / 2;

    if(proto->encoded_polarity) {
        proto->encoded_polarity = false;
    } else {
        level = !level;

        proto->encoded_polarity = true;
        proto->encoded_data_index++;
        if(proto->encoded_data_index >= 64) {
            proto->encoded_data_index = 0;
        }
    }

    return level_duration_make(level, duration);
};

bool protocol_em4100_write_data(ProtocolEM4100* protocol, void* data) {
    LFRFIDWriteRequest* request = (LFRFIDWriteRequest*)data;
    bool result = false;

    // Correct protocol data by redecoding
    protocol_em4100_encoder_start(protocol);
    em4100_decode(
        (uint8_t*)&protocol->encoded_data,
        sizeof(EM4100DecodedData),
        protocol->data,
        EM4100_DECODED_DATA_SIZE);

    protocol_em4100_encoder_start(protocol);

    if(request->write_type == LFRFIDWriteTypeT5577) {
        request->t5577.block[0] =
            (LFRFID_T5577_MODULATION_MANCHESTER | protocol_em4100_get_t5577_bitrate(protocol) |
             (2 << LFRFID_T5577_MAXBLOCK_SHIFT));
        request->t5577.block[1] = protocol->encoded_data;
        request->t5577.block[2] = protocol->encoded_data >> 32;
        request->t5577.blocks_to_write = 3;
        result = true;
    }
    return result;
};

void protocol_em4100_render_data(ProtocolEM4100* protocol, String* result) {
    uint8_t* data = protocol->data;
    stringprintf(
        result,
        "FC: %03u, Card: %05u (RF/%u)",
        data[2],
        (uint16_t)((data[3] << 8) | (data[4])),
        protocol->clock_per_bit);
};

const ProtocolBase protocol_em4100 = {
    .name = "EM4100/64(std)",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK | LFRFIDFeaturePSK,
    .validate_count = 3,
    .alloc = (ProtocolAlloc)protocol_em4100_alloc,
    .free = (ProtocolFree)protocol_em4100_free,
    .get_data = (ProtocolGetData)protocol_em4100_get_data,
    .decoder =
        {
            .decode = (ProtocolDecoderDecode)protocol_em4100_decoder_decode,
            .start = (ProtocolDecoderStart)protocol_em4100_decoder_start,
            .feed = (ProtocolDecoderFeed)protocol_em4100_decoder_feed,
        },
    .encoder =
        {
            .start = (ProtocolEncoderStart)protocol_em4100_encoder_start,
            .yield = (ProtocolEncoderYield)protocol_em4100_encoder_yield,
        },
    .render_data = (ProtocolRenderData)protocol_em4100_render_data,
    .render_brief_data = (ProtocolRenderData)protocol_em4100_render_data,
    .write_data = (ProtocolWriteData)protocol_em4100_write_data,
};

const ProtocolBase protocol_em4100_32 = {
    .name = "EM4100/32",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK | LFRFIDFeaturePSK,
    .validate_count = 3,
    .alloc = (ProtocolAlloc)protocol_em4100_32_alloc,
    .free = (ProtocolFree)protocol_em4100_free,
    .get_data = (ProtocolGetData)protocol_em4100_get_data,
    .decoder =
        {
            .decode = (ProtocolDecoderDecode)protocol_em4100_decoder_decode,
            .start = (ProtocolDecoderStart)protocol_em4100_decoder_start,
            .feed = (ProtocolDecoderFeed)protocol_em4100_decoder_feed,
        },
    .encoder =
        {
            .start = (ProtocolEncoderStart)protocol_em4100_encoder_start,
            .yield = (ProtocolEncoderYield)protocol_em4100_encoder_yield,
        },
    .render_data = (ProtocolRenderData)protocol_em4100_render_data,
    .render_brief_data = (ProtocolRenderData)protocol_em4100_render_data,
    .write_data = (ProtocolWriteData)protocol_em4100_write_data,
};

const ProtocolBase protocol_em4100_16 = {
    .name = "EM4100/16",
    .manufacturer = "EM-Micro",
    .data_size = EM4100_DECODED_DATA_SIZE,
    .features = LFRFIDFeatureASK | LFRFIDFeaturePSK,
    .validate_count = 3,
    .alloc = (ProtocolAlloc)protocol_em4100_16_alloc,
    .free = (ProtocolFree)protocol_em4100_free,
    .get_data = (ProtocolGetData)protocol_em4100_get_data,
    .decoder =
        {
            .decode = (ProtocolDecoderDecode)protocol_em4100_decoder_decode,
            .start = (ProtocolDecoderStart)protocol_em4100_decoder_start,
            .feed = (ProtocolDecoderFeed)protocol_em4100_decoder_feed,
        },
    .encoder =
        {
            .start = (ProtocolEncoderStart)protocol_em4100_encoder_start,
            .yield = (ProtocolEncoderYield)protocol_em4100_encoder_yield,
        },
    .render_data = (ProtocolRenderData)protocol_em4100_render_data,
    .render_brief_data = (ProtocolRenderData)protocol_em4100_render_data,
    .write_data = (ProtocolWriteData)protocol_em4100_write_data,
};
