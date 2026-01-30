#ifndef EMV_H
#define EMV_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Scan for an EMV card and return formatted information.
 * 
 * @param buffer Output buffer to store the string result.
 * @param max_len Maximum length of the buffer.
 * @return true if successful, false otherwise.
 */
bool emv_scan(char *buffer, uint16_t max_len);

#endif
