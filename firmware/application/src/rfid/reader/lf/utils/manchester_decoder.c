#include "manchester_decoder.h"
#include <stdint.h>

//static const uint8_t transitions[] = {0b00000001, 0b10010001, 0b10011011, 0b11111011};
static const ManchesterState manchester_reset_state = ManchesterResetState;

bool manchester_advance(
    ManchesterState state,
    ManchesterEvent event,
    ManchesterState* next_state,
    BitArray* data) {
    bool result = false;
    ManchesterState new_state;
    bitarray_clear(data);

    if(event == ManchesterEventReset) {
        new_state = manchester_reset_state;
    } else {



    }

    *next_state = new_state;
    return result;
}
/*
uint8_t mcst(RAWBUF_TYPE_S *Pdata) {
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
*/
