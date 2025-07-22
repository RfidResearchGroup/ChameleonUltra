#ifdef debugviking
#include <stdio.h>
#endif

#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_data.h"
#include "lf_viking_data_i.h"
#include "lf_viking_data.h"
#include "lf_125khz_radio.h"
#include "lf_manchester.h"
#include "lf_tag_viking.h"

#define NRF_LOG_MODULE_NAME viking
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static RAWBUF_TYPE_S carddata;
static volatile uint16_t dataindex = 0;          //Record changes along the number of times
#ifdef debugviking
static volatile uint16_t timeindex = 0;          // Each transition we increment index.
#endif
static uint8_t cardbufbyte[CARD_BUF_BYTES_SIZE];   //Card data

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
    uint64_t data = viking_id_to_memory64(pData);
    // Reverse 64-bit number and assign to array.
    data = ((data >> 1)  & 0x5555555555555555ULL) | ((data & 0x5555555555555555ULL) << 1);
    data = ((data >> 2)  & 0x3333333333333333ULL) | ((data & 0x3333333333333333ULL) << 2);
    data = ((data >> 4)  & 0x0F0F0F0F0F0F0F0FULL) | ((data & 0x0F0F0F0F0F0F0F0FULL) << 4);
    data = ((data >> 8)  & 0x00FF00FF00FF00FFULL) | ((data & 0x00FF00FF00FF00FFULL) << 8);
    data = ((data >> 16) & 0x0000FFFF0000FFFFULL) | ((data & 0x0000FFFF0000FFFFULL) << 16);
    data = (data >> 32) | (data << 32);
    pOut[0] = (data >> 56) & 0xFF;
    pOut[1] = (data >> 48) & 0xFF;
    pOut[2] = (data >> 40) & 0xFF;
    pOut[3] = (data >> 32) & 0xFF;
    pOut[4] = (data >> 24) & 0xFF;
    pOut[5] = (data >> 16) & 0xFF;
    pOut[6] = (data >> 8) & 0xFF;
    pOut[7] = data & 0xFF;
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
                    #ifdef debugviking
                    NRF_LOG_INFO("000102 card\r\n");
                    #endif
                } else {
                    #ifdef debugviking
                    NRF_LOG_INFO("x30102 card\r\n");
                    #endif                    
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
            // Capture 64 bits, starting at "startbit".
            char xx[64] = {0};
            for (int i = 0; i < 64; i++) {
                xx[i] = readbit(carddata.rawa, carddata.rawb, i + carddata.startbit);
            }
            NRF_LOG_HEXDUMP_INFO(xx, 64);
            NRF_LOG_INFO("mcst,start: %d\r\n",carddata.startbit); // Tell the startbit.

            // Dump the first 128 timings.
            NRF_LOG_HEXDUMP_INFO(carddata.timebuf, 64);
            NRF_LOG_HEXDUMP_INFO(&carddata.timebuf[64], 64);
            NRF_LOG_INFO("timebuf: %d\r\n", timeindex);
            #endif

            if (mcst(carddata.rawa, carddata.rawb, carddata.hexbuf, carddata.startbit, RAW_BUF_SIZE, 0) == 1) {

                #ifdef debugviking
                // Bits for each byte are backwards.
                char yy[CARD_BUF_SIZE] = {0};
                for (int i=0; i<CARD_BUF_SIZE;i++) {
                    for (int j=0;j<8;j++) {
                        yy[i] |= getbit(carddata.hexbuf[i], j) << (7-j);
                    }
                }
                NRF_LOG_HEXDUMP_INFO(yy, CARD_BUF_SIZE);
                #endif

                if (viking_decoder(carddata.hexbuf, CARD_BUF_SIZE, cardbufbyte)) {
                    #ifdef debugviking
                    NRF_LOG_INFO("///Valid viking card\r\n");
                    #endif
                
                    //Card data check passes
                    dataindex = 0;
                    #ifdef debugviking
                    timeindex = 0;  // Reset timeindex to start capturing new timings.
                    #endif
                    return 1;
                } else {
                    #ifdef debugviking
                    NRF_LOG_INFO("///Invalid viking card\r\n");
                    #endif
                }
            }
        } else {
            #ifdef debugviking
            // No start bit found, dump first 128 bits.
            char xx[128] = {0};
            for (int i = 0; i < 128; i++) {
                xx[i] = readbit(carddata.rawa, carddata.rawb, i);
            }
            NRF_LOG_HEXDUMP_INFO(xx, 64);
            NRF_LOG_HEXDUMP_INFO(&xx[64], 64);
            NRF_LOG_INFO("NO start\r\n");
            #endif
        }

        // Start a new cycle
        dataindex = 0;
        #ifdef debugviking
        timeindex = 0; // Reset timeindex to start capturing new timings.
        #endif
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

            if (cons_temp>3) { // Impossible in Manchester, so we must have missed a transition.
                // Typically what I see is that we missed a `cons_temp=1` transition, so let's
                // treat the missing signal as 48 cycles.
                writebit(carddata.rawa, carddata.rawb, dataindex, 1);
                dataindex++;
                cons_temp -= 4;
            }

            if (dataindex < RAW_BUF_SIZE*8) {
                writebit(carddata.rawa, carddata.rawb, dataindex, cons_temp);
            }
            
            #ifdef debugviking
            if (timeindex < RAW_BUF_SIZE*8) {
                if (thistimelen > 255) { // Max time we can store is 255.
                    thistimelen = 255;
                }
                carddata.timebuf[timeindex++] = thistimelen; // Store duration since last transition.
            }
            #endif

            dataindex++;
        }
        clear_lf_counter_value();
    } else {
        #ifdef debugviking
        if (timeindex < RAW_BUF_SIZE*8) {
            if (thistimelen>255) { // Max time we can store is 255.
                thistimelen = 255;
            }
            carddata.timebuf[timeindex++] = thistimelen;  // Store duration since last transition.
        }
        #endif
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
    #ifdef debugviking    
    timeindex = 0;
    #endif

    bsp_return_timer(p_at);
    p_at = NULL;

    return ret;
}
