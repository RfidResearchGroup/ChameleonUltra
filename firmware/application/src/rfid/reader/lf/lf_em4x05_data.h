#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------- */

#define EM4X05_OPCODE_READ    0x02
#define EM4X05_OPCODE_WRITE   0x01
#define EM4X05_OPCODE_PRCT    0x03
#define EM4X05_OPCODE_DSBL    0x00

#define EM4X05_BLOCK_CONFIG   0
#define EM4X05_BLOCK_PASSWD   1
#define EM4X05_BLOCK_UID      15
#define EM4X69_BLOCK_UID_LO   13
#define EM4X69_BLOCK_UID_HI   14

#define EM4X05_RESPONSE_BITS  45
#define EM4X05_RF_DIV         64
#define EM4X05_RESPONSE_TIMEOUT_TC  300

/* -----------------------------------------------------------------------
 * Data structures
 * --------------------------------------------------------------------- */

typedef struct {
    uint32_t config;        /* block 0: configuration word                  */
    uint32_t uid;           /* UID (block determined by LWR or block 15)    */
    uint32_t uid_hi;        /* EM4x69 only: high word of 64-bit UID         */
    bool     is_em4x69;     /* true if 64-bit UID was successfully read      */
    uint8_t  uid_block;     /* block number where UID was actually read from */
    uint32_t password;      /* password to use for LOGIN (default 0x00000000)*/
    bool     login_required;/* true if tag has RL bit set and login failed   */
} em4x05_data_t;

/* -----------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------- */

bool    em4x05_read(em4x05_data_t *out, uint32_t timeout_ms);
uint8_t scan_em4x05(em4x05_data_t *out);

#ifdef __cplusplus
}
#endif
