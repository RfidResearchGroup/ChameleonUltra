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

static const uint8_t APDU_SELECT_MAESTRO[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x04, 0x30, 0x60,
    0x00
};

static const uint8_t APDU_SELECT_VISA_ELECTRON[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x03, 0x20, 0x10,
    0x00
};

static const uint8_t APDU_SELECT_VPAY[] = {
    0x00, 0xA4, 0x04, 0x00, 0x07,
    0xA0, 0x00, 0x00, 0x00, 0x03, 0x20, 0x20,
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
            if (i >= len) {
                return -1;
            }
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

static void pan_from_bcd(uint8_t *data, uint16_t len, char *out, uint16_t out_len) {
    uint16_t pos = 0;

    for (uint16_t i = 0; i < len && pos + 1 < out_len; i++) {
        uint8_t hi = data[i] >> 4;
        uint8_t lo = data[i] & 0x0F;

        if (hi <= 9) {
            out[pos++] = '0' + hi;
        }
        if (lo <= 9 && pos + 1 < out_len) {
            out[pos++] = '0' + lo;
        } else if (lo == 0x0F) {
            break;
        }
    }
    out[pos] = '\0';
}

static void pan_date_from_track2(uint8_t *data, uint16_t len, char *pan, uint16_t pan_len, char *date, uint16_t date_len) {
    uint16_t digit = 0;
    bool separator_found = false;
    uint8_t exp[2] = {0};
    uint8_t exp_digits = 0;

    for (uint16_t i = 0; i < len; i++) {
        uint8_t nibbles[] = {data[i] >> 4, data[i] & 0x0F};

        for (uint8_t j = 0; j < 2; j++) {
            uint8_t nibble = nibbles[j];

            if (!separator_found) {
                if (nibble <= 9) {
                    if (digit + 1 < pan_len) {
                        pan[digit++] = '0' + nibble;
                        pan[digit] = '\0';
                    }
                } else if (nibble == 0x0D) {
                    separator_found = true;
                }
            } else if (nibble <= 9 && exp_digits < sizeof(exp)) {
                exp[exp_digits++] = nibble;
                if (exp_digits == sizeof(exp) && date_len >= 5) {
                    snprintf(date, date_len, "%02X%02X", exp[0], exp[1]);
                    return;
                }
            }
        }
    }
}

static void mask_pan(const char *pan, char *out, uint16_t out_len) {
    size_t len = strlen(pan);

    if (out_len == 0) {
        return;
    }

    if (len <= 4) {
        snprintf(out, out_len, "%s", pan);
        return;
    }

    if (out_len < len + 1) {
        out[0] = '\0';
        return;
    }

    memset(out, '*', len - 4);
    memcpy(&out[len - 4], &pan[len - 4], 4);
    out[len] = '\0';
}

static void mask_exp(const char *date, char *out, uint16_t out_len) {
    if (out_len == 0) {
        return;
    }

    if (date[0] == '\0') {
        out[0] = '\0';
        return;
    }

    if (strlen(date) >= 4) {
        snprintf(out, out_len, "**%c%c", date[2], date[3]);
    } else {
        snprintf(out, out_len, "**");
    }
}

static uint16_t build_pdol_data(uint8_t *pdol, uint16_t pdol_len, uint8_t *out, uint16_t out_max) {
    uint16_t pos = 0;
    uint16_t i = 0;

    while (i < pdol_len) {
        uint32_t tag = pdol[i++];
        if ((tag & 0x1F) == 0x1F) {
            do {
                if (i >= pdol_len) {
                    return 0;
                }
                tag = (tag << 8) | pdol[i];
            } while (pdol[i++] & 0x80);
        }

        if (i >= pdol_len) {
            return 0;
        }

        uint8_t len = pdol[i++];
        if (pos + len > out_max) {
            return 0;
        }

        memset(&out[pos], 0, len);

        switch (tag) {
            case 0x9F66: // Terminal Transaction Qualifiers
                if (len >= 4) {
                    out[pos + 0] = 0x36;
                    out[pos + 1] = 0x00;
                    out[pos + 2] = 0x00;
                    out[pos + 3] = 0x00;
                }
                break;
            case 0x9F02: // Amount, Authorised
            case 0x9F03: // Amount, Other
                break;
            case 0x9F1A: // Terminal Country Code
                if (len >= 2) {
                    out[pos + 0] = 0x08;
                    out[pos + 1] = 0x40;
                }
                break;
            case 0x95: // TVR
                break;
            case 0x5F2A: // Transaction Currency Code
                if (len >= 2) {
                    out[pos + 0] = 0x09;
                    out[pos + 1] = 0x78;
                }
                break;
            case 0x9A: // Transaction Date
                if (len >= 3) {
                    out[pos + 0] = 0x26;
                    out[pos + 1] = 0x07;
                    out[pos + 2] = 0x04;
                }
                break;
            case 0x9C: // Transaction Type
                break;
            case 0x9F37: // Unpredictable Number
                for (uint8_t j = 0; j < len; j++) {
                    out[pos + j] = 0x55 + j;
                }
                break;
            default:
                break;
        }

        pos += len;
    }

    return pos;
}

bool emv_scan(char *out_buffer, uint16_t max_len) {
    picc_14a_tag_t tag;
    uint8_t status;
    uint8_t tx_buf[256];
    uint8_t rx_buf[256];
    uint16_t rx_len;
    bool selection_success = false;
    uint16_t last_sw = 0;
    uint16_t default_timeout_ms = pcd_14a_reader_timeout_get();
    uint16_t ppse_sw = 0;
    uint16_t aid_sw[7] = {0};
    
    out_buffer[0] = '\0';
    
    // Increase timeout for phones
    pcd_14a_reader_timeout_set(500);
    
    // 1. Scan for card
    status = pcd_14a_reader_scan_auto(&tag);
    if (status != STATUS_HF_TAG_OK) {
        snprintf(out_buffer, max_len, "No Card Found");
        pcd_14a_reader_timeout_set(default_timeout_ms);
        return false;
    }
    
    if (tag.ats_len == 0) {
        snprintf(out_buffer, max_len, "Card found but no ATS");
        pcd_14a_reader_timeout_set(default_timeout_ms);
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
            ppse_sw = last_sw;
        }
        
        // Parse application AID from PSE FCI. Tag 84 is the selected DF name, not a payment AID.
        uint8_t *aid = NULL;
        uint16_t aid_len = 0;
        
        if (last_sw == 0x9000 && find_tag(rx_buf, rx_len, 0x4F, &aid, &aid_len) == 0) {
            if (aid_len == 0 || aid_len > sizeof(tx_buf) - 6) {
                snprintf(out_buffer, max_len, "Invalid AID length");
                pcd_14a_reader_timeout_set(default_timeout_ms);
                return false;
            }

            // Found AID in PSE, Select it
            tx_buf[0] = 0x00; tx_buf[1] = 0xA4; tx_buf[2] = 0x04; tx_buf[3] = 0x00;
            tx_buf[4] = aid_len;
            memcpy(&tx_buf[5], aid, aid_len);
            tx_buf[5 + aid_len] = 0x00;
            
            if (iso14443_4_transceive(tx_buf, 5 + aid_len + 1, rx_buf, &rx_len, sizeof(rx_buf))) {
                if (rx_len >= 2) {
                    last_sw = (rx_buf[rx_len-2] << 8) | rx_buf[rx_len-1];
                    selection_success = (last_sw == 0x9000);
                }
            }
        }
    }
    
    // Fallback: Direct Selection if PSE failed
    if (!selection_success) {
        const uint8_t* apdus[] = {
            APDU_SELECT_VISA,
            APDU_SELECT_VISA_ELECTRON,
            APDU_SELECT_VPAY,
            APDU_SELECT_MC,
            APDU_SELECT_MAESTRO,
            APDU_SELECT_AMEX,
            APDU_SELECT_DISCOVER
        };
        int count = sizeof(apdus) / sizeof(apdus[0]);
        
        for (int i=0; i<count; i++) {
            iso14443_4_reset_block_num(); 
            
            if (iso14443_4_transceive((uint8_t*)apdus[i], 13, rx_buf, &rx_len, sizeof(rx_buf))) { 
                if (rx_len >= 2) {
                    uint16_t sw = (rx_buf[rx_len-2] << 8) | rx_buf[rx_len-1];
                    if (sw == 0x9000) {
                        selection_success = true;
                        break;
                    }
                    aid_sw[i] = sw;
                    last_sw = sw; 
                }
            } else {
                aid_sw[i] = 0xFFFF;
            }
        }
    }
    
    if (!selection_success) {
        snprintf(out_buffer, max_len,
                 "Select Failed. PPSE:%04X AIDs V:%04X VE:%04X VP:%04X MC:%04X MA:%04X AX:%04X DI:%04X",
                 ppse_sw, aid_sw[0], aid_sw[1], aid_sw[2], aid_sw[3], aid_sw[4], aid_sw[5], aid_sw[6]);
        pcd_14a_reader_timeout_set(default_timeout_ms);
        return false;
    }
    
    // 5. Get Processing Options (GPO)
    uint8_t *pdol = NULL;
    uint16_t pdol_len = 0;
    uint8_t pdol_data[128];
    uint16_t pdol_data_len = 0;
    uint8_t gpo_apdu[136];

    if (find_tag(rx_buf, rx_len, 0x9F38, &pdol, &pdol_len) == 0) {
        pdol_data_len = build_pdol_data(pdol, pdol_len, pdol_data, sizeof(pdol_data));
    }

    if (pdol_data_len > sizeof(gpo_apdu) - 8) {
        snprintf(out_buffer, max_len, "PDOL too large");
        pcd_14a_reader_timeout_set(default_timeout_ms);
        return false;
    }

    gpo_apdu[0] = 0x80;
    gpo_apdu[1] = 0xA8;
    gpo_apdu[2] = 0x00;
    gpo_apdu[3] = 0x00;
    gpo_apdu[4] = 2 + pdol_data_len;
    gpo_apdu[5] = 0x83;
    gpo_apdu[6] = pdol_data_len;
    memcpy(&gpo_apdu[7], pdol_data, pdol_data_len);
    gpo_apdu[7 + pdol_data_len] = 0x00;

    if (!iso14443_4_transceive(gpo_apdu, 8 + pdol_data_len, rx_buf, &rx_len, sizeof(rx_buf))) {
         snprintf(out_buffer, max_len, "GPO Failed");
         pcd_14a_reader_timeout_set(default_timeout_ms);
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
                 pcd_14a_reader_timeout_set(default_timeout_ms);
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
    char masked_pan[30] = "";
    char masked_date[10] = "";
    
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
                     pan_from_bcd(val, val_len, card_pan, sizeof(card_pan));
                 } else if (card_pan[0] == '\0') {
                     if (find_tag_raw(rx_buf, rx_len, 0x5A, &val, &val_len) == 0) {
                         pan_from_bcd(val, val_len, card_pan, sizeof(card_pan));
                     }
                 }
                 
                 if (card_date[0] == '\0' && find_tag(rx_buf, rx_len, 0x5F24, &val, &val_len) == 0) {
                      int pos = 0;
                     for(int k=0; k<val_len && pos < 9; k++) {
                         pos += snprintf(&card_date[pos], 10-pos, "%02X", val[k]);
                     }
                 }

                 if ((card_pan[0] == '\0' || card_date[0] == '\0') && find_tag(rx_buf, rx_len, 0x57, &val, &val_len) == 0) {
                     pan_date_from_track2(val, val_len, card_pan, sizeof(card_pan), card_date, sizeof(card_date));
                 }
                 
                 if (card_pan[0] != '\0' && card_date[0] != '\0') break;
             }
        }
        if (card_pan[0] != '\0' && card_date[0] != '\0') break;
    }
    
    mask_pan(card_pan, masked_pan, sizeof(masked_pan));
    mask_exp(card_date, masked_date, sizeof(masked_date));
    snprintf(out_buffer, max_len, "PAN: %s, EXP: %s", masked_pan, masked_date);
    
    // Log info
    if (log_sfi != 0 && log_records > 0) {
        char log_str[32];
        snprintf(log_str, sizeof(log_str), ", Logs: %d", log_records);
        strncat(out_buffer, log_str, max_len - strlen(out_buffer) - 1);
    }
    
    pcd_14a_reader_timeout_set(default_timeout_ms);
    return (card_pan[0] != '\0');
}
