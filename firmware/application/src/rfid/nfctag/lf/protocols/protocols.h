#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nrf_pwm.h"

typedef void* (*codec_alloc)(void);
typedef void (*codec_free)(void* codec);
typedef uint8_t* (*codec_get_data)(void* codec);

typedef void (*decoder_start)(void* codec, uint8_t format);
typedef bool (*decoder_feed)(void* codec, uint16_t val);

typedef nrf_pwm_sequence_t* (*modulator)(void* d, uint8_t* buf);

typedef struct {
    decoder_start start;
    decoder_feed feed;
} decoder_t;

typedef struct {
    uint16_t tag_type;
    const size_t data_size;
    codec_alloc alloc;
    codec_free free;
    codec_get_data get_data;
    decoder_t decoder;
    modulator modulator;
} protocol;
