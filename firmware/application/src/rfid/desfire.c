#include "desfire.h"
#include "rc522.h"
#include "iso14443_4_transceiver.h"
#include "rfid_main.h"
#include <stdio.h>
#include <string.h>

bool desfire_scan(char *out_buffer, uint16_t max_len) {
    picc_14a_tag_t tag;
    uint8_t status;
    uint8_t tx_buf[64];
    uint8_t rx_buf[256];
    uint16_t rx_len;
    
    out_buffer[0] = '\0';
    
    // Increase timeout
    pcd_14a_reader_timeout_set(500);
    
    // 1. Scan for card
    status = pcd_14a_reader_scan_auto(&tag);
    if (status != STATUS_HF_TAG_OK) {
        snprintf(out_buffer, max_len, "No Card");
        return false;
    }
    
    if (tag.ats_len == 0) {
        snprintf(out_buffer, max_len, "Not DESFire (No ATS)");
        return false;
    }
    
    iso14443_4_reset_block_num();
    
    // 2. Get Version (0x60)
    tx_buf[0] = 0x60;
    
    if (iso14443_4_transceive(tx_buf, 1, rx_buf, &rx_len, sizeof(rx_buf))) {
        // DESFire returns version in 3 frames usually (AF -> AF -> 00)
        // Frame 1: HW Vendor, Type, Subtype, Version, Storage
        if (rx_len > 0) {
             uint8_t vendor = rx_buf[0];
             uint8_t type = rx_buf[1];
             uint8_t storage = rx_buf[5]; // 16=2k, 18=4k, 1A=8k usually (approx)
             
             char type_str[20] = "Unknown";
             if (type == 0x81) strcpy(type_str, "DESFire EV1");
             else if (type == 0x82) strcpy(type_str, "DESFire EV2");
             else if (type == 0x83) strcpy(type_str, "DESFire EV3");
             else if (type == 0x88) strcpy(type_str, "DESFire Light");
             
             snprintf(out_buffer, max_len, "%s (0x%02X), V:0x%02X, S:0x%02X", type_str, type, vendor, storage);
             
             // Get more frames if status is 0xAF (More Data)
             // We can stop here for basic info
        }
    } else {
        snprintf(out_buffer, max_len, "GetVersion Failed");
        return false;
    }
    
    // 3. Get Application IDs (0x6A)
    tx_buf[0] = 0x6A;
    if (iso14443_4_transceive(tx_buf, 1, rx_buf, &rx_len, sizeof(rx_buf))) {
        if (rx_len > 0 && rx_buf[rx_len-1] == 0x00) { // Success
             int apps = (rx_len - 1) / 3;
             char temp[32];
             snprintf(temp, sizeof(temp), ", Apps: %d", apps);
             strncat(out_buffer, temp, max_len - strlen(out_buffer) - 1);
             
             if (apps > 0) {
                 strncat(out_buffer, " [", max_len - strlen(out_buffer) - 1);
                 for (int i=0; i<apps && i<3; i++) { // Show first 3
                     uint32_t aid = (rx_buf[i*3] << 16) | (rx_buf[i*3+1] << 8) | rx_buf[i*3+2];
                     snprintf(temp, sizeof(temp), "%06lX ", (unsigned long)aid);
                     strncat(out_buffer, temp, max_len - strlen(out_buffer) - 1);
                 }
                 strncat(out_buffer, "]", max_len - strlen(out_buffer) - 1);
             }
        }
    }
    
    return true;
}
