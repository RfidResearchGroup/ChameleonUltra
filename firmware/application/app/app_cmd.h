#ifndef APP_CMD_H
#define APP_CMD_H

#include <stdint.h>
#include "dataframe.h"


typedef data_frame_tx_t* (*cmd_processor)(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

typedef struct {
    uint16_t cmd;
    cmd_processor cmd_before;
    cmd_processor cmd_processor;
    cmd_processor cmd_after;
} cmd_data_map_t;

void on_data_frame_received(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

#endif
