#ifndef STATUS_H
#define STATUS_H


/////////////////////////////////////////////////////////////////////
// 14a status
/////////////////////////////////////////////////////////////////////
#define     HF_TAG_OK                               (0x00) // IC card operation successful
#define     HF_TAG_NO                               (0x01) // No IC card found
#define     HF_ERR_STAT                             (0x02) // IC Card communication error
#define     HF_ERR_CRC                              (0x03) // IC Card communication verification error
#define     HF_COLLISION                            (0x04) // IC card conflict
#define     HF_ERR_BCC                              (0x05) // IC card BCC error
#define     MF_ERR_AUTH                             (0x06) // MF card verification failed
#define     HF_ERR_PARITY                           (0x07) // IC card parity error
#define     HF_ERR_ATS                              (0x08) // ATS should be present but card NAKed

/////////////////////////////////////////////////////////////////////
// MIFARE status
/////////////////////////////////////////////////////////////////////
#define     DARKSIDE_CANT_FIXED_NT                  (0x20)  // Darkside, the random number cannot be fixed, this situation may appear on the UID card
#define     DARKSIDE_LUCK_AUTH_OK                   (0x21)  // Darkside, the direct verification is successful, maybe the key is just empty
#define     DARKSIDE_NACK_NO_SEND                   (0x22)  // Darkside, the card does not respond to NACK, it may be a card that fixes Nack logic vulnerabilities
#define     DARKSIDE_TAG_CHANGED                    (0x23)  // Darkside, card switching in the process of running DARKSIDE, May is the two cards quickly switched
#define     NESTED_TAG_IS_STATIC                    (0x24)  // Nested, the random number of the card response is fixed
#define     NESTED_TAG_IS_HARD                      (0x25)  // Nested, the random number of the card response is unpredictable


/////////////////////////////////////////////////////////////////////
// lf status
/////////////////////////////////////////////////////////////////////
#define     LF_TAG_OK                               (0x40)  // Some of the low -frequency cards are successful!
#define     EM410X_TAG_NO_FOUND                     (0x41)  // Can't search for valid EM410X tags


/////////////////////////////////////////////////////////////////////
// other status
/////////////////////////////////////////////////////////////////////
#define     STATUS_PAR_ERR                          (0x60)  // The parameter errors transferred by the BLE instruction, or call the parameter error transmitted by certain functions
#define     STATUS_DEVICE_MODE_ERROR                (0x66)  // The mode of the current device is wrong, and the corresponding API cannot be called
#define     STATUS_INVALID_CMD                      (0x67)  // Invalid instruction
#define     STATUS_DEVICE_SUCCESS                   (0x68)  // Device -related operations successfully executed
#define     STATUS_NOT_IMPLEMENTED                  (0x69)  // Calling some unrealized operations, which belongs to the missed error of the developer
#define     STATUS_FLASH_WRITE_FAIL                 (0x70)  // Flash writing failed
#define     STATUS_FLASH_READ_FAIL                  (0x71)  // Flash read failed
#endif
