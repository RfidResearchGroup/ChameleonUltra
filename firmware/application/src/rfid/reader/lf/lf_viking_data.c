#define debugviking 1

#ifdef debugviking
#include <stdio.h>
#endif

#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_data.h"
#include "lf_viking_data_i.h"
#include "lf_viking_data.h"
#include "lf_125khz_radio.h"

#define NRF_LOG_MODULE_NAME viking
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static RAWBUF_TYPE_S carddata;
static volatile uint16_t dataindex = 0;          //Record changes along the number of times
static uint8_t cardbufbyte[CARD_BUF_BYTES_SIZE];   //Card data

static volatile uint16_t timeindex = 0;

//Process card data, enter raw Buffer's starting position in Non-sync state.
//After processing the card data, put cardbuf, return 5 normal analysis
//pdata is rawbuffer
static uint8_t mcst(RAWBUF_TYPE_S *Pdata) {
    uint8_t sync = 0;
    uint8_t cardindex = 0; //Record change number
    for (int i = Pdata->startbit; i < RAW_BUF_SIZE * 8; i++) {
        uint8_t thisbit = readbit(Pdata->rawa, Pdata->rawb, i);
        switch (sync) {
            case 1: //Synchronous state
                switch (thisbit) {
                    case 0: //TheSynchronousState1T,Add1Digit0,StillSynchronize
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    case 1: // Synchronous status 1.5T, add 1 digit 1, switch to non -synchronized state
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        sync = 0;
                        break;
                    case 2: //Synchronous2T,Add2Digits10,StillSynchronize
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    default:
                        NRF_LOG_INFO("sync 1 err. i:%d ci:%d case:%d!\n", i, cardindex, thisbit);
                        return 0;
                }
                break;
            case 0: //Non -synchronous state
                switch (thisbit) {
                    case 0: //1TInNonSynchronousState,Add1Digit1,StillNonSynchronous
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        break;
                    case 1: // In non -synchronous status 1.5T, add 2 digits 10, switch to the synchronous state
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(Pdata->hexbuf, Pdata->hexbuf, cardindex, 0);
                        cardindex++;
                        sync = 1;
                        break;
                    case 2: //The2TOfTheNonSynchronousState,ItIsImpossibleToOccur,ReportAnError
                        NRF_LOG_INFO("sync 0 err. i:%d ci:%d case:%d!\n", i, cardindex, thisbit);
                        return 0;
                    default:
                        NRF_LOG_INFO("sync 0 err. i:%d ci:%d case:%d!\n", i, cardindex, thisbit);
                        return 0;
                }
                break;
        }
        if (cardindex >= CARD_BUF_SIZE * 8)
            break;
    }


    return 1;
}

//Process card, find school inspection and determine whether it is normal
uint8_t viking_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut) {
    if (size != 8) {
        //NRF_LOG_INFO("size err %d!\n", size);
        return 0;
    }

    // Is this the order of bits?
    if (!getbit(pData[0], 0) || !getbit(pData[0], 1) || !getbit(pData[0], 2) || !getbit(pData[0], 3)) {
        return 0;
    }
    if (getbit(pData[0], 4) || getbit(pData[0], 5) || !getbit(pData[0], 6) || getbit(pData[0], 7)) {
        return 0;
    }
    if (getbit(pData[1], 0) || getbit(pData[1], 1) || getbit(pData[1], 2) || getbit(pData[1], 3)) {
        return 0;
    }
    if (getbit(pData[1], 4) || getbit(pData[1], 5) || getbit(pData[1], 6) || getbit(pData[1], 7)) {
        return 0;
    }
    if (getbit(pData[2], 0) || getbit(pData[2], 1) || getbit(pData[2], 2) || getbit(pData[2], 3)) {
        return 0;
    }
    if (getbit(pData[2], 4) || getbit(pData[2], 5) || getbit(pData[2], 6) || getbit(pData[2], 7)) {
        return 0;
    }

    for (int i=0; i<4; i++) {
        for (int j=0;j<8;j++) {
            if (getbit(pData[i+3], 7-j)) {
                pOut[i] |= 1 << j;
            } else {
                pOut[i] &= ~(1<<j);
            } 
        }
    }
    
    uint8_t checksum = 0;
    for (int j=0;j<8;j++) {
        if (getbit(pData[7], 7-j)) {
            checksum |= 1 << j;
        } else {
            checksum &= ~(1<<j);
        }
    }

    checksum = checksum ^ pOut[0] ^ pOut[1] ^ pOut[2] ^ pOut[3] ^ 0x5A;
    if (checksum) {
        return 0;
    }

    return 1;
}

/**
* Code the VIKING card number
* @param: pData card number - ID, fixed 4 length byte
* @param: pOut Output buffer, fixed 8 -length byte
*/
void viking_encoder(uint8_t *pData, uint8_t *pOut) {
    pOut[0] = 0xF2;
    pOut[1] = 0x00;
    pOut[2] = 0x00;
    pOut[3] = pData[0];
    pOut[4] = pData[1];
    pOut[5] = pData[2];
    pOut[6] = pData[3];
    pOut[7] = pOut[3] ^ pOut[4] ^ pOut[5] ^ pOut[6] ^ 0x5A;
}

// Reading the card function, you need to stop calling, return 0 to read the card, 1 is to read
uint8_t viking_acquire(void) {
    if (dataindex >= RAW_BUF_SIZE * 8) {

        carddata.startbit = 255;
        for (int i = 1; i < (RAW_BUF_SIZE * 8) - 64; i++) {
            if (readbit(carddata.rawa, carddata.rawb, i) == 0) { // start with 00102
                if ((readbit(carddata.rawa, carddata.rawb, i + 1) != 0) ||
                    (readbit(carddata.rawa, carddata.rawb, i + 2) != 1) ||
                    (readbit(carddata.rawa, carddata.rawb, i + 3) != 0) ||
                    (readbit(carddata.rawa, carddata.rawb, i + 4) != 2)) {
                    continue;
                }
                if (readbit(carddata.rawa, carddata.rawb, i -1) == 0) {
                    NRF_LOG_INFO("000102 card\r\n");
                } else {
                    NRF_LOG_INFO("x30102 card\r\n");
                    writebit(carddata.rawa, carddata.rawb, i-1, 0);
                }
            } else { // not valid start.
                continue;
            }

            uint8_t non_zeros = 0;
            for (int j=0; j<16; j++) {
                non_zeros += readbit(carddata.rawa, carddata.rawb, i + j + 5);
            }
            if (non_zeros) {  // it should have had a bunch of 0s, otherwise it is not valid preamble.
                continue;
            }

            carddata.startbit = i-1;
            break;
        }

        // If you find the right beginning to deal with it
        if (carddata.startbit != 255 && carddata.startbit < (RAW_BUF_SIZE * 8) - 64) {
            //Guarantee card data can be fully analyzed

            #ifdef debugviking
            char xx[64] = {0};
            for (int i = 0; i < 64; i++) {
                xx[i] = readbit(carddata.rawa, carddata.rawb, i + carddata.startbit);
            }
            NRF_LOG_HEXDUMP_INFO(xx, 64);

            NRF_LOG_INFO("mcst,start: %d\r\n",carddata.startbit);

            NRF_LOG_HEXDUMP_INFO(carddata.timebuf, 64);
            NRF_LOG_HEXDUMP_INFO(&carddata.timebuf[64], 64);
            NRF_LOG_INFO("timebuf: %d\r\n", timeindex);
            #endif

            if (mcst(&carddata) == 1) {
                //Card normal analysis
                char yy[CARD_BUF_SIZE] = {0};
                for (int i=0; i<CARD_BUF_SIZE;i++) {
                    for (int j=0;j<8;j++) {
                        yy[i] |= getbit(carddata.hexbuf[i], j) << (7-j);
                    }
                }
                NRF_LOG_HEXDUMP_INFO(yy, CARD_BUF_SIZE); // BITS ARE BACKWARDS.

                if (viking_decoder(carddata.hexbuf, CARD_BUF_SIZE, cardbufbyte)) {
                    NRF_LOG_INFO("///Valid viking card\r\n");
                    //Card data check passes
                    dataindex = 0;
                    timeindex = 0;
                    return 1;
                } else {
                    NRF_LOG_INFO("///Invalid viking card\r\n");
                }
            }
        } else {
            #ifdef debugviking
            char xx[128] = {0};
            for (int i = 0; i < 128; i++) {
                xx[i] = readbit(carddata.rawa, carddata.rawb, i);
            }
            NRF_LOG_HEXDUMP_INFO(xx, 128);

            NRF_LOG_INFO("NO start\r\n");
            #endif
        }

        // Start a new cycle
        dataindex = 0;
        timeindex = 0;
    }
    return 0;
}

//GPIO interrupt recovery function is used to detect the descending edge
static void GPIO_INT0_callback(void) {
    static uint32_t thistimelen = 0;
    thistimelen = get_lf_counter_value();
    if (thistimelen > 16) {
        static uint8_t cons_temp = 0;
        if (dataindex < RAW_BUF_SIZE * 8) {
            if (thistimelen <= 40) { // 16 cycles
                cons_temp = 0;
            } else if (thistimelen <= 56) { // 16 cycles
                cons_temp = 1;
            } else if (thistimelen <= 72) { // 16 cycles
                cons_temp = 2;
            } else if (thistimelen <= 88) { // Missed a pulse  (probably 1 + 0)
                cons_temp = 4;
            } else if (thistimelen <= 104) { // Missed a pulse (probably 1 + 1)
                cons_temp = 5;
            } else if (thistimelen <= 120) { // Missed a pulse (probably 1 + 2)
                cons_temp = 6;
            } else {
                cons_temp = 3;
            }

            if (cons_temp>3) {
                writebit(carddata.rawa, carddata.rawb, dataindex, 1);
                dataindex++;
                cons_temp -= 4;
            }

            if (dataindex < RAW_BUF_SIZE*8) {
                writebit(carddata.rawa, carddata.rawb, dataindex, cons_temp);
            }
            
            if (timeindex < RAW_BUF_SIZE*8) {
                if (thistimelen>255) {
                    thistimelen = 255;
                }
                carddata.timebuf[timeindex++] = thistimelen;
            }

            dataindex++;
        }
        clear_lf_counter_value();
    } else {
        if (timeindex < RAW_BUF_SIZE*8) {
            if (thistimelen>255) {
                thistimelen = 255;
            }
            carddata.timebuf[timeindex++] = thistimelen;
        }
    }

    uint16_t counter = 0;
    do {
        __NOP();
    } while (counter++ > 1000);
}

//Start the timer and initialize related peripherals, start a low -frequency card reading
void init_viking_hw(void) {
    //Registered card reader IO interrupt recovery
    register_rio_callback(GPIO_INT0_callback);
}

/**
* Read the card number of the EM410X card within the specified timeout
*/
uint8_t viking_read(uint8_t *uid, uint32_t timeout_ms) {
    uint8_t ret = 0;

    init_viking_hw();           // Initialized decline along the sampling recovery function
    start_lf_125khz_radio();    // Start 125kHz modulation

    // Reading the card during timeout
    autotimer *p_at = bsp_obtain_timer(0);
    // NO_TIMEOUT_1MS(p_at, timeout_ms)
    while (NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        //Execute the card, exit if you read it
        if (viking_acquire()) {
            stop_lf_125khz_radio();
            uid[0] = cardbufbyte[0];
            uid[1] = cardbufbyte[1];
            uid[2] = cardbufbyte[2];
            uid[3] = cardbufbyte[3];
            ret = 1;
            break;
        } else {
            bsp_delay_ms(30);
        }
    }

    if (ret != 1) {  // If the card is not searched, it means that the timeout is over. We must manually end the card reader here.
        stop_lf_125khz_radio();
    }

    dataindex = 0;  // After the end, keep in mind the index of resetting data
    timeindex = 0;

    bsp_return_timer(p_at);
    p_at = NULL;

    return ret;
}
