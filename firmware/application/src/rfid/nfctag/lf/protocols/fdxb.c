#include "fdxb.h"

#include <stdlib.h>
#include <string.h>

#include "nordic_common.h"
#include "protocols.h"
#include "tag_base_type.h"
#include "t55xx.h"
#include "utils/diphase.h"

/*
 * FDX-B animal transponder (ISO 11784 / 11785), read-only.
 *
 * Carrier   : 134.2 kHz  (NOT 125 kHz -- see lf_fdxb_data.c)
 * Bit rate  : RF/32
 * Encoding  : differential biphase, inverted convention -- same as
 *             Jablotron, so utils/diphase.c is reused unchanged.
 *
 * Frame: 128 bits
 *   bits 0-10   : header 00000000001
 *   bits 11-127 : 13 groups of (8 data bits + 1 control bit '1')
 *
 * All fields are transmitted LSB-first, unlike EM410x/Jablotron.
 *
 * Reference: Proxmark3 client/src/cmdlffdxb.c
 */

#define FDXB_RAW_SIZE      (128)
#define FDXB_HEADER_BITS   (11)
#define FDXB_GROUPS        (13)

/* Edge intervals in carrier cycles at RF/32.  These are counts of PWM
 * period-end events, so they hold for any carrier frequency -- 32 cycles
 * per bit whether the carrier is 125 kHz or 134.2 kHz. */
#define FDXB_READ_TIME1_BASE        (0x20)  /* 1T   = 32 cycles */
#define FDXB_READ_TIME2_BASE        (0x30)  /* 1.5T = 48 cycles */
#define FDXB_READ_TIME3_BASE        (0x40)  /* 2T   = 64 cycles */
#define FDXB_READ_JITTER_TIME_BASE  (0x06)

#define NRF_LOG_MODULE_NAME fdxb_protocol
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

typedef struct {
    uint8_t data[FDXB_DATA_SIZE];
    uint64_t raw_hi;    /* bit 0 of the frame is the MSB of raw_hi */
    uint64_t raw_lo;
    uint16_t raw_length;
    diphase *modem;
} fdxb_codec;

/*
 * CRC-16/KERMIT: reflected 0x1021 (0x8408), init 0x0000, no final xor.
 *
 * NOTE: this is the variant Proxmark3 uses for FDX-B.  If frames decode
 * with a valid header and valid control bits but always fail CRC, this
 * constant is the first thing to check against a real tag -- a wrong CRC
 * variant rejects every frame silently.
 */
static uint16_t fdxb_crc16(const uint8_t *d, size_t n) {
    uint16_t crc = 0x0000;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? ((crc >> 1) ^ 0x8408) : (crc >> 1);
        }
    }
    return crc;
}

static void fdxb_shift_bit(fdxb_codec *d, bool bit) {
    d->raw_hi = (d->raw_hi << 1) | (d->raw_lo >> 63);
    d->raw_lo = (d->raw_lo << 1) | (bit ? 1 : 0);
}

/* Position 0 = oldest bit received. */
static bool fdxb_get_bit(fdxb_codec *d, uint16_t pos) {
    if (pos < 64) {
        return (d->raw_hi >> (63 - pos)) & 1;
    }
    return (d->raw_lo >> (127 - pos)) & 1;
}

static bool fdxb_get_time(uint8_t interval, uint8_t base) {
    return interval >= (base - FDXB_READ_JITTER_TIME_BASE) &&
           interval <= (base + FDXB_READ_JITTER_TIME_BASE);
}

static uint8_t fdxb_period(uint8_t interval) {
    if (fdxb_get_time(interval, FDXB_READ_TIME1_BASE)) {
        return 0;
    }
    if (fdxb_get_time(interval, FDXB_READ_TIME2_BASE)) {
        return 1;
    }
    if (fdxb_get_time(interval, FDXB_READ_TIME3_BASE)) {
        return 2;
    }
    return 3;
}

static fdxb_codec *fdxb_alloc(void) {
    fdxb_codec *codec = malloc(sizeof(fdxb_codec));
    codec->modem = malloc(sizeof(diphase));
    codec->modem->rp = fdxb_period;
    return codec;
}

static void fdxb_free(fdxb_codec *d) {
    if (d->modem) {
        free(d->modem);
        d->modem = NULL;
    }
    free(d);
}

static uint8_t *fdxb_get_data(fdxb_codec *d) {
    return d->data;
}

/**
 * Reconstruct the 128-bit FDX-B raw frame from 13-byte destuffed data.
 * 
 * Reverse of fdxb_validate(): takes destuffed frame and adds back:
 *   - 11-bit header: 00000000001 (LSB first)
 *   - Control bits: 1 after every 8 data bits
 * 
 * Frame layout (128 bits total):
 *   bits 0-10:   header (00000000001)
 *   bits 11-18:  data byte 0, LSB first
 *   bit 19:      control bit 1
 *   bits 20-27:  data byte 1, LSB first
 *   bit 28:      control bit 1
 *   ... (pattern repeats for all 13 bytes)
 * 
 * @param frame: 13-byte destuffed FDX-B frame
 * @param raw_hi: output - bits 0-63 of 128-bit frame (MSB side)
 * @param raw_lo: output - bits 64-127 of 128-bit frame (LSB side)
 * @return: true if frame is valid (non-null)
 */
static bool fdxb_raw_frame(const uint8_t *frame, uint64_t *raw_hi, uint64_t *raw_lo) {
    if (frame == NULL) {
        return false;
    }
    
    *raw_hi = 0;
    *raw_lo = 0;
    
    // Frame is built LSB-first into a 128-bit register
    // We build into raw_lo first (bits 0-63), then overflow to raw_hi
    uint64_t bits = 0;
    uint8_t bit_count = 0;
    bool storing_hi = false;  // Track which half we're storing to
    
    // Helper macro to add a single bit
#define ADD_BIT(b) do { \
    bits |= (((uint64_t)(b) & 1) << bit_count); \
    bit_count++; \
    if (bit_count == 64) { \
        if (!storing_hi) { \
            *raw_lo = bits; \
            storing_hi = true; \
        } else { \
            *raw_hi = bits; \
        } \
        bits = 0; \
        bit_count = 0; \
    } \
} while(0)
    
    // Add 11-bit header: 00000000001 (LSB first = bit 0 is 1, bits 1-10 are 0)
    ADD_BIT(1);  // header bit 0 (the '1')
    for (int i = 1; i < 11; i++) {
        ADD_BIT(0);  // header bits 1-10 (the '0's)
    }
    
    // Add 13 groups: 8 data bits + 1 control bit '1'
    for (int k = 0; k < FDXB_GROUPS; k++) {
        // Add 8 data bits (LSB first)
        for (int i = 0; i < 8; i++) {
            ADD_BIT((frame[k] >> i) & 1);
        }
        // Add control bit '1'
        ADD_BIT(1);
    }
    
#undef ADD_BIT
    
    return true;
}

static void fdxb_decoder_start(fdxb_codec *d, uint8_t format) {
    memset(d->data, 0, FDXB_DATA_SIZE);
    d->raw_hi = 0;
    d->raw_lo = 0;
    d->raw_length = 0;
    diphase_reset(d->modem);
}

/*
 * Validate the 128-bit window and destuff it.  Called on every new bit
 * once the register is full, so the window slides until a frame aligns --
 * no explicit preamble hunt needed.
 */
static bool fdxb_validate(fdxb_codec *d) {
    /* Header: ten zeros then a one. */
    for (uint16_t i = 0; i < FDXB_HEADER_BITS - 1; i++) {
        if (fdxb_get_bit(d, i)) {
            return false;
        }
    }
    if (!fdxb_get_bit(d, FDXB_HEADER_BITS - 1)) {
        return false;
    }

    /* Destuff: every 9th bit is a control bit and must be 1. */
    uint8_t frame[FDXB_DATA_SIZE];
    for (uint16_t k = 0; k < FDXB_GROUPS; k++) {
        uint16_t base = FDXB_HEADER_BITS + (9 * k);
        if (!fdxb_get_bit(d, base + 8)) {
            return false;
        }
        uint8_t byte_val = 0;
        for (uint16_t i = 0; i < 8; i++) {
            if (fdxb_get_bit(d, base + i)) {
                byte_val |= (1 << i);  /* LSB first */
            }
        }
        frame[k] = byte_val;
    }

    uint16_t stored = (uint16_t)frame[8] | ((uint16_t)frame[9] << 8);
    if (fdxb_crc16(frame, 8) != stored) {
        return false;
    }

    memcpy(d->data, frame, FDXB_DATA_SIZE);
    return true;
}

static bool fdxb_decode_feed(fdxb_codec *d, bool bit) {
    fdxb_shift_bit(d, bit);
    if (d->raw_length < FDXB_RAW_SIZE) {
        d->raw_length++;
        if (d->raw_length < FDXB_RAW_SIZE) {
            return false;
        }
    }
    return fdxb_validate(d);
}

uint8_t fdxb_t55xx_writer(uint8_t *fdxb_data, uint32_t *blks) {
    /**
     * Encode FDX-B frame for T55xx programming.
     * 
     * Reconstructs the full 128-bit FDX-B frame from the 13-byte destuffed data,
     * then packs it into T55xx blocks with Diphase/RF32 config.
     * 
     * Block layout:
     *   Block 0: T5577_FDXB_CONFIG
     *   Block 1: bits 0-31 of 128-bit raw frame
     *   Block 2: bits 32-63 of 128-bit raw frame
     *   Block 3: bits 64-95 of 128-bit raw frame
     *   Block 4: bits 96-127 of 128-bit raw frame
     * 
     * @param fdxb_data: 13-byte FDX-B destuffed frame
     * @param blks: output array (must hold at least 5 elements)
     * @return: number of blocks used (5: config + 4 data blocks)
     */
    if (fdxb_data == NULL) {
        return 0;
    }
    
    // Reconstruct the full 128-bit frame from destuffed data
    uint64_t raw_hi, raw_lo;
    if (!fdxb_raw_frame(fdxb_data, &raw_hi, &raw_lo)) {
        return 0;
    }
    
    // Block 0: T55xx configuration for FDX-B (Diphase, RF/32)
    blks[0] = T5577_FDXB_CONFIG;
    
    // Blocks 1-4: Pack 128-bit frame into four 32-bit words (HIGH bits first, like jablotron)
    blks[1] = (uint32_t)((raw_hi >> 32) & 0xFFFFFFFF);  // bits 96-127
    blks[2] = (uint32_t)(raw_hi & 0xFFFFFFFF);          // bits 64-95
    blks[3] = (uint32_t)((raw_lo >> 32) & 0xFFFFFFFF);  // bits 32-63
    blks[4] = (uint32_t)(raw_lo & 0xFFFFFFFF);          // bits 0-31
    
    return 5;  // config + 4 data blocks (full 128-bit encoded frame)
}

static bool fdxb_decoder_feed(fdxb_codec *d, uint16_t interval) {
    bool bits[2] = {0};
    int8_t bitlen = 0;
    diphase_feed(d->modem, (uint8_t)interval, bits, &bitlen);
    if (bitlen == -1) {
        /* Invalid interval: diphase_feed() has already re-synced the phase.
         * Deliberately do NOT wipe the 128-bit window -- corrupted bits slide
         * out within one frame, and header + 13 control bits + CRC reject any
         * window they still occupy.  Wiping here means a single glitch per
         * frame prevents a read from ever completing. */
        return false;
    }
    for (int i = 0; i < bitlen; i++) {
        if (fdxb_decode_feed(d, bits[i])) {
            return true;
        }
    }
    return false;
}

const protocol fdxb = {
    .tag_type = TAG_TYPE_FDXB,
    .data_size = FDXB_DATA_SIZE,
    .alloc = (codec_alloc)fdxb_alloc,
    .free = (codec_free)fdxb_free,
    .get_data = (codec_get_data)fdxb_get_data,
    /* Decode only.  Emulation would need a 134.2 kHz modulator and is not
     * part of this test -- fdxb is never added to the emulation tag table,
     * so this NULL is not reachable from the tag_emulation path. */
    .modulator = NULL,
    .decoder =
        {
            .start = (decoder_start)fdxb_decoder_start,
            .feed = (decoder_feed)fdxb_decoder_feed,
        },
};

const protocol *fdxb_protocols[] = {
    &fdxb,
};
size_t fdxb_protocols_size = ARRAY_SIZE(fdxb_protocols);
