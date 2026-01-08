#include "ndef_gen.h"
#include <string.h>

uint16_t ndef_gen_uri(uint8_t *buffer, uint16_t max_len, const char *uri) {
    if (max_len < 10 + strlen(uri)) return 0;
    
    uint16_t offset = 0;
    uint8_t prefix = 0x00; // No prefix by default
    const char *payload = uri;
    
    // Auto-detect prefix
    if (strncmp(uri, "https://www.", 12) == 0) { prefix = 0x02; payload += 12; }
    else if (strncmp(uri, "http://www.", 11) == 0) { prefix = 0x01; payload += 11; }
    else if (strncmp(uri, "https://", 8) == 0) { prefix = 0x04; payload += 8; }
    else if (strncmp(uri, "http://", 7) == 0) { prefix = 0x03; payload += 7; }
    
    uint8_t payload_len = 1 + strlen(payload); // Prefix + Rest
    
    // NDEF Message Header
    // Record: MB=1, ME=1, SR=1, TNF=1 (Well Known) -> 0xD1
    uint8_t header = 0xD1;
    uint8_t type_len = 1;
    
    // TLV Wrapper (0x03 Length Value)
    buffer[offset++] = 0x03; // NDEF Message TLV
    
    // Calculate NDEF Message Length = Header + TypeLen + PayloadLen + Type + Payload
    uint8_t ndef_msg_len = 1 + 1 + 1 + 1 + payload_len; // 5 + strlen
    
    buffer[offset++] = ndef_msg_len;
    
    // NDEF Record
    buffer[offset++] = header;
    buffer[offset++] = type_len;
    buffer[offset++] = payload_len;
    buffer[offset++] = 'U'; // Type: URI
    buffer[offset++] = prefix;
    memcpy(&buffer[offset], payload, strlen(payload));
    offset += strlen(payload);
    
    // Terminator TLV
    buffer[offset++] = 0xFE;
    
    return offset;
}
