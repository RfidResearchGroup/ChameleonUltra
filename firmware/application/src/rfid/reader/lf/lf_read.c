#ifdef debug410x
#include <stdio.h>
#endif

#include "lf_read.h"

#include "bsp_time.h"
#include "bsp_delay.h"
#include "lf_reader_data.h"
#include "lf_em410x_data.h"
#include "lf_125khz_radio.h"
#include "protocols/lfrfid_protocols.h"

#define NRF_LOG_MODULE_NAME lf_read
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


static RAWBUF_TYPE_S carddata;
uint8_t lf_cardbuf[LF_CARD_BUF_SIZE];

static volatile uint32_t dataindex = 0;
uint8_t databuf[512] = { 0x00 };


//Process card data, enter raw Buffer's starting position 2 position (21111 ...)
//After processing the card data, put cardbuf, return 5 normal analysis
//pdata is rawbuffer
uint8_t mcst2(RAWBUF_TYPE_S *Pdata) {
    uint8_t sync = 1;      //After the current interval process is processed, is it on the judgment line
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
                        return 0;
                    default:
                        return 0;
                }
                break;
        }
        if (cardindex >= CARD_BUF_SIZE * 8)
            break;
    }
    return 1;
}

uint8_t lf_read_decoder(uint8_t *pData, uint8_t size, uint8_t *pOut) {
    return 0;
}

void lf_read_encoder(uint8_t *pData, uint8_t *pOut) {
#ifdef EM410X_Encoder_NRF_LOG_INFO
    NRF_LOG_INFO("%d ", count1 % 2);
    NRF_LOG_INFO(" <- Qi Dian verification : Tail code  -> 0\n\n");
#endif // EM410X_Encoder_NRF_LOG_INFO
}

// Reading the card function, you need to stop calling, return 0 to read the card, 1 is to read
uint8_t em410x_acquire2(void) {
    if (dataindex >= RAW_BUF_SIZE * 8) {
#ifdef debug410x
        {
            for (int i = 0; i < RAW_BUF_SIZE * 8; i++) {
                NRF_LOG_INFO("%d ", readbit(carddata.rawa, carddata.rawb, i));
            }
            NRF_LOG_INFO("///raw data\r\n");
            for (int i = 0; i < RAW_BUF_SIZE * 8; i++) {
                NRF_LOG_INFO("%d ", databuf[i]);
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
            if (mcst2(&carddata) == 1) {
                //Card normal analysis
#ifdef debug410x
                {
                    for (int i = 0; i < CARD_BUF_SIZE; i++) {
                        NRF_LOG_INFO("%02X", carddata.hexbuf[i]);
                    }
                    NRF_LOG_INFO("///card data\r\n");
                }
#endif
                if (em410x_decoder(carddata.hexbuf, CARD_BUF_SIZE, lf_cardbuf)) {
                    //Card data check passes
#ifdef debug410x
                    for (int i = 0; i < 5; i++) {
                        NRF_LOG_INFO("%02X", (int)lf_cardbuf[i]);
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
void GPIO_INT0_cb(void) {
    if (dataindex < sizeof(databuf)) {
        uint32_t cntr = get_lf_counter_value();
        if (cntr > 0xff)
            databuf[dataindex] = 0xff;
        else
            databuf[dataindex] = cntr & 0xff;
        dataindex++;
    }

    clear_lf_counter_value();
}

void lf_read_init_hw(void) {
    register_rio_callback(GPIO_INT0_cb);
}

uint8_t lf_read_reader(uint8_t *uid, uint32_t timeout_ms) {
    dataindex = 0;
    memset(databuf, 0, sizeof(databuf));

    lf_read_init_hw();
    start_lf_125khz_radio();

    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, 500)) {
        if (dataindex >= sizeof(databuf)) {

            break;
        }

    }

    stop_lf_125khz_radio();

    bsp_return_timer(p_at);
    p_at = NULL;

    if (dataindex > 0) {
        NRF_LOG_INFO("--> data [%d]", dataindex);
        NRF_LOG_HEXDUMP_INFO(databuf, dataindex - 1);
    } else {
        NRF_LOG_INFO("--> data empty");
    }

    NRF_LOG_INFO("--> protocols count: %d", lfrfid_protocols_size);
    for (int i = 0; i < lfrfid_protocols_size; i++) {
        void* data = lfrfid_protocols[i]->alloc();
        NRF_LOG_INFO("-- protocol: %s %s", lfrfid_protocols[i]->manufacturer, lfrfid_protocols[i]->name);

        //lfrfid_protocols[i]->decoder()

        lfrfid_protocols[i]->free(data);
    }

    NRF_LOG_INFO("--> read done.");

    return 0;
}
