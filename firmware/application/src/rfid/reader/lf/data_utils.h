#ifndef __DATA_UTILS_H__
#define __DATA_UTILS_H__

#include <stdint.h>


#ifdef __cplusplus
 extern "C" {
#endif

#define DEBUG_READ_BUFF_CHAR(WATCH, PDATA, LENG) \
  uint8_t (*(WATCH))[(LENG)] = (uint8_t (*)[(LENG)])(PDATA)
#define DEBUG_READ_BREAK(WATCH)  (*(WATCH))[0] = (*(WATCH))[0]

#define setbit(x,y)         (x|=(1<<y))
#define clrbit(x,y)         (x&=~(1<<y))
#define reversebit(x,y)     (x^=(1<<y))
#define getbit(x,y)         ((x) >> (y)&1)

void writebit(uint8_t *dataa, uint8_t *datab, uint8_t pos, uint8_t adata);
uint8_t readbit(uint8_t *dataa, uint8_t *datab, uint8_t pos);
void writebit_msb(uint8_t *dataa, uint8_t *datab, uint8_t pos, uint8_t adata);
uint8_t readbit_msb(uint8_t *dataa, uint8_t *datab, uint8_t pos);
uint8_t invert_num(uint8_t num);
void ByteToHexStr(uint8_t *source, uint8_t *dest, uint8_t sourceLen);
void HexStrToByte(uint8_t *source, uint8_t *dest, uint8_t sourceLen);

#ifdef __cplusplus
}
#endif

#endif
