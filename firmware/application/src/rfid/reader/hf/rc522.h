#ifndef RC522_H
#define RC522_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <cmsis_gcc.h>

/*
* rC522CommandWord
*/
#define PCD_IDLE              0x00               //Cancel the current command
#define PCD_AUTHENT           0x0E               //Verification key
#define PCD_RECEIVE           0x08               //Receive data
#define PCD_TRANSMIT          0x04               //send data
#define PCD_TRANSCEIVE        0x0C               //Send and receive numbers according to
#define PCD_RESET             0x0F               //Restoration
#define PCD_CALCCRC           0x03               //CRC calculation

/*
* isO14443ACommandWord
*/
#define PICC_REQIDL           0x26               //Find antenna area Not entering the dormant state
#define PICC_REQALL           0x52               //Find antenna area All card
#define PICC_ANTICOLL1        0x93               //Anti -collision
#define PICC_ANTICOLL2        0x95               //Anti -collision
#define PICC_ANTICOLL3        0x97               //Anti -collision
#define PICC_RATS             0xE0               //Choose to respond

/*
* m1CardCommandWord
*/
#define PICC_AUTHENT1A        0x60               //Verify A secret key
#define PICC_AUTHENT1B        0x61               //Verify B Secret key
#define PICC_READ             0x30               //Read block
#define PICC_WRITE            0xA0               //Write block
#define PICC_DECREMENT        0xC0               //Deduction
#define PICC_INCREMENT        0xC1               //Recharge
#define PICC_RESTORE          0xC2               //Bonus number According to the buffer
#define PICC_TRANSFER         0xB0               //Save slowly Data in the area
#define PICC_HALT             0x50               //Dormant

// gen1ATagCommandWord
#define PICC_MAGICWUPC1       0x40               // backDoorInstruction1
#define PICC_MAGICWUPC2       0x43               // backDoorInstruction2
#define PICC_MAGICWIPEC       0x41               // BACK qing card incision

/* RC522 fifoLengthDefinition */
#define DEF_FIFO_LENGTH         64               //FIFO size=64byte

// RC522 crcLengthDefinition
#define DEF_CRC_LENGTH          2

/*
    RC522 theDefaultTimerTimeoutConfiguration,ThisValueCanBeAdjustedDynamically,Through PcdSetTimeout function
    operationStandardM1CardMaximumWaitingTime 25ms
    weCanIncreaseTimeoutToCompatibleWithSomeDullCards
    forExample,SomeBraceletSimulationCards,SuchAsSomeOtherHardwareSimulationCards,SuchAsColorChangingDragons
    ifTheTimeoutValueIsTooSmall,YouMayNotBeAbleToReadTheUid (gen1A)Card!
*/
#define DEF_COM_TIMEOUT         25

// dataIoLengthDefinition
#define MAX_MIFARE_FRAME_SIZE   18                              // biggest Mifare frame is answer to a read (one block = 16 Bytes) + 2 Bytes CRC
#define MAX_MIFARE_PARITY_SIZE  3                               // need 18 parity bits for the 18 Byte above. 3 Bytes are enough to store these
#define CARD_MEMORY_SIZE        4096

/////////////////////////////////////////////////////////////////////
// MF522 registerDefinition
/////////////////////////////////////////////////////////////////////
// PAGE 0
#define     RFU00                 0x00    //reserve
#define     CommandReg            0x01    //Start and stop command execution
#define     ComIEnReg             0x02    //Enable/Disable for interrupt request transmission
#define     DivlEnReg             0x03    //Interrupt request transmission enable
#define     ComIrqReg             0x04    //Including interrupt request signs
#define     DivIrqReg             0x05    //Including interrupt request signs
#define     ErrorReg              0x06    //Error sign, indicate the error status of the previous command executing
#define     Status1Reg            0x07    //Including communication status identification
#define     Status2Reg            0x08    //Including the status logo of the receiver and the transmitter
#define     FIFODataReg           0x09    //64ByteFifoBufferInputAndOutput
#define     FIFOLevelReg          0x0A    //Instructed the number of bytes stored in FIFO
#define     WaterLevelReg         0x0B    //Define the FIFO depth of FIFO overflow and overflow alarm
#define     Control522Reg         0x0C    //Different control registers
#define     BitFramingReg         0x0D    //Adjustment of position -oriented frames
#define     CollReg               0x0E    //The position of the first -bit conflict detected on the RF interface
#define     RFU0F                 0x0F    //reserve
// PAGE 1
#define     RFU10                 0x10    //reserve
#define     ModeReg               0x11    //Define common mode of sending and receiving
#define     TxModeReg             0x12    //Define the data transmission rate of the sending process
#define     RxModeReg             0x13    //Define the data transmission rate during the receiving process
#define     TxControlReg          0x14    //controlTheLogicalCharacteristicsOfTheAntennaDriverDisciplinedTX1AndTX2
#define     TxAutoReg             0x15    //Control antenna drive settings
#define     TxSelReg              0x16    //Select the internal source of antenna drive
#define     RxSelReg              0x17    //Select the internal receiver settings
#define     RxThresholdReg        0x18    //Select the threshold of the decoder
#define     DemodReg              0x19    //Define the settings of the demodulator
#define     RFU1A                 0x1A    //reserve
#define     RFU1B                 0x1B    //reserve
#define     MfTxReg               0x1C    //Control ISO 14443/ 106kbit/ SCOMMUNICATIONTHEMIFAREMODE (FOREXAMPLE,TheCalculationOfTheCouplingSchoolTest)
#define     MfRxReg               0x1D    //Control ISO 14443/ 106kbit/ SCOMMUNICATIONTHEMIFAREMODE (FOREXAMPLE,TheCalculationOfTheCouplingSchoolTest)
#define     RFU1E                 0x1E    //reserve
#define     SerialSpeedReg        0x1F    //The rate of selecting serial UART interface
// PAGE 2
#define     RFU20                 0x20    //reserve
#define     CRCResultRegM         0x21    //Show the actual MSB value of CRC computing
#define     CRCResultRegL         0x22    //Show the actual LSB value of CRC computing
#define     RFU23                 0x23    //reserve
#define     ModWidthReg           0x24    //settingsToControlModWidth
#define     RFU25                 0x25    //reserve
#define     RFCfgReg              0x26    //Configuration receiver gain
#define     GsNReg                0x27    //selectTheModulationConductivityOfTheAntennaDriveTube (tX1AndTX2)
#define     CWGsCfgReg            0x28    //Select the modulation of the antenna drive tube foot
#define     ModGsCfgReg           0x29    //Select the modulation of the antenna drive tube foot
#define     TModeReg              0x2A    //Define the settings of internal timers
#define     TPrescalerReg         0x2B    //Define the settings of internal timers
#define     TReloadRegH           0x2C    //Describe the 16 -bit timer heavy loading value
#define     TReloadRegL           0x2D    //Describe the 16 -bit timer heavy loading value
#define     TCounterValueRegH     0x2E
#define     TCounterValueRegL     0x2F    //Show the actual timer value of the 16 -bit length
// PAGE 3
#define     RFU30                 0x30    //reserve
#define     TestSel1Reg           0x31    //Common test signal configuration
#define     TestSel2Reg           0x32    //Common test signal configuration and PRBS control
#define     TestPinEnReg          0x33    //d1D7OutputDrivesEnablePipeTube (onlyForSerialInterface)
#define     TestPinValueReg       0x34    //defineTheValueOfTheD1D7AsTheI/oBus
#define     TestBusReg            0x35    //Show the status of the internal test bus
#define     AutoTestReg           0x36    //Control the number self -test
#define     VersionReg            0x37    //Display version
#define     AnalogTestReg         0x38    //controlPipeFootAuX1AndAuX2
#define     TestDAC1Reg           0x39    //defineTheTestValueOfTestDaC1
#define     TestDAC2Reg           0x3A    //defineTheTestValueOfTestDaC2
#define     TestADCReg            0x3B    //Display the actual values of ADCI and Q channels
#define     RFU3C                 0x3C    //reserve
#define     RFU3D                 0x3D    //reserve
#define     RFU3E                 0x3E    //reserve
#define     RFU3F                 0x3F    //reserve


/////////////////////////////////////////////////////////////////////
// functionAndTypeDefinition
/////////////////////////////////////////////////////////////////////


// highEfficiencyConversion4ByteDataIsTheValueOfU32Type
#define BYTES4_TO_U32(src) (__REV(*((uint32_t*)src)))

// getTheBitLengthOfTheStaticByteArray
#define U8ARR_BIT_LEN(src) ((sizeof(src)) * (8))

// basicStructurePackagingOfLabelInformation
typedef struct {
    uint8_t uid[10];  // theByteArrayOfTheCardNumber,TheLongest10Byte
    uint8_t uid_len;  // theLengthOfTheCardNumber
    uint8_t cascade;  // theAntiCollisionLevelValueIs1Representation 4Byte,2Represents7Byte,3Means10Byte
    uint8_t sak;      // chooseToConfirm
    uint8_t atqa[2];  // requestResponse
} picc_14a_tag_t;

#ifdef __cplusplus
extern "C" {
#endif
// Device control
void pcd_14a_reader_init(void);
void pcd_14a_reader_uninit(void);
void pcd_14a_reader_reset(void);
void pcd_14a_reader_antenna_on(void);
void pcd_14a_reader_antenna_off(void);

// Device register
uint8_t read_register_single(uint8_t Address);
void write_register_single(uint8_t Address, uint8_t value);
void clear_register_mask(uint8_t reg, uint8_t mask);
void set_register_mask(uint8_t reg, uint8_t mask);

// Device communication control
uint16_t pcd_14a_reader_timeout_get(void);
void pcd_14a_reader_timeout_set(uint16_t timeout_ms);

// Device communication interface
uint8_t pcd_14a_reader_bytes_transfer(uint8_t Command,
                                      uint8_t *pIn,
                                      uint8_t  InLenByte,
                                      uint8_t *pOut,
                                      uint16_t *pOutLenBit,
                                      uint16_t maxOutLenBit);
uint8_t pcd_14a_reader_bits_transfer(uint8_t *pTx,
                                     uint16_t  szTxBits,
                                     uint8_t *pTxPar,
                                     uint8_t *pRx,
                                     uint8_t *pRxPar,
                                     uint16_t *pRxLenBit,
                                     uint16_t szRxLenBitMax);

// Device auto append and check 14443-A parity enable or disable.
void pcd_14a_reader_parity_on(void);
void pcd_14a_reader_parity_off(void);

// 14443-A tag operation
uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag);
uint8_t pcd_14a_reader_ats_request(uint8_t *pAts, uint16_t *szAts, uint16_t szAtsBitMax);
uint8_t pcd_14a_reader_atqa_request(uint8_t *resp, uint8_t *resp_par, uint16_t resp_max_bit);

// M1 tag operation
uint8_t pcd_14a_reader_mf1_auth(picc_14a_tag_t *tag, uint8_t type, uint8_t addr, uint8_t *pKey);
void pcd_14a_reader_mf1_unauth(void);
// writeCardOperation
uint8_t pcd_14a_reader_mf1_write_by_cmd(uint8_t cmd, uint8_t addr, uint8_t *p);
uint8_t pcd_14a_reader_mf1_write(uint8_t addr, uint8_t *pData);
// cardReadingOperation
uint8_t pcd_14a_reader_mf1_read_by_cmd(uint8_t cmd, uint8_t addr, uint8_t *p);
uint8_t pcd_14a_reader_mf1_read(uint8_t addr, uint8_t *pData);
// Formation card operation
uint8_t pcd_14a_reader_halt_tag(void);
void pcd_14a_reader_fast_halt_tag(void);

// UID & UFUID tag operation
uint8_t pcd_14a_reader_gen1a_unlock(void);
uint8_t pcd_14a_reader_gen1a_uplock(void);

// MFU tag operation
// --- cardReadOperation
uint8_t pcd_14a_reader_mfuc_read_by_cmd(uint8_t cmd, uint8_t addr, uint8_t *p);
uint8_t pcd_14a_reader_mfuc_read(uint8_t addr, uint8_t *pData);

// CRC calculate
void pcd_14a_reader_calc_crc(uint8_t *pbtData, size_t szLen, uint8_t *pbtCrc);
void crc_14a_calculate(uint8_t *pbtData, size_t szLen, uint8_t *pbtCrc);
void crc_14a_append(uint8_t *pbtData, size_t szLen);
void pcd_14a_reader_crc_computer(uint8_t use522CalcCRC);

// other
uint8_t cascade_to_cmd(uint8_t cascade);
uint32_t get_u32_tag_uid(picc_14a_tag_t *tag);
uint8_t *get_4byte_tag_uid(picc_14a_tag_t *tag, uint8_t *out);
#ifdef __cplusplus
}
#endif

#endif // !RC522_H
