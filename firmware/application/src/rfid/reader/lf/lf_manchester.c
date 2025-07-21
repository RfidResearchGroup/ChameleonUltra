#include "lf_manchester.h"

//Process card data, enter raw Buffer's starting position in Non-sync state.
//After processing the card data, put cardbuf, return normal analysis
//pdata is rawbuffer
uint8_t mcst(uint8_t *rawa, uint8_t *rawb, uint8_t *hexbuf, uint8_t startbit, uint8_t rawbufsize, uint8_t sync) {
    uint8_t cardindex = 0; //Record change number
    for (int i = startbit; i < rawbufsize * 8; i++) {
        uint8_t thisbit = readbit(rawa, rawb, i);
        switch (sync) {
            case 1: //Synchronous state
                switch (thisbit) {
                    case 0: //TheSynchronousState1T,Add1Digit0,StillSynchronize
                        writebit(hexbuf, hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    case 1: // Synchronous status 1.5T, add 1 digit 1, switch to non -synchronized state
                        writebit(hexbuf, hexbuf, cardindex, 1);
                        cardindex++;
                        sync = 0;
                        break;
                    case 2: //Synchronous2T,Add2Digits10,StillSynchronize
                        writebit(hexbuf, hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(hexbuf, hexbuf, cardindex, 0);
                        cardindex++;
                        break;
                    default:
                        return 0;
                }
                break;
            case 0: //Non -synchronous state
                switch (thisbit) {
                    case 0: //1TInNonSynchronousState,Add1Digit1,StillNonSynchronous
                        writebit(hexbuf, hexbuf, cardindex, 1);
                        cardindex++;
                        break;
                    case 1: // In non -synchronous status 1.5T, add 2 digits 10, switch to the synchronous state
                        writebit(hexbuf, hexbuf, cardindex, 1);
                        cardindex++;
                        writebit(hexbuf, hexbuf, cardindex, 0);
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
        if (cardindex >= rawbufsize * 8) {
            break;
        }
    }
    return 1;
}

