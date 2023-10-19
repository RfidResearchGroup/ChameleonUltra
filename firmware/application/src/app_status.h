#ifndef STATUS_H
#define STATUS_H


/////////////////////////////////////////////////////////////////////
// 14a status
/////////////////////////////////////////////////////////////////////
#define     STATUS_HF_TAG_OK                        (0x00) // IC card operation successful
#define     STATUS_HF_TAG_NO                        (0x01) // No IC card found
#define     STATUS_HF_ERR_STAT                      (0x02) // IC Card communication error
#define     STATUS_HF_ERR_CRC                       (0x03) // IC Card communication verification error
#define     STATUS_HF_COLLISION                     (0x04) // IC card conflict
#define     STATUS_HF_ERR_BCC                       (0x05) // IC card BCC error
#define     STATUS_MF_ERR_AUTH                      (0x06) // MF card verification failed
#define     STATUS_HF_ERR_PARITY                    (0x07) // IC card parity error
#define     STATUS_HF_ERR_ATS                       (0x08) // ATS should be present but card NAKed

/////////////////////////////////////////////////////////////////////
// lf status
/////////////////////////////////////////////////////////////////////
#define     STATUS_LF_TAG_OK                        (0x40)  // Some of the low -frequency cards are successful!
#define     STATUS_EM410X_TAG_NO_FOUND              (0x41)  // Can't search for valid EM410X tags


/////////////////////////////////////////////////////////////////////
// other status
/////////////////////////////////////////////////////////////////////
#define     STATUS_PAR_ERR                          (0x60)  // The parameter errors transferred by the BLE instruction, or call the parameter error transmitted by certain functions
#define     STATUS_DEVICE_MODE_ERROR                (0x66)  // The mode of the current device is wrong, and the corresponding API cannot be called
#define     STATUS_INVALID_CMD                      (0x67)  // Invalid instruction
#define     STATUS_SUCCESS                          (0x68)  // Device -related operations successfully executed
#define     STATUS_NOT_IMPLEMENTED                  (0x69)  // Calling some unrealized operations, which belongs to the missed error of the developer
#define     STATUS_FLASH_WRITE_FAIL                 (0x70)  // Flash writing failed
#define     STATUS_FLASH_READ_FAIL                  (0x71)  // Flash read failed
#endif
