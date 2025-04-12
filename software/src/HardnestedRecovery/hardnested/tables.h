/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   tables.h
 * Author: vk496
 *
 * Created on 15 de noviembre de 2018, 17:42
 */

#ifndef TABLES_H
#define TABLES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <lzma.h>
#include "../cmdhfmfhard.h"

typedef struct bitflip_info {
    uint32_t len;
    uint8_t *input_buffer;
} bitflip_info;

typedef enum {
    EVEN_STATE = 0,
    ODD_STATE = 1
} odd_even_t;


bitflip_info get_bitflip(odd_even_t odd_num, uint16_t id);
bool decompress(lzma_stream* strm);
void lzma_init_inflate(lzma_stream *strm, uint8_t *inbuf, uint32_t inbuf_len, uint8_t *outbuf, uint32_t outbuf_len);
void lzma_init_decoder(lzma_stream *strm);

#endif /* TABLES_H */

