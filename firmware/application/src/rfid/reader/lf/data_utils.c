#include "data_utils.h"
#include <ctype.h>

//Write2BitDataToRaw,DataBStores0Bit,DataaStores1Bit
void writebit(uint8_t *dataa, uint8_t *datab, uint8_t pos, uint8_t adata) {
    if (adata >= 4) {
        return;
    }
    static uint8_t aimbyte = 0;
    static uint8_t aimbit = 0;
    aimbyte = pos / 8;
    aimbit = pos % 8;
    getbit(adata, 1) ? setbit(dataa[aimbyte], aimbit) : clrbit(dataa[aimbyte], aimbit);
    getbit(adata, 0) ? setbit(datab[aimbyte], aimbit) : clrbit(datab[aimbyte], aimbit);
}

//OutputRaw's2BitCombinationData
uint8_t readbit(uint8_t *dataa, uint8_t *datab, uint8_t pos) {
    static uint8_t aimbyte = 0;
    static uint8_t aimbit = 0;
    aimbyte = pos / 8;
    aimbit = pos % 8;
    return (
               (getbit(dataa[aimbyte], aimbit) << 1) |
               (getbit(datab[aimbyte], aimbit)));
}

// Write 2bit data to RAW (large -end method, 1st place for each Byte's 8th position), datab deposit 0bit, dataa save 1bit 1bit
void writebit_msb(uint8_t *dataa, uint8_t *datab, uint8_t pos, uint8_t adata) {
    if (adata >= 4) {
        return;
    }
    static uint8_t aimbyte = 0;
    static uint8_t aimbit = 0;
    aimbyte = pos / 8;
    aimbit = 7 - (pos % 8);
    getbit(adata, 1) ? setbit(dataa[aimbyte], aimbit) : clrbit(dataa[aimbyte], aimbit);
    getbit(adata, 0) ? setbit(datab[aimbyte], aimbit) : clrbit(datab[aimbyte], aimbit);
}

// Output RAW's 2bit combination data (large -end method, No. 1 of the 8th reading data of each byte)
uint8_t readbit_msb(uint8_t *dataa, uint8_t *datab, uint8_t pos) {
    static uint8_t aimbyte = 0;
    static uint8_t aimbit = 0;
    aimbyte = pos / 8;
    aimbit = 7 - (pos % 8);
    return (
               (getbit(dataa[aimbyte], aimbit) << 1) |
               (getbit(datab[aimbyte], aimbit)));
}

//High and low flip
uint8_t invert_num(uint8_t num) {
    uint8_t temp = 0, sh = 0xf;
    uint8_t i = 0;
    for (i = 0; i < sizeof(uint8_t); i++) {
        temp |= (num & (sh << ((sizeof(uint8_t) - 1 - i) << 2))) << ((i << 3) + 4);
        temp |= (num & (sh << ((sizeof(uint8_t) + i) << 2))) >> ((i << 3) + 4);
    }
    num = ((temp << 2) & 0xcccccccccccccccc) | ((temp >> 2) & 0x3333333333333333);
    num = ((num << 1) & 0xaaaaaaaaaaaaaaaa) | ((num >> 1) & 0x5555555555555555);

    return num;
}

//The original data is converted to the HEX character array with a 2x length
void ByteToHexStr(uint8_t *source, uint8_t *dest, uint8_t sourceLen) {
    uint8_t i, highByte, lowByte;

    for (i = 0; i < sourceLen; i++) {
        highByte = source[i] >> 4;
        lowByte = source[i] & 0x0f;

        highByte += 0x30;

        if (highByte > 0x39)
            dest[i * 2] = highByte + 0x07;
        else
            dest[i * 2] = highByte;

        lowByte += 0x30;
        if (lowByte > 0x39)
            dest[i * 2 + 1] = lowByte + 0x07;
        else
            dest[i * 2 + 1] = lowByte;
    }
    return;
}

void HexStrToByte(uint8_t *source, uint8_t *dest, uint8_t sourceLen) {
    uint8_t i, highByte, lowByte;

    for (i = 0; i < sourceLen; i += 2) {
        highByte = toupper(source[i]);
        lowByte = toupper(source[i + 1]);

        if (highByte > 0x39)
            highByte -= 0x37;
        else
            highByte -= 0x30;

        if (lowByte > 0x39)
            lowByte -= 0x37;
        else
            lowByte -= 0x30;

        dest[i / 2] = (highByte << 4) | lowByte;
    }
    return;
}
