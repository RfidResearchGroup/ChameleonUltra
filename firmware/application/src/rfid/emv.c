#include "emv.h"
#include "rc522.h"
#include "rfid_main.h"
#include "hex_utils.h"
#include <stdio.h>
#include <string.h>
#include "nrf_log.h"
#include "bsp_delay.h"
#include "iso14443_4_transceiver.h"

// APDU Constants
static const uint8_t APDU_SELECT_PSE[] = {
    0x00, 0xA4, 0x04, 0x00, 0x0E, 
    0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E, 0x44, 0x44, 0x46, 0x30, 0x31, 
    0x00
};

static const uint8_t APDU_SELECT_VISA[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x03, 0x10, 0x10,
    0x00
};

static const uint8_t APDU_SELECT_MC[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x04, 0x10, 0x10,
    0x00
};

static const uint8_t APDU_SELECT_AMEX[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x25, 0x01, 0x01,
    0x00
};

static const uint8_t APDU_SELECT_DISCOVER[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x01, 0x52, 0x30, 0x10,
    0x00
};

// Simple TLV parser helper
static int find_tag(uint8_t *data, uint16_t len, uint16_t tag, uint8_t **value, uint16_t *value_len) {
    uint16_t i = 0;
    while (i < len) {
        uint16_t current_tag = data[i++];
        if ((current_tag & 0x1F) == 0x1F) {
            current_tag = (current_tag << 8) | data[i++];
        }
        
        if (i >= len) break;

        uint16_t current_len = data[i++];
        if (current_len & 0x80) {
             int len_bytes = current_len & 0x7F;
             current_len = 0;
             for (int j = 0; j < len_bytes; j++) {
                 if (i >= len) break;
                 current_len = (current_len << 8) | data[i++];
             }
        }
        
        if (i + current_len > len) break;

        if (current_tag == tag) {
            *value = &data[i];
            *value_len = current_len;
            return 0; // Found
        }
        
        bool constructed = (current_tag >> 8 == 0) ? (current_tag & 0x20) : ((current_tag >> 8) & 0x20);
        if (constructed) {
             if (find_tag(&data[i], current_len, tag, value, value_len) == 0) {
                 return 0;
             }
        }
        
        i += current_len;
    }
    return -1;
}

static int find_tag_raw(uint8_t *data, uint16_t len, uint8_t tag_byte, uint8_t **value, uint16_t *value_len) {
    for (int i = 0; i < len - 2; i++) {
        if (data[i] == tag_byte) {
             uint8_t l = data[i+1];
             if (l < 0x80 && i + 2 + l <= len) {
                 *value = &data[i+2];
                 *value_len = l;
                 return 0;
             }
        }
    }
    return -1;
}

bool emv_scan(char *out_buffer, uint16_t max_len) {
    picc_14a_tag_t tag;
    uint8_t status;
    uint8_t tx_buf[256];
    uint8_t rx_buf[256];
    uint16_t rx_len;
    bool selection_success = false;
    uint16_t last_sw = 0;
    
    out_buffer[0] = '\0';
    
    // Increase timeout for phones
    pcd_14a_reader_timeout_set(500);
    
    // 1. Scan for card
    status = pcd_14a_reader_scan_auto(&tag);
    if (status != STATUS_HF_TAG_OK) {
        snprintf(out_buffer, max_len, "No Card Found");
        return false;
    }
    
    if (tag.ats_len == 0) {
        snprintf(out_buffer, max_len, "Card found but no ATS");
        return false;
    }
    
    iso14443_4_reset_block_num();
    
    // DELAY for Android HCE:
    // Phones need time to initialize the applet after activation.
    bsp_delay_ms(100);
    
    // 2. Try Select PSE (Directory)
    if (iso14443_4_transceive((uint8_t*)APDU_SELECT_PSE, sizeof(APDU_SELECT_PSE), rx_buf, &rx_len, sizeof(rx_buf))) {
        if (rx_len >= 2) {
            last_sw = (rx_buf[rx_len-2] << 8) | rx_buf[rx_len-1];
        }
        
        // Parse AID from FCI
        uint8_t *aid = NULL;
        uint16_t aid_len = 0;
        
        if (find_tag(rx_buf, rx_len, 0x84, &aid, &aid_len) == 0 || find_tag(rx_buf, rx_len, 0x4F, &aid, &aid_len) == 0) {
            // Found AID in PSE, Select it
            tx_buf[0] = 0x00; tx_buf[1] = 0xA4; tx_buf[2] = 0x04; tx_buf[3] = 0x00;
            tx_buf[4] = aid_len;
            memcpy(&tx_buf[5], aid, aid_len);
            tx_buf[5 + aid_len] = 0x00;
            
            if (iso14443_4_transceive(tx_buf, 5 + aid_len + 1, rx_buf, &rx_len, sizeof(rx_buf))) {
                selection_success = true;
            }
        }
    }
    
    // Fallback: Direct Selection if PSE failed
    if (!selection_success) {
        const uint8_t* apdus[] = {APDU_SELECT_VISA, APDU_SELECT_MC, APDU_SELECT_AMEX, APDU_SELECT_DISCOVER};
        int count = 4;
        
        for (int i=0; i<count; i++) {
            // Re-activate tag to ensure fresh state for this AID attempt.
            // Phones can be finicky if you throw multiple SELECTs without re-polling.
            // But we check if it's necessary. If the previous transceive failed (timeout), the state is unknown.
            // It's safer to re-scan.
            
            pcd_14a_reader_scan_auto(&tag); 
            // We ignore result, if it fails, the next transceive will fail anyway.
            // But giving it a chance to WUPA/RATS again helps.
            
            iso14443_4_reset_block_num(); 
            bsp_delay_ms(20); // Small delay after re-activation
            
            if (iso14443_4_transceive((uint8_t*)apdus[i], 13, rx_buf, &rx_len, sizeof(rx_buf))) { 
                if (rx_len >= 2) {
                    uint16_t sw = (rx_buf[rx_len-2] << 8) | rx_buf[rx_len-1];
                    if (sw == 0x9000) {
                        selection_success = true;
                        break;
                    }
                    last_sw = sw; 
                }
            }
        }
    }
    
    if (!selection_success) {
        if (last_sw != 0) {
            snprintf(out_buffer, max_len, "Select Failed. Last SW: %04X", last_sw);
        } else {
            snprintf(out_buffer, max_len, "Select Failed (Timeout/No Resp)");
        }
        return false;
    }
    
    // 5. Get Processing Options (GPO)
    // Using basic empty PDOL
    uint8_t gpo_apdu[] = { 0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00 };
    if (!iso14443_4_transceive(gpo_apdu, sizeof(gpo_apdu), rx_buf, &rx_len, sizeof(rx_buf))) {
         snprintf(out_buffer, max_len, "GPO Failed");
         return false;
    }
    
    // 6. Read AFL
    uint8_t *afl = NULL;
    uint16_t afl_len = 0;
    
    if (rx_buf[0] == 0x80) {
        uint16_t l = rx_buf[1];
        if (l > 2) { 
             afl = &rx_buf[2 + 2];
             afl_len = l - 2;
        }
    } else if (rx_buf[0] == 0x77) {
        if (find_tag(rx_buf, rx_len, 0x94, &afl, &afl_len) != 0) {
             if (find_tag_raw(rx_buf, rx_len, 0x94, &afl, &afl_len) != 0) {
                 snprintf(out_buffer, max_len, "AFL not found");
                 return false;
             }
        }
    }
    
    // Check for Log Entry (9F4D)
    uint8_t *log_entry = NULL;
    uint16_t log_entry_len = 0;
    uint8_t log_sfi = 0;
    uint8_t log_records = 0;
    if (find_tag(rx_buf, rx_len, 0x9F4D, &log_entry, &log_entry_len) == 0 && log_entry_len == 2) {
        log_sfi = log_entry[0];
        log_records = log_entry[1];
    }
    
    // 7. Read Records
    char card_pan[30] = "";
    char card_date[10] = "";
    
    for (int i = 0; i < afl_len; i += 4) {
        uint8_t sfi = afl[i] >> 3;
        uint8_t rec_start = afl[i+1];
        uint8_t rec_end = afl[i+2];
        
        for (uint8_t rec = rec_start; rec <= rec_end; rec++) {
             uint8_t read_rec_apdu[] = { 0x00, 0xB2, rec, (sfi << 3) | 0x04, 0x00 };
             
             if (iso14443_4_transceive(read_rec_apdu, sizeof(read_rec_apdu), rx_buf, &rx_len, sizeof(rx_buf))) {
                 uint8_t *val;
                 uint16_t val_len;
                 
                 if (card_pan[0] == '\0' && find_tag(rx_buf, rx_len, 0x5A, &val, &val_len) == 0) {
                     int pos = 0;
                     for(int k=0; k<val_len && pos < 29; k++) {
                         pos += snprintf(&card_pan[pos], 30-pos, "%02X", val[k]);
                     }
                 } else if (card_pan[0] == '\0') {
                     if (find_tag_raw(rx_buf, rx_len, 0x5A, &val, &val_len) == 0) {
                         int pos = 0;
                         for(int k=0; k<val_len && pos < 29; k++) {
                             pos += snprintf(&card_pan[pos], 30-pos, "%02X", val[k]);
                         }
                     }
                 }
                 
                 if (card_date[0] == '\0' && find_tag(rx_buf, rx_len, 0x5F24, &val, &val_len) == 0) {
                      int pos = 0;
                     for(int k=0; k<val_len && pos < 9; k++) {
                         pos += snprintf(&card_date[pos], 10-pos, "%02X", val[k]);
                     }
                 }
                 
                 if (card_pan[0] != '\0' && card_date[0] != '\0') break;
             }
        }
        if (card_pan[0] != '\0' && card_date[0] != '\0') break;
    }
    
    snprintf(out_buffer, max_len, "PAN: %s, EXP: %s", card_pan, card_date);
    
    // Log info
    if (log_sfi != 0 && log_records > 0) {
        char log_str[32];
        snprintf(log_str, sizeof(log_str), ", Logs: %d", log_records);
        strncat(out_buffer, log_str, max_len - strlen(out_buffer) - 1);
    }
    
    return (card_pan[0] != '\0');
}