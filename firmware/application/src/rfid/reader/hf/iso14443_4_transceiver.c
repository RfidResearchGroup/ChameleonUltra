#include "iso14443_4_transceiver.h"
#include "rc522.h"
#include "rfid_main.h"
#include "nrf_log.h"
#include <string.h>

static uint8_t g_pcb_block_num = 0;

void iso14443_4_reset_block_num(void) {
    g_pcb_block_num = 0;
}

bool iso14443_4_transceive(uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len, uint16_t max_rx_len) {
    uint8_t buffer[260]; 
    uint16_t rx_bits = 0;
    uint8_t status;

    // Construct I-Block
    buffer[0] = 0x02 | (g_pcb_block_num & 0x01);
    memcpy(&buffer[1], tx_data, tx_len);
    
    // Append CRC manually
    crc_14a_append(buffer, 1 + tx_len);
    
    uint16_t frame_len = 1 + tx_len + 2; 
    
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, buffer, frame_len, buffer, &rx_bits, sizeof(buffer) * 8);
    
    if (status != STATUS_HF_TAG_OK || rx_bits < (3 * 8)) { 
        return false;
    }
    
    uint16_t rx_bytes = rx_bits / 8;
    
    // Verify CRC
    uint8_t crc_calc[2];
    crc_14a_calculate(buffer, rx_bytes - 2, crc_calc);
    if (buffer[rx_bytes - 2] != crc_calc[0] || buffer[rx_bytes - 1] != crc_calc[1]) {
        return false;
    }
    
    g_pcb_block_num ^= 1;
    
    if ((buffer[0] & 0xC0) != 0x00) {
        // Simple implementation: ignore chaining/WTX for now
        return false;
    }
    
    if (rx_bytes - 3 > max_rx_len) {
         return false;
    }
    
    *rx_len = rx_bytes - 3;
    memcpy(rx_data, &buffer[1], *rx_len);
    
    return true;
}
