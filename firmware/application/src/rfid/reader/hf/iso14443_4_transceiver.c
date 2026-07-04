#include "iso14443_4_transceiver.h"
#include "rc522.h"
#include "rfid_main.h"
#include "nrf_log.h"
#include <string.h>

static uint8_t g_pcb_block_num = 0;

#define ISO14443_4_PCB_I_BLOCK_MASK      0xC0
#define ISO14443_4_PCB_I_BLOCK           0x00
#define ISO14443_4_PCB_S_BLOCK_MASK      0xF0
#define ISO14443_4_PCB_S_BLOCK           0xF0
#define ISO14443_4_PCB_S_WTX             0xF2
#define ISO14443_4_WTXM_MASK             0x3F
#define ISO14443_4_MAX_WTX_RETRIES       8

void iso14443_4_reset_block_num(void) {
    g_pcb_block_num = 0;
}

bool iso14443_4_transceive(uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len, uint16_t max_rx_len) {
    uint8_t buffer[260]; 
    uint16_t rx_bits = 0;
    uint8_t status;
    uint8_t wtx_count = 0;
    uint16_t default_timeout_ms = pcd_14a_reader_timeout_get();

    if (tx_len > sizeof(buffer) - 3) {
        return false;
    }

    // Construct I-Block
    buffer[0] = 0x02 | (g_pcb_block_num & 0x01);
    memcpy(&buffer[1], tx_data, tx_len);
    
    // Append CRC manually
    crc_14a_append(buffer, 1 + tx_len);
    
    uint16_t frame_len = 1 + tx_len + 2; 
    
    for (;;) {
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, buffer, frame_len, buffer, &rx_bits, sizeof(buffer) * 8);

        if (status != STATUS_HF_TAG_OK || rx_bits < (3 * 8)) {
            pcd_14a_reader_timeout_set(default_timeout_ms);
            return false;
        }

        uint16_t rx_bytes = rx_bits / 8;

        // Verify CRC
        uint8_t crc_calc[2];
        crc_14a_calculate(buffer, rx_bytes - 2, crc_calc);
        if (buffer[rx_bytes - 2] != crc_calc[0] || buffer[rx_bytes - 1] != crc_calc[1]) {
            pcd_14a_reader_timeout_set(default_timeout_ms);
            return false;
        }

        if ((buffer[0] & ISO14443_4_PCB_I_BLOCK_MASK) == ISO14443_4_PCB_I_BLOCK) {
            pcd_14a_reader_timeout_set(default_timeout_ms);
            g_pcb_block_num ^= 1;

            if (rx_bytes - 3 > max_rx_len) {
                return false;
            }

            *rx_len = rx_bytes - 3;
            memcpy(rx_data, &buffer[1], *rx_len);

            return true;
        }

        if ((buffer[0] & ISO14443_4_PCB_S_BLOCK_MASK) == ISO14443_4_PCB_S_BLOCK && (buffer[0] & 0xF7) == ISO14443_4_PCB_S_WTX && rx_bytes >= 4) {
            uint8_t wtxm = buffer[1] & ISO14443_4_WTXM_MASK;
            if (wtxm == 0 || ++wtx_count > ISO14443_4_MAX_WTX_RETRIES) {
                pcd_14a_reader_timeout_set(default_timeout_ms);
                return false;
            }

            uint32_t extended_timeout_ms = (uint32_t)default_timeout_ms * wtxm;
            if (extended_timeout_ms > UINT16_MAX) {
                extended_timeout_ms = UINT16_MAX;
            }
            pcd_14a_reader_timeout_set((uint16_t)extended_timeout_ms);

            buffer[0] = ISO14443_4_PCB_S_WTX;
            buffer[1] = wtxm;
            crc_14a_append(buffer, 2);
            frame_len = 4;
            continue;
        }

        pcd_14a_reader_timeout_set(default_timeout_ms);
        return false;
    }
}
