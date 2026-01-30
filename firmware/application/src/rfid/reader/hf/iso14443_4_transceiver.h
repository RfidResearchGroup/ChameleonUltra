#ifndef ISO14443_4_TRANSCEIVER_H
#define ISO14443_4_TRANSCEIVER_H

#include <stdint.h>
#include <stdbool.h>

void iso14443_4_reset_block_num(void);
bool iso14443_4_transceive(uint8_t *tx_data, uint16_t tx_len, uint8_t *rx_data, uint16_t *rx_len, uint16_t max_rx_len);

#endif
