#include <stdint.h>
#include <string.h>
#include "app_cmd.h"
#include "data_cmd_value.h"
#include "rfid/nfctag/hf/nfc_mf1.h"
#include "nrf_log.h"

// Firmware-side value block extractor for MIFARE Classic
static data_frame_tx_t *cmd_processor_mf1_extract_value_blocks(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data)
{
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_4096);
    nfc_tag_mf1_information_t *info = (nfc_tag_mf1_information_t *)buffer->buffer;
    uint8_t out[16 * 16]; // up to 16 value blocks (sector/block/value)
    uint8_t out_len = 0;
    for (uint16_t block = 0; block < NFC_TAG_MF1_BLOCK_MAX; ++block)
    {
        uint8_t *blk = info->memory[block];
        if (CheckValueIntegrity(blk))
        {
            uint32_t value = 0;
            ValueFromBlock(&value, blk);
            NRF_LOG_INFO("[VAL] Block %u: value=0x%08X", block, value);
            out[out_len++] = (uint8_t)block;
            memcpy(&out[out_len], blk, 16);
            out_len += 16;
            if (out_len > sizeof(out) - 16)
                break;
        }
    }
    return data_frame_make(cmd, STATUS_SUCCESS, out_len, out);
}

// Prototype for registration
static data_frame_tx_t *cmd_processor_mf1_extract_value_blocks(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);
