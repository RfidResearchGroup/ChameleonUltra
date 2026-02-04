#ifndef LF_T55XX_DATA_H
#define LF_T55XX_DATA_H

#include <stdint.h>

// T5577 Opcodes
#define T5577_OPCODE_PAGE0 0x02 // 10
#define T5577_OPCODE_PAGE1 0x03 // 11
#define T5577_OPCODE_RESET 0x00 // 00

void t55xx_send_cmd(uint8_t opcode, uint32_t *passwd, uint8_t lock_bit, uint32_t *data, uint8_t blk_addr);
void t55xx_write_data(uint32_t passwd, uint32_t *blks, uint8_t blk_count);
void t55xx_reset_passwd(uint32_t old_passwd, uint32_t new_passwd);

#endif
