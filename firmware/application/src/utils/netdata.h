#ifndef NETDATA_H
#define NETDATA_H

#include <stdbool.h>
#include <stdint.h>

#include "utils.h"

#define NETDATA_MAX_DATA_LENGTH 4096

/*
 * *********************************************************************************************************************************
 *                          Variable length data frame format
 *                                  Designed by proxgrind
 *                                  Date: 20221205
 *
 *      0           1           2 3         45               6 7                    8                8 + n           8 +
 * n + 1 SOF(1byte)  LRC(1byte)  CMD(2byte)  Status(2byte)  Data Length(2byte)  Frame Head LRC(1byte)  Data(length)
 * Frame All LRC(1byte) 0x11       0xEF        cmd(u16)    status(u16)      length(u16)              lrc(u8) data(u8*)
 * lrc(u8)
 *
 *  The data length max is 4096, frame length is 1 + 1 + 2 + 2 + 2 + 1 + n + 1 = (10 + n)
 *  So, one frame will be between 10 and 4106 bytes.
 * *********************************************************************************************************************************
 */

// data frame preamble as sent from/to the client, Network byte order.

typedef struct {
    uint8_t sof;
    uint8_t lrc1;
    uint16_t cmd;
    uint16_t status;
    uint16_t len;
    uint8_t lrc2;
} PACKED netdata_frame_preamble_t;

#define NETDATA_FRAME_SOF 0x11

typedef struct {
    uint8_t lrc3;
} PACKED netdata_frame_postamble_t;

// For reception and CRC check
typedef struct {
    netdata_frame_preamble_t pre;
    uint8_t data[NETDATA_MAX_DATA_LENGTH];
    netdata_frame_postamble_t foopost;  // Probably not at that offset!
} PACKED netdata_frame_raw_t;

// Command-specific structs are defined in their respective cmd_processor handlers in app_cmd.c

#endif /* NETDATA_H */
