#ifdef debug410x
#include <stdio.h>
#endif

#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_data.h"
#include "lf_em410x_data_i.h"
#include "lf_em410x_data.h"
#include "lf_125khz_radio.h"
#include "lf_manchester.h"

#define NRF_LOG_MODULE_NAME em410x
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


static RAWBUF_TYPE_S carddata;
static volatile uint8_t dataindex = 0;          //Record changes along the number of times
static uint8_t cardbufbyte[CARD_BUF_BYTES_SIZE];   //Card data

#ifdef debug410x
static uint8_t datatest[256] = { 0x00 };
#endif

//Process card, find school inspection and determine whether it is normal
uint8_t em410x_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut) {
    if (size != 8) {
        //NRF_LOG_INFO("size err %d!\n", size);
        return 0;
    }

    // Nrf_log_info ("Start the decoding data! \ N");

    // The number of iterative data, each time +5 Bit
    uint8_t iteration = 0;
    // The current merged data storage location
    uint8_t merge_pos = 0;

    // Quickly check the head
    uint8_t head_check = 1;
    for (int i = 0; i < 9; i++) {
        head_check &= getbit(pData[i / 8], i % 8);
    }
    // Quickly school to test the tail
    if ((!head_check) || getbit(pData[7], 7)) {
        //NRF_LOG_INFO("head or tail err!\n");
        return 0;
    }

    // Nrf_log_info ("Terrate head and tail detection pass! \ N");

    // Check the data of the X -axis first
    // X -axis verification, every separate 5 BIT check once,
    // After the verification is completed, it is stored to halfbyte
    for (int i = 9; i < size * 8 - 5; i += 5) {
        uint8_t count_bit_x = 0;

        for (int j = i; j < i + 5; j++) {

            // Collect the number of puppet data
            if (getbit(pData[j / 8], j % 8) == 1) {
                count_bit_x += 1;
            }

            if (j != i + 4) {
                // Merged bit data to In UINT8 buffer
                // You need to add left -left coordinates, if it is a high part
                uint8_t first_merge_offset = (iteration % 2) ? 0 : 4;
                uint8_t finally_offset = first_merge_offset + ((i + 5 - 1) - j - 1);
                // Nrf_log_info ("need left move %d Bit.\ n ", finally_offset);
                getbit(pData[j / 8], j % 8) ? (pOut[merge_pos] |= 1 << finally_offset) : (pOut[merge_pos] &= ~(1 << finally_offset));
            }

            // If in the first line, we can go directly to the verification Y Verification of the shaft
            if (iteration == 0 && j != i + 4) {
                uint8_t count_bit_y = 0;
                for (int m = j; m < j + 51; m += 5) {
                    // Nrf_log_info ("The current M coordinate is %d, Data is %d\n", m, pData[m]);
                    if (getbit(pData[m / 8], m % 8) == 1) {
                        count_bit_y += 1;
                    }
                }
                if (count_bit_y % 2) {
                    //NRF_LOG_INFO("bit even parity err at Y-axis from %d to %d!\n", j, j + 51);
                    return 0;
                }
            } // Otherwise, go directly to the next round of verification
        }

        // After a round of verification, don't check it next time Y axis
        iteration += 1;

        // If the remaining number is not 0
        // Explain that the new data processing cycle has entered
        // We need to increase the bidding required for merging bytes
        if (!(iteration % 2)) {
            merge_pos += 1;
            // NRF_LOG_INFO("\n");
        }

        if (count_bit_x % 2) {
            //NRF_LOG_INFO("bit even parity err at X-axis from %d to %d!\n", i, i + 5);
            return 0;
        }
    }
    return 1;
}

// Reading the card function, you need to stop calling, return 0 to read the card, 1 is to read
uint8_t em410x_acquire(void) {
    if (dataindex >= RAW_BUF_SIZE * 8) {
#ifdef debug410x
        {
            for (int i = 0; i < RAW_BUF_SIZE * 8; i++) {
                NRF_LOG_INFO("%d ", readbit(carddata.rawa, carddata.rawb, i));
            }
            NRF_LOG_INFO("///raw data\r\n");
            for (int i = 0; i < RAW_BUF_SIZE * 8; i++) {
                NRF_LOG_INFO("%d ", datatest[i]);
            }
            NRF_LOG_INFO("///time data\r\n");
        }
#endif
        //Looking for goals 0 1111 1111
        carddata.startbit = 255;
        for (int i = 0; i < (RAW_BUF_SIZE * 8) - 8; i++) {
            if (readbit(carddata.rawa, carddata.rawb, i) == 1) {
                carddata.startbit = 0;
                for (int j = 1; j < 8; j++) {
                    carddata.startbit += (uint8_t)readbit(carddata.rawa, carddata.rawb, i + j);
                }
                if (carddata.startbit == 0) {
                    carddata.startbit = i;
                    break;
                } else {
                    carddata.startbit = 255;
                }
            }
        }
        // If you find the right beginning to deal with it
        if (carddata.startbit != 255 && carddata.startbit < (RAW_BUF_SIZE * 8) - 64) {
            //Guarantee card data can be fully analyzed
            //NRF_LOG_INFO("do mac,start: %d\r\n",startbit);
            if (mcst(carddata.rawa, carddata.rawb, carddata.hexbuf, carddata.startbit, RAW_BUF_SIZE, 1) == 1) {
                //Card normal analysis
#ifdef debug410x
                {
                    for (int i = 0; i < CARD_BUF_SIZE; i++) {
                        NRF_LOG_INFO("%02X", carddata.hexbuf[i]);
                    }
                    NRF_LOG_INFO("///card data\r\n");
                }
#endif
                if (em410x_decoder(carddata.hexbuf, CARD_BUF_SIZE, cardbufbyte)) {
                    //Card data check passes
#ifdef debug410x
                    for (int i = 0; i < 5; i++) {
                        NRF_LOG_INFO("%02X", (int)cardbufbyte[i]);
                    }
                    NRF_LOG_INFO("///card dataBYTE\r\n");
#endif
                    dataindex = 0;
                    return 1;
                }
            }
        }
        // Start a new cycle
        dataindex = 0;
    }
    return 0;
}

//GPIO interrupt recovery function is used to detect the descending edge
static void GPIO_INT0_callback(void) {
    static uint32_t thistimelen = 0;
    thistimelen = get_lf_counter_value();
    if (thistimelen > 47) {
        static uint8_t cons_temp = 0;
        if (dataindex < RAW_BUF_SIZE * 8) {
            if (48 <= thistimelen && thistimelen <= 80) {
                cons_temp = 0;
            } else if (80 <= thistimelen && thistimelen <= 112) {
                cons_temp = 1;
            } else if (112 <= thistimelen && thistimelen <= 144) {
                cons_temp = 2;
            } else {
                cons_temp = 3;
            }
            writebit(carddata.rawa, carddata.rawb, dataindex, cons_temp);
#ifdef debug410x
            datatest[dataindex] = thistimelen;
#endif
            dataindex++;
        }
        clear_lf_counter_value();
    }

    uint16_t counter = 0;
    do {
        __NOP();
    } while (counter++ > 1000);
}

//Start the timer and initialize related peripherals, start a low -frequency card reading
void init_em410x_hw(void) {
    //Registered card reader IO interrupt recovery
    register_rio_callback(GPIO_INT0_callback);
}

/**
* Read the card number of the EM410X card within the specified timeout
*/
uint8_t em410x_read(uint8_t *uid, uint32_t timeout_ms) {
    uint8_t ret = 0;

    init_em410x_hw();           // Initialized decline along the sampling recovery function
    start_lf_125khz_radio();    // Start 125kHz modulation

    // Reading the card during timeout
    autotimer *p_at = bsp_obtain_timer(0);
    // NO_TIMEOUT_1MS(p_at, timeout_ms)
    while (NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        //Execute the card, exit if you read it
        if (em410x_acquire()) {
            stop_lf_125khz_radio();
            uid[0] = cardbufbyte[0];
            uid[1] = cardbufbyte[1];
            uid[2] = cardbufbyte[2];
            uid[3] = cardbufbyte[3];
            uid[4] = cardbufbyte[4];
            ret = 1;
            break;
        }
    }

    if (ret != 1) {  // If the card is not searched, it means that the timeout is over. We must manually end the card reader here.
        stop_lf_125khz_radio();
    }

    dataindex = 0;  // After the end, keep in mind the index of resetting data

    bsp_return_timer(p_at);
    p_at = NULL;

    return ret;
}
