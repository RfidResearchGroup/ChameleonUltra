#ifdef debugt55xx
#include <stdio.h>
#endif

#include "nrf_sdh_soc.h"
#include "nrf_gpio.h"

#include "timeslot.h"
#include "bsp_delay.h"
#include "lf_t55xx_data.h"
#include "lf_reader_data.h"
#include "lf_125khz_radio.h"


#define NRF_LOG_MODULE_NAME lf_t55xx
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


/*
Small machine writing card:
01 00000000000001000010000000010000 0 000

01 1 00010100010010010000110110010010 000 00000000000000000000000000000 000 011

01 1 00010100010010010000110110010110 000
         01010001001001000011011001011000 51243658

10 01010001001001000011011001001000 0 01010001001001000011011001001000 111

|--------------------------------------------------------------------------|
|                      70bit Password write found                          |
|--------------------------------------------------------------------------|
|OP|PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|01010001001001000011011001001000|0|01010001001001000011011001001000|111|    Zone 0 7 blocks to write the current password (password)
|10|01010001001001000011011001001000|0|01010001001001000111011001001000|111|    Zone 0 7 blocks to write the current password (password)
|10|01010001001001000011011001001000|0|00000000000101001000000001010000|000|    Zone 0 0 blocks are written in the current password00148050 (control zone)
|10|01010001001001000011011001001000|0|11111111101001010010000000000100|001|    Zone 0 1 block is written in the current passwordFFA52004 (data)
|11|01010001001001000011011001001000|0|11111111101001010010000000000100|001|    zone 1 1 block is written in the current passwordFFA52004 (data)
|10|01010001001001000011011001001000|0|10100101011100011001011101101010|010|    Zone 0 2 are written in the current passwordA571976A (data)
|11|01010001001001000011011001001000|0|10100101011100011001011101101010|010|    zone 1 2 are written in the current passwordA571976A (data)
|11|01010001001001000011011001001000|0|01100000000000000000100000000000|011|    zone 1 3 blocks are written in the current password60000800 (radio frequency parameter)
|--------------------------------------------------------------------------|
RESET Pack received
|-----------------------------------------|
|       38bit regular write found         |
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
| 10 | 0 | 00000000000101001000000000000 | 000 | 0 area 0 block writing00148050 (control zone)
| 10 | 0 | 111111111010010000000000100 | 001 | 0 area 1 pieceFFA52004 (data)
| 10 | 0 | 1010010101100011000101110101010 | 010 | 0 area 2 pieces of writingA571976A (data)
|-----------------------------------------|
RESET Pack received

Copy Qiji Writing Card:
|-----------------------------------------|
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
| 10 | 0 | 00011001100100000000100111 | 111 | 0 area 7 pieces19920427 (password)
| 10 | 0 | 000000000001010010000000010000 | 000 | 0 area 0 block writing00148050 (control zone)
|-----------------------------------------|
|--------------------------------------------------------------------------|
|OP|PPPPPPPPPPPPPPPPPPPPPPPPPPPPPPPP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|00011001100100100000010000100111|0|11111111101001010010000000000100|001|
|10|00011001100100100000010000100111|0|10100101011111011101110000011010|010|
|10|00011001100100100000010000100111|0|00000000000101001000000001010000|000|
|10|00011001100100100000010000100111|0|11111111101001010010000000000100|001|
|10|00011001100100100000010000100111|0|10100101011111011101110000011010|010|
|--------------------------------------------------------------------------|
|-----------------------------------------|
|OP|L|DDDDDDDDDDDDDDDDDDDDDDDDDDDDDDDD|AAA|
|10|0|11111111101001010010000000000100|001|
|10|0|10100101011111011101110000011010|010|
|-----------------------------------------|
RESET Pack received
*/


static struct {
    uint8_t opcode;
    uint8_t usepassword;
    uint32_t password;
    uint8_t lockBit;
    uint8_t usedata;
    uint32_t data;
    uint8_t blockAddr;
} t55xx_cmd;


// Air function, T55XX writing card does not need to care about the data you read
void empty_callback() { }


//Start the timer and initialize related peripherals, start a low -frequency card reading
void init_t55xx_hw(void) {
    //Registered card reader IO interrupt recovery
    register_rio_callback(empty_callback);
}

void T55xx_SendGap(unsigned int tm) {
    stop_lf_125khz_radio();    // Turn off PWM output
    bsp_delay_us(tm);
    start_lf_125khz_radio();     // Start PWM output
}

void TxBitRfid(uint8_t data) {
    if (data & 1)
        bsp_delay_us(54 * 8);
    else
        bsp_delay_us(24 * 8);
    T55xx_SendGap(9 * 8); //write gap
}

void TxByteRfid(uint8_t data) {
    for (uint8_t n_bit = 0; n_bit < 8; n_bit++) {
        TxBitRfid(data & 1);
        data = data >> 1;
    }
}

// T55XX high -precision timing control function
void T55XX_Timeslot_Callback() {
    T55xx_SendGap(30 * 8); // start gap

    //Send instructions first
    TxBitRfid(t55xx_cmd.opcode >> 1);
    TxBitRfid(t55xx_cmd.opcode & 1);

    //The instruction does not need to be sent when it is 00
    if (t55xx_cmd.opcode != 0) {
        //If you need it after the instruction, you can send the password
        if (t55xx_cmd.usepassword) {
            for (uint8_t i = 0; i < 32; i++) {
                TxBitRfid((t55xx_cmd.password >> (31 - i)) & 1);
            }
        }

        //Process lock position
        if (t55xx_cmd.lockBit == 0 || t55xx_cmd.lockBit == 1) {
            TxBitRfid(t55xx_cmd.lockBit & 1);
        }

        //Only need to send data if there is a need
        if (t55xx_cmd.usedata) {
            for (uint8_t i = 0; i < 32; i++) {
                TxBitRfid((t55xx_cmd.data >> (31 - i)) & 1);
            }
        }

        //Processing address
        if (t55xx_cmd.blockAddr != 255) {
            TxBitRfid(t55xx_cmd.blockAddr >> 2);
            TxBitRfid(t55xx_cmd.blockAddr >> 1);
            TxBitRfid(t55xx_cmd.blockAddr & 1);
        }
    }
}

/**
 * @brief Write to 5577 instructions, this instruction can be read and write
 *
 * @param opcode The operating code must be 1*in normal operation mode, only the reset is 00
 * @param usepassword Whether the password is used, the password is the password mode
 * @param password Password, send it when USepAssWD is valid, 32 BIT, start transmission from the bidding 0
 * @param lockBit Locking position may only be 1 or 0. Passing other values means not using LOCK bit (for password awakening mode)
 * @param usedata Whether the data area is used to transmit the data for 1 time
 * @param data Data, 32 bits, transmitted from the lower bid 0
 * @param blockAddr Block number, 3 bit 0-7 yuan, input 255 means not using this bit (for password wake-up mode)
 */
void T55xx_Send_Cmd(uint8_t opcode, uint8_t usepassword, uint32_t password, uint8_t lockBit, uint8_t usedata, uint32_t data, uint8_t blockAddr) {
    //Password reading mode,        2op(1+bck)  32pw    1(0)            3addr
    //Password writing mode,        2op(1+bck)  32pw    1l      32data  3addr
    //Password wake -up mode,        2op(1+0)    32pw

    //Read the mode directly,        2op(1+bck)          1(0)            3addr
    //Standard writing mode,        2op(1+bck)          1l      32data  3addr

    //This will not be implemented // Standard read page mode,      2op(1+bck)

    //Reset mode,            2op(0+0)

    t55xx_cmd.opcode = opcode;
    t55xx_cmd.usepassword = usepassword;
    t55xx_cmd.password = password;
    t55xx_cmd.lockBit = lockBit;
    t55xx_cmd.usedata = usedata;
    t55xx_cmd.data = data;
    t55xx_cmd.blockAddr = blockAddr;


    // Request timing, and wait for the order operation to complete
    request_timeslot(37 * 1000, T55XX_Timeslot_Callback, true);

    if (opcode != 0) {
        bsp_delay_ms(6);    // Maybe continue to write a card next time, you need to wait more for a while
    } else {
        bsp_delay_ms(1);
    }
}

/**
 * @brief T55XX Write into EM410X data
 *
 * @param passwd The password for the final encryption (also the current password of the card) (is a pointer, 4 -byte width small end byte sequence storage)
 * @param datas After the data of EM410X, you need to call the EM410X_ENCODER calculation
 */
void T55xx_Write_data(uint8_t *passwd, uint8_t *datas) {
    uint32_t blk1data = 0, blk2data = 0, u32passwd = 0;
    //Extract the data and passwords of two blocks
    for (uint8_t dataindex = 0; dataindex < 4; dataindex++) {
        blk1data = blk1data << 8;
        blk1data |= (uint8_t)datas[dataindex];
        u32passwd = u32passwd << 8;
        u32passwd |= (uint8_t)passwd[dataindex];
    }
    for (uint8_t dataindex = 4; dataindex < 8; dataindex++) {
        blk2data = blk2data << 8;
        blk2data |= (uint8_t)datas[dataindex];
    }
    //writeToThePasswordAreaFirst
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, u32passwd, 7);        // 0 area 7 blocks to write the current password (password)
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, u32passwd, 7);        // 0 area 7 blocks to write the current password (password)
    //Then write to the control area
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, 0X00148050, 0);       // 0 area 0 blocks are written in the current password00148050 (control zone)
    //Then write the data
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, blk1data, 1);         // 0 area 1 block is written in the current passwordblk1data (data)
    T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, blk1data, 1);         //zone 1 1 block is written in the current passwordblk1data (data)
    T55xx_Send_Cmd(2, 1, u32passwd, 0, 1, blk2data, 2);         // 0 area 2 are written in the current passwordblk2data (data)
    T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, blk2data, 2);         //zone 1 2 are written in the current passwordblk2data (data)
    //Then write in the radio frequency parameter
    // 2021-12-15 FIX: Writing this data will cause the small card to be unable to write repeatedly
    // T55xx_Send_Cmd(3, 1, u32passwd, 0, 1, 0X60000800, 3);    //zone 1 3 blocks are written in the current password60000800 (radio frequency parameter)

    //Then write again with non -password instructions
    T55xx_Send_Cmd(2, 0, 0, 0, 1, 0X00148050, 0);               // 0 area 0 block writing00148050 (control zone)
    T55xx_Send_Cmd(2, 0, 0, 0, 1, blk1data, 1);                 // 0 area 1 pieceblk1data (data)
    T55xx_Send_Cmd(2, 0, 0, 0, 1, blk2data, 2);                 // 0 area 2 pieces of writingblk2data (data)
    T55xx_Send_Cmd(0, 0, 0, 0, 0, 0, 0);                        //Restart card
}

/**
 * @brief Reset the password function to set the card that is used to set the existing known password into a target password
 *
 * @param oldpasswd The current password of the card (is a pointer, 4 -byte width small end byte sequential storage)
 * @param newpasswd The password for the final encryption (is a pointer, 4 -byte width small end byte sequential storage)
 */
void T55xx_Reset_Passwd(uint8_t *oldpasswd, uint8_t *newpasswd) {
    uint32_t u32oldpasswd = 0, u32newpasswd = 0;
    //Extract the data and passwords of two blocks
    for (uint8_t dataindex = 0; dataindex < 4; dataindex++) {
        u32oldpasswd = u32oldpasswd << 8;
        u32oldpasswd |= (uint8_t)oldpasswd[dataindex];
        u32newpasswd = u32newpasswd << 8;
        u32newpasswd |= (uint8_t)newpasswd[dataindex];
    }

    T55xx_Send_Cmd(2, 1, u32oldpasswd, 0, 1, u32newpasswd, 7);  // 0 area 7 blocks to write new passwords (passwords)
    T55xx_Send_Cmd(2, 1, u32oldpasswd, 0, 1, u32newpasswd, 7);  // 0 area 7 blocks to write new passwords (passwords)
    T55xx_Send_Cmd(0, 0, 0, 0, 0, 0, 0);                        //Restart card
}
