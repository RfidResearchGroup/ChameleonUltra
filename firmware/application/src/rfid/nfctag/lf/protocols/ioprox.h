#pragma once

#include <stdint.h>
#include "protocols.h"
#include "fskdemod.h"

// 16-byte payload (data frame for emulation and CLI):
//  0:    Version
//  1:    Facility Code
//  2-3:  Card Number (Big-endian, uint16)
//  4-11: Raw8 (8 bytes of raw card data)
//  12-15: Reserved (0x00000000)

#define IOPROX_DATA_SIZE 16
#define IOPROX_MAX_BITS 256

typedef struct {
    fsk_t *modem;

    uint8_t bits[IOPROX_MAX_BITS];
    uint16_t bit_len;

    uint8_t data[IOPROX_DATA_SIZE];
} ioprox_codec_t;

extern const protocol ioprox;

uint8_t ioprox_t55xx_writer(uint8_t *buf, uint32_t *blks);
bool ioprox_decode_raw_to_data(const uint8_t *raw8, uint8_t *output);
bool ioprox_encode_params_to_data(uint8_t ver, uint8_t fc, uint16_t cn, uint8_t *out);
