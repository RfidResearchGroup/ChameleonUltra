#ifndef MF1_HOST_PLATFORM_H
#define MF1_HOST_PLATFORM_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tag_base_type.h"

/* Replace only the Nordic storage/platform dependencies. Use the real Classic,
 * ISO14443A, Crypto1 and tag type declarations. No hardware or flash is accessed. */
#define NFC_TAG_H
#define NETDATA_H
#define PACKED __attribute__((packed))
typedef struct {
    uint16_t length;
    uint8_t *buffer;
    uint16_t *crc;
} tag_data_buffer_t;
tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type);
bool fds_write_sync(uint16_t id, uint16_t key, uint16_t length, void *data);

#define NRF_LOG_MODULE_REGISTER()
#define NRF_LOG_INFO(...)
#define NRF_LOG_ERROR(...)

#endif
