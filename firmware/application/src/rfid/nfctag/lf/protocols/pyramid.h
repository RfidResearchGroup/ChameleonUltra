#pragma once

#include <stdint.h>
#include "protocols.h"
#include "fskdemod.h"

// Farpointe / Pyramid (FSK2a, RF/50, 128-bit frame).
//
// Emulation: the full 128-bit on-air frame (preamble + Wiegand data + parity +
// CRC) is stored verbatim as 16 bytes and replayed by the modulator; no
// encoding is performed on-device.
//
// Reading: ADC samples are FSK-demodulated to a raw bitstream, the 24-bit
// preamble {0x15, 1, 0x7, 1} is located, then odd parity (every 8th bit) and a
// CRC-8/Maxim trailer are validated. The 16-byte frame recovered on a valid
// read matches the emulation layout, so a read result can be written straight
// into a slot. Card number / format length are decoded host-side by the CLI.

#define PYRAMID_DATA_SIZE 16   // 128-bit frame, MSB-first
#define PYRAMID_RAW_BITS 128
#define PYRAMID_MAX_BITS 256   // bit ring buffer for the reader (~2 frames)

typedef struct {
    fsk_t *modem;                   // FSK demodulator (reader path only)
    uint8_t bits[PYRAMID_MAX_BITS]; // recovered raw bitstream
    uint16_t bit_len;
    uint8_t data[PYRAMID_DATA_SIZE];
} pyramid_codec_t;

extern const protocol pyramid;
