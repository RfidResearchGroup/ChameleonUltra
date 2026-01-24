#ifndef NDEF_GEN_H
#define NDEF_GEN_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Generate NDEF URI message in the buffer (TLV format).
 * 
 * @param buffer Output buffer.
 * @param max_len Maximum length.
 * @param uri The URI string (e.g., "google.com").
 * @return Total length of generated data.
 */
uint16_t ndef_gen_uri(uint8_t *buffer, uint16_t max_len, const char *uri);

#endif
