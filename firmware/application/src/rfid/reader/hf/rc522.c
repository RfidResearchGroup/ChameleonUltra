#include <string.h>
#include <stdbool.h>
#include <nrf_gpio.h>

#include "nrf_drv_spi.h"
#include "nrf_gpio.h"
#include "app_error.h"

#include "rfid_main.h"
#include "rc522.h"
#include "bsp_delay.h"
#include "bsp_time.h"
#include "app_status.h"
#include "hex_utils.h"
#include "crc_utils.h"

#define NRF_LOG_MODULE_NAME rc522
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define RC522_DOSEL nrf_gpio_pin_clear(HF_SPI_SELECT)
#define RC522_UNSEL nrf_gpio_pin_set(HF_SPI_SELECT)

bool g_is_reader_antenna_on = false;

//CRC 14A calculator, when the MCU performance is too weak, or when the MCU is busy, you can use 522 to calculate CRC
static uint8_t m_crc_computer = 0;
//Whether it is initialized by the card reader
static bool m_reader_is_init = false;

// Communication timeout
static uint16_t g_com_timeout_ms = DEF_COM_TIMEOUT;
static autotimer *g_timeout_auto_timer;

// RC522 SPI
#define SPI_INSTANCE  0 /**< SPI instance index. */
static const nrf_drv_spi_t s_spiHandle = NRF_DRV_SPI_INSTANCE(SPI_INSTANCE);    // SPI instance

#define ONCE_OPT __attribute__((optimize("O3")))

/**
* @brief  :Read register
* @param  :Address:Register address
* @retval :Value in the register
*/
uint8_t read_register_single(uint8_t Address) {
    RC522_DOSEL;

    Address = (uint8_t)(((Address << 1) & 0x7E) | 0x80);

    NRF_SPI0->TXD = Address;
    while (NRF_SPI0->EVENTS_READY == 0);
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;

    NRF_SPI0->TXD = Address;
    while (NRF_SPI0->EVENTS_READY == 0);
    NRF_SPI0->EVENTS_READY = 0;
    Address = NRF_SPI0->RXD;

    RC522_UNSEL;

    return Address;
}

void read_register_buffer(uint8_t Address, uint8_t *pInBuffer, uint8_t len) {
    RC522_DOSEL;

    Address = (((Address << 1) & 0x7E) | 0x80);

    NRF_SPI0->TXD = Address;
    while (NRF_SPI0->EVENTS_READY == 0);    // Waiting for transmission ends
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;    //Read once and give a level

    uint8_t i = 0;
    do {
        //Then start receiving data
        NRF_SPI0->TXD = Address;
        while (NRF_SPI0->EVENTS_READY == 0);    //Waiting for transmission
        NRF_SPI0->EVENTS_READY = 0;
        pInBuffer[i] = NRF_SPI0->RXD;    // Read once and give a level
    } while (++i < len);

    RC522_UNSEL;
}

/**
* @brief  :Write register
* @param  :Address:Register address
*           value: The value to be written
*/
void ONCE_OPT write_register_single(uint8_t Address, uint8_t value) {
    RC522_DOSEL;

    Address = ((Address << 1) & 0x7E);

    // First pass the address first pass the address
    NRF_SPI0->TXD = Address;
    while (NRF_SPI0->EVENTS_READY == 0);
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;

    //  Passing address
    NRF_SPI0->TXD = value;
    while (NRF_SPI0->EVENTS_READY == 0);
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;

    RC522_UNSEL;
}

void write_register_buffer(uint8_t Address, uint8_t *values, uint8_t len) {
    RC522_DOSEL;

    Address = ((Address << 1) & 0x7E);

    NRF_SPI0->TXD = Address;
    while (NRF_SPI0->EVENTS_READY == 0);
    NRF_SPI0->EVENTS_READY = 0;
    (void)NRF_SPI0->RXD;

    uint8_t i = 0;
    do {
        // Passing address
        NRF_SPI0->TXD = values[i];
        while (NRF_SPI0->EVENTS_READY == 0);
        NRF_SPI0->EVENTS_READY = 0;
        (void)NRF_SPI0->RXD;
    } while (++i < len);

    RC522_UNSEL;
}

/**
* @brief  : Register function switch
* @param  : REG: register address
*           mask: Switch
*/
inline void set_register_mask(uint8_t reg, uint8_t mask) {
    write_register_single(reg, read_register_single(reg) | mask);  // set bit mask
}

/**
* @brief  : Register function switch
* @param  : REG: register address
*           mask: Switch
*/
inline void clear_register_mask(uint8_t reg, uint8_t mask) {
    write_register_single(reg, read_register_single(reg) & ~mask);  // clear bit mask
}

/**
* @brief  Initialized card reader
* @retval none
*/
void pcd_14a_reader_init(void) {
    // Make sure you only initialize it once
    if (!m_reader_is_init) {
        // The logo is the state of initialization
        m_reader_is_init = true;

        // Initialize NSS foot GPIO
        nrf_gpio_cfg_output(HF_SPI_SELECT);

        // Initialize SPI
        ret_code_t errCode;

        nrf_drv_spi_config_t spiConfig = NRF_DRV_SPI_DEFAULT_CONFIG;                // Use SPI default configuration
        // Configure the SPI port, pay attention not to set the CSN here, and use the GPIO port control
        spiConfig.miso_pin = HF_SPI_MISO;
        spiConfig.mosi_pin = HF_SPI_MOSI;
        spiConfig.sck_pin = HF_SPI_SCK;
        spiConfig.mode = NRF_DRV_SPI_MODE_0;
        spiConfig.frequency = NRF_DRV_SPI_FREQ_8M;
        // Configure to block operation
        errCode = nrf_drv_spi_init(&s_spiHandle, &spiConfig, NULL, NULL);
        APP_ERROR_CHECK(errCode);

        // Initialized timer
        // This timer is not released after the initialization of the timer, and it always needs to take up
        g_timeout_auto_timer = bsp_obtain_timer(0);
    }
}

/**
* @brief  : Reset the card reader
* @retval : Status value hf_tag_ok, success
*/
void pcd_14a_reader_reset(void) {
    // Make sure it has been initialized before communicating and soft reset
    if (m_reader_is_init) {
        // Softening 522
        write_register_single(CommandReg, PCD_IDLE);
        write_register_single(CommandReg, PCD_RESET);

        bsp_delay_ms(10);

        // Then default does not allow the antenna
        // Please don't continue to make high -frequency antennas
        pcd_14a_reader_antenna_off();

        // Disable the timer of 522, use the MCU timer timeout time
        write_register_single(TModeReg, 0x00);

        // The modulation sending signal is 100%ask
        write_register_single(TxAutoReg, 0x40);
        // Define common mode and receive common mode and receiveMiFare cartoon communication, CRC initial value 0x6363
        write_register_single(ModeReg, 0x3D);

        bsp_delay_ms(10);
    }
}

/**
* @brief  Anti -initial starting card reader
* @retval none
*/
void pcd_14a_reader_uninit(void) {
    // Make sure that the device has been initialized, and then the anti -initialization
    if (m_reader_is_init) {
        m_reader_is_init = false;
        bsp_return_timer(g_timeout_auto_timer);
        nrf_drv_spi_uninit(&s_spiHandle);
    }
}

/**
* @brief  MF522 Communication timeout configuration
* @param  : Timeout_ms: timeout value
*
* @retval none
*/
void pcd_14a_reader_timeout_set(uint16_t timeout_ms) {
    g_com_timeout_ms = timeout_ms;
}

/**
* @brief  MF522 Communication timeout acquisition
*
* @retval Timeout
*/
uint16_t pcd_14a_reader_timeout_get() {
    return g_com_timeout_ms;
}

/**
* @brief  : Through RC522 and ISO14443 cartoon communication
* @param  : Command: RC522 command word
*          PIN: Data sent to the card through RC522
*          Inlenbyte: The byte length of sending the data
*          POUT: The receiving card returns the data
*          POUTLENBIT: Bit the length of the data
* @retval : Status value mi_ok, successful
*/
uint8_t pcd_14a_reader_bytes_transfer(uint8_t Command, uint8_t *pIn, uint8_t  InLenByte, uint8_t *pOut, uint16_t *pOutLenBit, uint16_t maxOutLenBit) {
    uint8_t status      = STATUS_HF_ERR_STAT;
    uint8_t waitFor     = 0x00;
    uint8_t lastBits    = 0;
    uint8_t n           = 0;
    uint8_t pcd_err_val = 0;
    uint8_t not_timeout = 0;
    // Reset the length of the received data
    *pOutLenBit         = 0;

    switch (Command) {
        case PCD_AUTHENT:                       //  MiFare certification
            waitFor = 0x10;                     //  Query the free interrupt logo when the certification card is waiting
            break;

        case PCD_TRANSCEIVE:
            waitFor = 0x30;                     //  Inquiry the receiving interrupt logo position and Leisure interrupt logo
            break;
    }

    write_register_single(CommandReg,       PCD_IDLE);      //  Flushbuffer clearing the internal FIFO read and writing pointer and ErRreg's Bufferovfl logo position is cleared
    clear_register_mask(ComIrqReg,      0x80);          //  When Set1 is cleared, the shielding position of commonricqreg is clear zero
    set_register_mask(FIFOLevelReg,     0x80);          //  Write an empty order

    write_register_buffer(FIFODataReg, pIn, InLenByte); // Write data into FIFODATA
    write_register_single(CommandReg, Command);             // Write command

    if (Command == PCD_TRANSCEIVE) {
        set_register_mask(BitFramingReg, 0x80);     // StartSend places to start the data to send this bit and send and receive commands when it is valid
    }

    if (pOut == NULL) {
        // If the developer does not need to receive data, then return directly after the sending!
        while ((read_register_single(Status2Reg) & 0x07) == 0x03);
        return STATUS_HF_TAG_OK;
    }

    bsp_set_timer(g_timeout_auto_timer, 0);         // Before starting the operation, return to zero over time counting

    do {
        n = read_register_single(ComIrqReg);                // Read the communication interrupt register to determine whether the current IO task is completed!
        not_timeout = NO_TIMEOUT_1MS(g_timeout_auto_timer, g_com_timeout_ms);
    } while (not_timeout && (!(n & waitFor)));  // Exit conditions: timeout interruption, interrupt with empty command commands
    // NRF_LOG_INFO("N = %02x\n", n);

    if (Command == PCD_TRANSCEIVE) {
        clear_register_mask(BitFramingReg, 0x80);       // Clean up allows the startsend bit and the bit length position
    }

    // Whether to receive timeout
    if (not_timeout) {
        // First determine whether there is a place where there is an error register
        if (n & 0x02) {
            // Error occur
            // Read an error logo register BufferOfI CollErr ParityErr ProtocolErr
            pcd_err_val = read_register_single(ErrorReg);
            // Detect whether there are abnormalities
            if (pcd_err_val & 0x01) {               // ProtocolErr Error only appears in the following two cases:
                if (Command == PCD_AUTHENT) {       // During the execution of the MFAUTHENT command, if the number of bytes received by a data stream, the position of the place
                    // Therefore, we need to deal with it well, assuming that there are problems during the verification process, then we need to think that this is normal
                    status = STATUS_MF_ERR_AUTH;
                } else {                            // If the SOF is wrong, the position is set up and the receiver is automatically cleared during the start -up stage, which is effective at the rate of 106kbd
                    NRF_LOG_INFO("Protocol error\n");
                    status = STATUS_HF_ERR_STAT;
                }
            } else if (pcd_err_val & 0x02) {
                // Detecting whether there are even strange errors
                NRF_LOG_INFO("Parity error\n");
                status = STATUS_HF_ERR_PARITY;
            } else if (pcd_err_val & 0x04) {        // Detect whether there are CRC errors
                NRF_LOG_INFO("CRC error\n");
                status = STATUS_HF_ERR_CRC;
            } else if (pcd_err_val & 0x08) {        // There is a conflict to detect the label
                NRF_LOG_INFO("Collision tag\n");
                status = STATUS_HF_COLLISION;
            } else {                                // There are other unrepaired abnormalities
                NRF_LOG_INFO("HF error: 0x%0x2\n", pcd_err_val);
                status = STATUS_HF_ERR_STAT;
            }
        } else {
            // Occasionally occur
            // NRF_LOG_INFO("COM OK\n");
            if (Command == PCD_TRANSCEIVE) {
                n = read_register_single(FIFOLevelReg);                             // Read the number of bytes saved in FIFO
                if (n == 0) { n = 1; }

                lastBits = read_register_single(Control522Reg) & 0x07;          // Finally receive the validity of the byte

                if (lastBits) { *pOutLenBit = (n - 1) * 8 + lastBits; } // N -byte number minus 1 (last byte)+ the number of bits of the last bit The total number of data readings read
                else { *pOutLenBit = n * 8; }                           // Finally received the entire bytes received by the byte valid

                if (*pOutLenBit <= maxOutLenBit) {
                    // Read all the data in FIFO
                    read_register_buffer(FIFODataReg, pOut, n);
                    // Transmission instructions can be considered success when reading normal data!
                    status = STATUS_HF_TAG_OK;
                } else {
                    NRF_LOG_INFO("pcd_14a_reader_bytes_transfer receive response overflow: %d, max = %d\n", *pOutLenBit, maxOutLenBit);
                    // We can't pass the problem with problems, which is meaningless for the time being
                    *pOutLenBit = 0;
                    // Since there is a problem with the data, let's notify the upper layer and inform me
                    status = STATUS_HF_ERR_STAT;
                }
            } else {
                // Non -transmitted instructions, the execution is completed without errors and considered success!
                status = STATUS_HF_TAG_OK;
            }
        }
    } else {
        status = STATUS_HF_TAG_NO;
        // NRF_LOG_INFO("Tag lost(timeout).\n");
    }

    if (status != STATUS_HF_TAG_OK) {
        // If there are certain operations,
        // We may need to remove MFCrypto1On This register logo,
        // Because it may be because of the error encryption communication caused by verification
        clear_register_mask(Status2Reg, 0x08);
    }

    // NRF_LOG_INFO("Com status: %d\n", status);
    return status;
}

/**
* @brief  : Through RC522 and ISO14443 cartoon communication
* @param
*          pTx      : Data sent to the card through RC522
*          szTxBits : Bit length of sending data
*          pTxPar   : The puppet school test of the sending data
*          pRx      : Caps of the data responding to the card response after storing the packaging
*          pRxPar   : The buffer of the strange coupling verification data responding to the card
* @retval : Bit the data of the data responding to the card response when successful,
              Back the corresponding error code when failed.
*/
uint8_t pcd_14a_reader_bits_transfer(uint8_t *pTx, uint16_t  szTxBits, uint8_t *pTxPar, uint8_t *pRx, uint8_t *pRxPar, uint16_t *pRxLenBit, uint16_t szRxLenBitMax) {

    static uint8_t buffer[DEF_FIFO_LENGTH];
    uint8_t status      = 0,
            modulus     = 0,
            i           = 0,
            dataLen     = 0;

    buffer[0] = pTx[0];
    if (szTxBits > 8) {
        // Determine that you need to be merged and you can check the data stream
        if (pTxPar != NULL) {
            // Several bytes need a few bites, so it will
            // Data of BIT with more bytes of the number of bytes
            modulus = dataLen = szTxBits / 8;
            buffer[1] = (pTxPar[0] | (pTx[1] << 1));
            for (i = 2; i < dataLen; i++) {
                // add the remaining prev byte and parity
                buffer[i] = ((pTxPar[i - 1] << (i - 1)) | (pTx[ i - 1] >> (9 - i)));
                // add next byte and push i bits
                buffer[i] |= (pTx[i] << i);
            }
            // add remainder of last byte + end parity
            buffer[dataLen] = ((pTxPar[dataLen - 1] << (i - 1)) | (pTx[dataLen - 1] >> (9 - i)));
            dataLen += 1;
        } else {
            modulus = szTxBits % 8;
            dataLen = modulus > 0 ? (szTxBits / 8 + 1) : (szTxBits / 8);
            // No need to merge the coupling school inspection, it is treated as the outside that has been done here.
            for (i = 1; i < dataLen; i++) {
                buffer[i] = pTx[i];
            }
        }
    } else {
        dataLen = 1;
        modulus = szTxBits;
    }

    set_register_mask(BitFramingReg, modulus);  // Set the last byte transmission n bit
    set_register_mask(MfRxReg, 0x10);  // Need to close the puppet school test to enable

    status = pcd_14a_reader_bytes_transfer(
                 PCD_TRANSCEIVE,
                 buffer,
                 dataLen,                // Data byte count
                 buffer,                 // Receiving buffer
                 pRxLenBit,              // The length of the received data, note that it is the length of the special stream
                 U8ARR_BIT_LEN(buffer)   // The upper limit of the data that can be collected
             );

    clear_register_mask(BitFramingReg, modulus);
    clear_register_mask(MfRxReg, 0x10);  // Enable Qiqi school inspection

    // Simply judge the length of data transmission
    if (status != STATUS_HF_TAG_OK) {
        // NRF_LOG_INFO("pcd_14a_reader_bytes_transfer error status: %d\n", status);
        return status;
    }

    pRx[0] = buffer[0];
    modulus = 0;
    if (*pRxLenBit > 8) {
        // Take the remaining, wait for the statistical number of bytes
        modulus  = *pRxLenBit % 8;
        // Take the number of bytes, wait for the packaging
        dataLen  = *pRxLenBit / 8 + (modulus > 0);
        // Take the Special Number, this is the length of the final data
        *pRxLenBit = *pRxLenBit - modulus;

        // Determine whether the data decoding will overflow
        if (*pRxLenBit > szRxLenBitMax) {
            NRF_LOG_INFO("pcd_14a_reader_bits_transfer decode parity data overflow: %d, max = %d\n", *pRxLenBit, szRxLenBitMax);
            // There must be an overflow here, and the length of the data that is valid is reset to avoid misjudgment from external calls.
            *pRxLenBit = 0;
            return STATUS_HF_ERR_STAT;
        }

        // The process of the separation and dissection process of the unprecedented verification and the data
        for (i = 1; i < dataLen - 1; i++) {
            if (pRxPar != NULL) {
                pRxPar[i - 1] = (buffer[i] & (1 << (i - 1))) >> (i - 1);
            }
            pRx[i] = (buffer[i] >> i) | (buffer[i + 1] << (8 - i));
        }
        if (pRxPar != NULL) {
            pRxPar[i - 1] = (buffer[i] & (1 << (i - 1))) >> (i - 1);
        }
    }
    return STATUS_HF_TAG_OK;
}

/**
* @brief  : ISO14443-A Fast Select
* @param  :tag: tag info buffer
* @retval : if return STATUS_HF_TAG_OK, the tag is selected.
*/
uint8_t pcd_14a_reader_fast_select(picc_14a_tag_t *tag) {
    uint8_t resp[5] = {0}; // theoretically. A usual RATS will be much smaller
    uint8_t uid_resp[4] = {0};
    uint8_t sak = 0x04; // cascade uid
    uint8_t status = STATUS_HF_TAG_OK;
    uint8_t cascade_level = 0;
    uint16_t len;

    // Wakeup
    if (pcd_14a_reader_atqa_request(resp, NULL, U8ARR_BIT_LEN(resp)) != STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_NO;
    }

    // OK we will select at least at cascade 1, lets see if first byte of UID was 0x88 in
    // which case we need to make a cascade 2 request and select - this is a long UID
    // While the UID is not complete, the 3nd bit (from the right) is set in the SAK.
    for (; sak & 0x04; cascade_level++) {
        // uint8_t sel_all[]    = { PICC_ANTICOLL1, 0x20 };
        uint8_t sel_uid[]    = { PICC_ANTICOLL1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        // SELECT_* (L1: 0x93, L2: 0x95, L3: 0x97)
        sel_uid[0] = /*sel_all[0] = */ 0x93 + cascade_level * 2;

        if (cascade_level < tag->cascade - 1) {
            uid_resp[0] = 0x88;
            memcpy(uid_resp + 1, tag->uid + cascade_level * 3, 3);
        } else {
            memcpy(uid_resp, tag->uid + cascade_level * 3, 4);
        }

        // Construct SELECT UID command
        //sel_uid[1] = 0x70;                                            // transmitting a full UID (1 Byte cmd, 1 Byte NVB, 4 Byte UID, 1 Byte BCC, 2 Bytes CRC)
        memcpy(sel_uid + 2, uid_resp, 4);                               // the UID received during anticollision, or the provided UID
        sel_uid[6] = sel_uid[2] ^ sel_uid[3] ^ sel_uid[4] ^ sel_uid[5]; // calculate and add BCC
        crc_14a_append(sel_uid, 7);                                         // calculate and add CRC
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, sel_uid, sizeof(sel_uid), resp, &len, U8ARR_BIT_LEN(resp));
        // Receive the SAK
        if (status != STATUS_HF_TAG_OK || !len) {
            // printf("SAK Err: %d, %d\r\n", status, recv_len);
            return STATUS_HF_TAG_NO;
        }

        sak = resp[0];

        // Test if more parts of the uid are coming
        if ((sak & 0x04) /* && uid_resp[0] == 0x88 */) {
            // Remove first byte, 0x88 is not an UID byte, it CT, see page 3 of:
            // http://www.nxp.com/documents/application_note/AN10927.pdf
            uid_resp[0] = uid_resp[1];
            uid_resp[1] = uid_resp[2];
            uid_resp[2] = uid_resp[3];
        }
    }
    return STATUS_HF_TAG_OK;
}

/**
* @brief  : ISO14443-A Find a card, only execute once!
* @param  :tag: Buffer that stores card information
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_scan_once(picc_14a_tag_t *tag) {
    // The key parameters of initialization
    if (tag) {
        tag->uid_len = 0;
        memset(tag->uid, 0, 10);
        tag->ats_len = 0;
    } else {
        return STATUS_PAR_ERR;  // Finding cards are not allowed to be transmitted to the label information structure
    }

    // wake
    if (pcd_14a_reader_atqa_request(tag->atqa, NULL, U8ARR_BIT_LEN(tag->atqa)) != STATUS_HF_TAG_OK) {
        // NRF_LOG_INFO("pcd_14a_reader_atqa_request STATUS_HF_TAG_NO\r\n");
        return STATUS_HF_TAG_NO;
    }

    uint8_t resp[DEF_FIFO_LENGTH] = {0}; // theoretically. A usual RATS will be much smaller
    // uint8_t resp_par[MAX_PARITY_SIZE] = {0};

    uint16_t len;
    uint8_t status;
    uint8_t do_cascade = 1;
    uint8_t cascade_level = 0;

    // OK we will select at least at cascade 1, lets see if first byte of UID was 0x88 in
    // which case we need to make a cascade 2 request and select - this is a long UID
    // While the UID is not complete, the 3nd bit (from the right) is set in the SAK.
    for (; do_cascade; cascade_level++) {
        // SELECT_* (L1: 0x93, L2: 0x95, L3: 0x97)
        uint8_t sel_all[]    = { PICC_ANTICOLL1, 0x20 };
        uint8_t sel_uid[]    = { PICC_ANTICOLL1, 0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        uint8_t uid_resp[5] = {0}; // UID + original BCC
        sel_uid[0] = sel_all[0] = PICC_ANTICOLL1 + cascade_level * 2;

        // Send anti -collision instruction
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, sel_all, sizeof(sel_all), resp, &len, U8ARR_BIT_LEN(resp));

        // There is a label collision, we need to solve the collision
        if (status != STATUS_HF_TAG_OK) {
            // The collision still has to be collided. Do n't have this during the decryption process.
            // So do not solve the collision for the time being, but directly inform the user that the user guarantees that there is only one card in the field
            NRF_LOG_INFO("Err at tag collision.\n");
            return status;
        } else {  // no collision, use the response to SELECT_ALL as current uid
            memcpy(uid_resp, resp, 5); // UID + original BCC
        }

        uint8_t uid_resp_len = 4;

        // Always use the final UID paragraph as U32 type UID,
        // No matter how many bytes of UID paragraphs
        // *u32Uid = bytes_to_num(uid_resp, 4);

        // Construct SELECT UID command
        sel_uid[1] = 0x70;  // transmitting a full UID (1 Byte cmd, 1 Byte NVB, 4 Byte UID, 1 Byte BCC, 2 Bytes CRC)

        memcpy(sel_uid + 2, uid_resp, 5);   // the UID received during anticollision with original BCC
        uint8_t bcc = sel_uid[2] ^ sel_uid[3] ^ sel_uid[4] ^ sel_uid[5]; // calculate BCC
        if (sel_uid[6] != bcc) {
            NRF_LOG_INFO("BCC%d incorrect, got 0x%02x, expected 0x%02x\n", cascade_level, sel_uid[6], bcc);
            return STATUS_HF_ERR_BCC;
        }

        crc_14a_append(sel_uid, 7); // calculate and add CRC

        // send 9x 70 Choose a card
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, sel_uid, sizeof(sel_uid), resp, &len, U8ARR_BIT_LEN(resp));
        if (status != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Err at sak receive.\n");
            return STATUS_HF_ERR_STAT;
        }

        // Sak received by buffer
        tag->sak = resp[0];

        // If UID is 0X88 The beginning of the form shows that the UID is not complete
        // In the next cycle, we need to make an increased level, return to the end of the anti -rushing collision, and complete the level
        do_cascade = (((tag->sak & 0x04) /* && uid_resp[0] == 0x88 */) > 0);
        if (do_cascade) {
            // Remove first byte, 0x88 is not an UID byte, it CT, see page 3 of:
            // http://www.nxp.com/documents/application_note/AN10927.pdf
            uid_resp[0] = uid_resp[1];
            uid_resp[1] = uid_resp[2];
            uid_resp[2] = uid_resp[3];
            uid_resp_len = 3;
        }

        // Copy the UID information of the card to the transmitted structure
        memcpy(tag->uid + (cascade_level * 3), uid_resp, uid_resp_len);
        tag->uid_len += uid_resp_len;
        // Only 1 2 3 Three types, corresponding 4 7 10 Byte card number
        // Therefore + 1
        tag->cascade = cascade_level + 1;
    }
    if (tag->sak & 0x20) {
        // Tag supports 14443-4, sending RATS
        uint16_t ats_size;
        status = pcd_14a_reader_ats_request(tag->ats, &ats_size, 0xFF * 8);
        NRF_LOG_INFO("ats status %d, length %d", status, ats_size);
        if (status != STATUS_HF_TAG_OK) {
            NRF_LOG_INFO("Tag SAK claimed to support ATS but tag NAKd RATS");
            tag->ats_len = 0;
            // return STATUS_HF_ERR_ATS;
        } else {
            ats_size -= 2;  // size returned by pcd_14a_reader_ats_request includes CRC
            if (ats_size > 254) {
                NRF_LOG_INFO("Invalid ATS > 254!");
                return STATUS_HF_ERR_ATS;
            }
            tag->ats_len = ats_size;
            // We do not validate ATS here as we want to report ATS as it is without breaking 14a scan
            if (tag->ats[0] != ats_size - 1) {
                NRF_LOG_INFO("Invalid ATS! First byte doesn't match received length");
                // return STATUS_HF_ERR_ATS;
            }
        }
        /*
        * FIXME: If there is an issue here, it will cause the label to lose its selected state.
        *   It is necessary to reselect the card after the issue occurs here.
        */
    }
    return STATUS_HF_TAG_OK;
}

/**
* @brief  : ISO14443-A Find a card
* @param  :tag: Buffer that stores card information
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_scan_auto(picc_14a_tag_t *tag) {
    uint8_t status;

    // The first card search
    status = pcd_14a_reader_scan_once(tag);
    if (status == STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_OK;
    }

    // Second card search
    status = pcd_14a_reader_scan_once(tag);
    if (status == STATUS_HF_TAG_OK) {
        return STATUS_HF_TAG_OK;
    }

    // More than the number of upper limits
    return status;
}

/**
* @brief  : Get selection response
* @param  : PATS: The preservation area of ATS
* @param  : SZATS: The length of the ATS response of the card
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_ats_request(uint8_t *pAts, uint16_t *szAts, uint16_t szAtsBitMax) {
    uint8_t rats[] = { PICC_RATS, 0x80, 0x31, 0x73 }; // FSD=256, FSDI=8, CID=0
    uint8_t status;

    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, rats, sizeof(rats), pAts, szAts, szAtsBitMax);

    if (status != STATUS_HF_TAG_OK) {
        *szAts = 0;
        NRF_LOG_ERROR("ATS rx error: %d", status);
        return status;
    } else if (*szAts == 7 && pAts[0] == 0x4) { // tag replied with NAK
        *szAts = 0;
        return STATUS_HF_ERR_ATS;
    }

    NRF_LOG_INFO("Received ATS length: %d\n", *szAts);

    if (*szAts > 0) { *szAts = *szAts / 8; }
    return STATUS_HF_TAG_OK;
}

/**
* @brief  : Get answering request, type A
* @param  : PSNR: Card serial number, n -bytes
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_atqa_request(uint8_t *resp, uint8_t *resp_par, uint16_t resp_max_bit) {
    uint16_t len = 0;
    uint8_t retry = 0;
    uint8_t status = STATUS_HF_TAG_OK;
    uint8_t wupa[] = { PICC_REQALL };  // 0x26 - REQA  0x52 - WAKE-UP

    // we may need several tries if we did send an unknown command or a wrong authentication before...
    do {
        // Broadcast for a card, WUPA (0x52) will force response from all cards in the field and Receive the ATQA
        status = pcd_14a_reader_bits_transfer(wupa, 7, NULL, resp, resp_par, &len, resp_max_bit);
        // NRF_LOG_INFO("pcd_14a_reader_atqa_request len: %d\n", len);
    } while (len != 16 && (retry++ < 10));

    // normal ATQA It is 2 bytes, that is, 16bit,
    // We need to judge whether the data received is correct
    if (status == STATUS_HF_TAG_OK && len == 16) {
        // You can confirm that at least one 14A card exists in the current field
        return STATUS_HF_TAG_OK;
    }

    // No card
    return STATUS_HF_TAG_NO;
}

/**
* @brief   : Unlock the Gen1a back door card for non -standard M1 operation steps
*               Note that do not have a card after unlocking.
*               Within the data block reading and writing operation range, if the field is turned off or the card is halt or the collision is re -anti -collision,
*               It will lose the back door authority, and you need to call this function to restart.
*
* @retval : Status value Hf_tag_ok, unlocked successfully, other state values indicate unlocking failure
*/
uint8_t pcd_14a_reader_gen1a_unlock(void) {
    // Initialize variables
    uint8_t unlock, status;
    uint16_t rx_length = 0;
    uint8_t recvbuf[1] = { 0x00 };

    // Restart communication (very important)
    pcd_14a_reader_halt_tag();

    // Unlock the first step, send 7bit 0x40
    unlock = PICC_MAGICWUPC1;
    status = pcd_14a_reader_bits_transfer(&unlock, 7, NULL, recvbuf, NULL, &rx_length, U8ARR_BIT_LEN(recvbuf));
    if (!(status == STATUS_HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
        NRF_LOG_INFO("UNLOCK(MAGICWUPC1) FAILED! Length: %d, Status: %02x\n", rx_length, status);
        return STATUS_HF_ERR_STAT;
    }

    // Step in the second step, send a complete byte 0x43
    unlock = PICC_MAGICWUPC2;
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, &unlock, 1, recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
    if (!(status == STATUS_HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
        NRF_LOG_INFO("UNLOCK(MAGICWUPC2) FAILED! Length: %d, Status: %02x\n", rx_length, status);
        return STATUS_HF_ERR_STAT;
    }

    // There is no problem with unlocking twice. We default this unlock operation successfully!
    return STATUS_HF_TAG_OK;
}

/**
* @brief  : Perform the UFUID card and block the back door instructions to make it a normal card without responding to the back door instruction
*               The premise of successful this operation is:
*               1. Has been called pcd_14a_reader_gen1a_unlock() Function unlock card successfully
*               2. The card has UFUID Card sealing door instruction function
*
* @retval : Status value Hf_tag_ok, closure card or the back door of the card seal,
            Other status values indicate the failure of the card or the back door without a card
*/
uint8_t pcd_14a_reader_gen1a_uplock(void) {
    uint8_t status;
    uint16_t rx_length = 0;

    // Our known dual -layer card sealing instructions
    uint8_t uplock_1[] = { 0xE1,  0x00,  0xE1,  0xEE };
    uint8_t uplock_2[] = { 0x85,  0x00,  0x00,  0x00,
                           0x00,  0x00,  0x00,  0x00,
                           0x00,  0x00,  0x00,  0x00,
                           0x00,  0x00,  0x00,  0x08,
                           0x18,  0x47
                         };

    uint8_t recvbuf[1] = { 0x00 };

    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, uplock_1, sizeof(uplock_1), recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
    if (!(status == STATUS_HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
        NRF_LOG_INFO("UPLOCK1(UFUID) FAILED!\n");
        return STATUS_HF_ERR_STAT;
    }

    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, uplock_2, sizeof(uplock_2), recvbuf, &rx_length, U8ARR_BIT_LEN(recvbuf));
    if (!(status == STATUS_HF_TAG_OK && rx_length == 4 && recvbuf[0] == 0x0A)) {
        NRF_LOG_INFO("UPLOCK2(UFUID) FAILED!\n");
        return STATUS_HF_ERR_STAT;
    }

    // Successful card sealing
    return STATUS_HF_TAG_OK;
}

/**
* @brief   : Check M1 card password
* @param  : Type: Password verification mode
*                     = 0x60, verify the A key
*                     = 0x61, verify the B key
*           ucaddr: block address
*           pKEY: password
*           PSNR: Card serial number, 4 bytes
* @retval : The status value STATUS_HF_TAG_OK is successful, tag_errauth fails, and other returns indicate some abnormalities related to communication errors!
*/
uint8_t pcd_14a_reader_mf1_auth(picc_14a_tag_t *tag, uint8_t type, uint8_t addr, uint8_t *pKey) {
    uint8_t dat_buff[12] = { type, addr };
    uint16_t data_len = 0;

    memcpy(&dat_buff[2], pKey, 6);
    get_4byte_tag_uid(tag, &dat_buff[8]);

    pcd_14a_reader_bytes_transfer(PCD_AUTHENT, dat_buff, 12, dat_buff, &data_len, U8ARR_BIT_LEN(dat_buff));

    // In order to improve compatibility, we directly judge the implementation of the execution PCD_AUTHENT
    // After the instruction, whether the communication plus position in Status2reg is placed.
    if (read_register_single(Status2Reg) & 0x08) {
        return STATUS_HF_TAG_OK;
    }

    // Other situations are considered failure!
    return STATUS_MF_ERR_AUTH;
}

/**
* @brief   : Cancel the state of the checked key
*/
void pcd_14a_reader_mf1_unauth(void) {
    clear_register_mask(Status2Reg, 0x08);
}

/**
* @brief   : Read the data of the specified block address of the m1 card
* @param  :cmd : Read instruction
*           addr: block address
*           p   : Read data, 16 bytes
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_mf1_read_by_cmd(uint8_t cmd, uint8_t addr, uint8_t *p) {
    uint8_t status;
    uint16_t len;
    uint8_t dat_buff[MAX_MIFARE_FRAME_SIZE] = { cmd, addr };
    uint8_t crc_buff[DEF_CRC_LENGTH]        = { 0x00 };

    // Short data directly MCU calculate
    crc_14a_append(dat_buff, 2);
    // Then initiate communication
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 4, dat_buff, &len, U8ARR_BIT_LEN(dat_buff));
    if (status == STATUS_HF_TAG_OK) {
        if (len == 0x90 /* 0x90 = 144bits */) {
            // 16 -byte length CRC data, in order not to waste the CPU performance,
            // We can let 522 Calculate
            crc_14a_calculate(dat_buff, 16, crc_buff);
            // Check the CRC to avoid data errors
            if ((crc_buff[0] != dat_buff[16]) || (crc_buff[1] != dat_buff[17])) {
                status = STATUS_HF_ERR_CRC;
            }
            // Although CRC After checking the problem, but we can still pass back
            // Read the card data, because developers may have special usage
            memcpy(p, dat_buff, 16);
        } else {
            // The data passed back is wrong, which may be an environmental factors or cards that do not comply with specifications!
            // Or the control bit affects reading!
            status = STATUS_HF_ERR_STAT;
        }
    }
    return status;
}

/**
* @brief   : Read the data of the specified block address of the m1 card
* @param  : Addr: block address
*           p   : Read data, 16 bytes
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_mf1_read(uint8_t addr, uint8_t *p) {
    // Standard M1 Card Reading Card Reading
    return pcd_14a_reader_mf1_read_by_cmd(PICC_READ, addr, p);
}

/**
* @brief   : Write the specified data at the designated block address of the M1 card
* @param  :cmd : Writing instruction
*           addr: block address
*           p   : Written data, 16 bytes
*
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_mf1_write_by_cmd(uint8_t cmd, uint8_t addr, uint8_t *p) {
    uint8_t status;
    uint16_t dat_len;

    // Prepare to write card data to initiate a card writing card
    uint8_t dat_buff[18] = { cmd, addr };
    crc_14a_append(dat_buff, 2);

    // NRF_LOG_INFO("0 pcd_14a_reader_mf1_write addr = %d\r\n", addr);

    // Request to write a card, at this time, the card should reply to ACK
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 4, dat_buff, &dat_len, U8ARR_BIT_LEN(dat_buff));
    // The communication fails, the reason is returned directly
    if (status != STATUS_HF_TAG_OK) {
        return status;
    }
    // The communication was successful, but the operation was rejected by the card!
    if ((dat_len != 4) || ((dat_buff[0] & 0x0F) != 0x0A)) {
        // NRF_LOG_INFO("1 status = %d, datalen = %d, data = %02x\n", status, dat_len, dat_buff[0]);
        status = STATUS_HF_ERR_STAT;
    }
    // The communication was successful, the card accepted the card writing operation
    if (status == STATUS_HF_TAG_OK) {
        // 1. Copy data and calculate CRC
        memcpy(dat_buff, p, 16);
        crc_14a_calculate(dat_buff, 16, &dat_buff[16]);

        // NRF_LOG_INFO_hex("Will send: ", (uint8_t *)p, 16);
        // NRF_LOG_INFO("\n");

        // 2. Transfer the final card writing data to complete the writing card
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, dat_buff, 18, dat_buff, &dat_len, U8ARR_BIT_LEN(dat_buff));
        // The communication fails, the reason is returned directly
        if (status != STATUS_HF_TAG_OK) {
            return status;
        }
        // The communication is successful, we need to determine whether the card is successfully processed after receiving the data
        // And reply ACK
        if ((dat_len != 4) || ((dat_buff[0] & 0x0F) != 0x0A)) {
            // NRF_LOG_INFO("2 status = %d, datalen = %d, data = %02x\n", status, dat_len, dat_buff[0]);
            status = STATUS_HF_ERR_STAT;
        }
    }
    return status;
}

/**
* @brief   : Write the specified data at the designated block address of the M1 card
* @param  : Addr: block address
*           P: The written data, 16 bytes
* @retval : Status value hf_tag_ok, success
*/
uint8_t pcd_14a_reader_mf1_write(uint8_t addr, uint8_t *p) {
    // Standard M1 writing card writing card
    return pcd_14a_reader_mf1_write_by_cmd(PICC_WRITE, addr, p);
}

/**
* @brief   : Let the card enter the dormant mode
* @param  :none
* @retval : Status value Tag_notag, success
*/
uint8_t pcd_14a_reader_halt_tag(void) {
    uint8_t status;
    uint16_t unLen;
    // Prepare the molding data directly, and calculate a ghost CRC
    uint8_t data[] = { PICC_HALT, 0x00, 0x57, 0xCD };
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, data, 4, data, &unLen, U8ARR_BIT_LEN(data));
    return status == STATUS_HF_TAG_NO && unLen == 0;
}

/**
* @brief   : Quickly let the card enter the dormant mode
* @param  :none
* @retval :none
*/
void pcd_14a_reader_fast_halt_tag(void) {
    uint8_t data[] = { PICC_HALT, 0x00, 0x57, 0xCD };
    pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE, data, 4, NULL, NULL, U8ARR_BIT_LEN(data));
}

/**
* @brief  : Calculate CRC16 with RC522 (cyclic redundant verification)
* @param  : PIN: Calculate the array of CRC16
*           Len: Calculate the number of bytes of the CRC16
*           POUT: Storage of the first address of the calculation results
* @retval : Status value hf_tag_ok, success
*/
void pcd_14a_reader_calc_crc(uint8_t *pbtData, size_t szLen, uint8_t *pbtCrc) {
    uint8_t i, n;

    // Reset state machine
    clear_register_mask(Status1Reg, 0x20);
    write_register_single(CommandReg, PCD_IDLE);
    set_register_mask(FIFOLevelReg, 0x80);

    // Calculate the data of CRC to write to FIFO
    write_register_buffer(FIFODataReg, pbtData, szLen);
    write_register_single(CommandReg, PCD_CALCCRC);

    // Waiting for calculation to complete
    i = szLen * 2;
    do {
        n = read_register_single(Status1Reg);
        i--;
    } while ((i != 0) && !(n & 0x20));

    // Get the final calculated CRC data
    pbtCrc[0] = read_register_single(CRCResultRegL);
    pbtCrc[1] = read_register_single(CRCResultRegM);
}

/**
* @brief  : Open the antenna
*/
inline void pcd_14a_reader_antenna_on(void) {
    set_register_mask(TxControlReg, 0x03);
    g_is_reader_antenna_on = true;
    TAG_FIELD_LED_ON();
}

/**
* @brief  : Close the antenna
*/
inline void pcd_14a_reader_antenna_off(void) {
    clear_register_mask(TxControlReg, 0x03);
    g_is_reader_antenna_on = false;
    TAG_FIELD_LED_OFF();
}

/**
* @brief  : Qi Dian school inspection enabled
*/
inline void pcd_14a_reader_parity_on(void) {
    clear_register_mask(MfRxReg, 0x10);
}

/**
* @brief  : Qi Tong school inspection position closed
*/
inline void pcd_14a_reader_parity_off(void) {
    set_register_mask(MfRxReg, 0x10);
}

/**
* @brief    : Obtaining a class joint command, input only allows only three cases to exist
*               1 Express the first level, the command is PICC_ANTICOLL1
*               2 Express the second level, the command is PICC_ANTICOLL2
*               3 Indicates the third level, the command is PICC_ANTICOLL3
* @param    :len  : The byte length of the buffer of the value of the value
* @param    :src  : Byte buffer stored in the numerical
* @retval   : Converting result
*
*/
uint8_t cascade_to_cmd(uint8_t cascade) {
    uint8_t ret = PICC_ANTICOLL1;
    switch (cascade) {
        case 1:
            ret = PICC_ANTICOLL1;
            break;

        case 2:
            ret = PICC_ANTICOLL2;
            break;

        case 3:
            ret = PICC_ANTICOLL3;
            break;
    }
    return ret;
}

/**
* @brief    : The 4 -byte type UID that obtained the label, determined based on the inlet of the basic card information
*               1 Indicates a one -level joint, uID has a UID  4 Byte, use all of them directly
*               2 Indicates a secondary couple, UID has a  7 Byte, post -four digits
*               3 Express three -time couplet, uid is available 10 Byte, post -four digits
* @param    :tag    : Structure of storing card information
* @param    :pUid       : Byte buffer of the result of storage results
* @retval   : Return to the starting address of the valid UID bytes we searched,
*             Note: This address may refer to the stack, and it is not guaranteed that it must be a global and effective address.
*                   This address is based on tag The location of the memory determines the life cycle.
*
*/
uint8_t *get_4byte_tag_uid(picc_14a_tag_t *tag, uint8_t *pUid) {
    uint8_t *p_TmpUid = NULL;
    switch (tag->cascade) {
        case 1:
            p_TmpUid = tag->uid;
            break;

        case 2:
            p_TmpUid = tag->uid + 3;
            break;

        case 3:
            p_TmpUid = tag->uid + 6;
            break;
    }
    if (pUid != NULL) {
        memcpy(pUid, p_TmpUid, 4);
    }
    return p_TmpUid;
}

/**
* @brief    : UID U32 -type U32 type, determined based on the input basic card information
*               1 Indicates a one -level joint, uID has a UID  4 byte
*               2 Indicates a secondary couple, UID has a  7 byte
*               3 Express three -time couplet, uid is available 10 byte
* @param    :tag  : Structure of storing card information
* @retval   : Converting result
*
*/
uint32_t get_u32_tag_uid(picc_14a_tag_t *tag) {
    uint8_t uid_buf[4] = { 0x00 };
    // Directly call the encapsulated function copy the target value
    get_4byte_tag_uid(tag, uid_buf);
    return bytes_to_num(uid_buf, 4);
}

/**
* @brief Calculate CRC on the selected platform
 *
 */
inline void crc_14a_calculate(uint8_t *pbtData, size_t szLen, uint8_t *pbtCrc) {
    switch (m_crc_computer) {
        case 0: {
            calc_14a_crc_lut(pbtData, szLen, pbtCrc);
        }
        break;
        case 1: {
            pcd_14a_reader_calc_crc(pbtData, szLen, pbtCrc);
        }
        break;
        default: {
            //
        } break;
    }
}


/**
 * @brief CRC after adding calculation to the end of the data
 *
 */
inline void crc_14a_append(uint8_t *pbtData, size_t szLen) {
    switch (m_crc_computer) {
        case 0: {
            calc_14a_crc_lut(pbtData, szLen, pbtData + szLen);
        }
        break;
        case 1: {
            pcd_14a_reader_calc_crc(pbtData, szLen, pbtData + szLen);
        }
        break;
        default: {

        } break;
    }
}

/**
* @brief Switch the calculation source of the CRC, the default is in MCU Calculated above,
*           Can be switched to RC522 Calculation, if the performance of the MCU is not enough
*           if MCU The performance is sufficient, it is recommended to put it MCU Calculated above to make the calculation process smoother
*           if MCU Insufficient performance, it is recommended to put it in 522 Calculated above, alleviate the calculation pressure of MCU
*
 */
inline void pcd_14a_reader_crc_computer(uint8_t use522CalcCRC) {
    m_crc_computer = use522CalcCRC;
}

/**
* @brief    : The hf 14a raw command implementation function can be used to send the 14A command with the specified configuration parameters.
* @param    :waitResp           : Wait for tag response
* @param    :appendCrc          : Do you want to add CRC before sending
* @param    :autoSelect         : Automatically select card before sending data
* @param    :keepField          : Do you want to keep the RF field on after sending
* @param    :checkCrc           : Is CRC verified after receiving data? If CRC verification is enabled, CRC bytes will be automatically removed after verification is completed.
* @param    :waitRespTimeout    : If waitResp is enabled, this parameter will be the timeout value to wait for the tag to respond
* @param    :szDataSend         : The number of bytes or bits of data to be sent
* @param    :pDataSend          : Pointer to the buffer of the data to be sent
*
* @retval   : Execution Status
*
*/
uint8_t pcd_14a_reader_raw_cmd(bool openRFField,  bool waitResp, bool appendCrc, bool autoSelect, bool keepField, bool checkCrc, uint16_t waitRespTimeout,
                               uint16_t szDataSendBits, uint8_t *pDataSend, uint8_t *pDataRecv, uint16_t *pszDataRecv, uint16_t szDataRecvBitMax) {
    // Status code, default is OK.
    uint8_t status = STATUS_HF_TAG_OK;
    // Reset recv length.
    *pszDataRecv = 0;

    // If additional CRC is required, first add the CRC to the tail.
    if (appendCrc) {
        if (szDataSendBits == 0) {
            NRF_LOG_INFO("Adding CRC but missing data");
            return STATUS_PAR_ERR;
        }
        if (szDataSendBits % 8) {
            NRF_LOG_INFO("Adding CRC incompatible with partial bytes");
            return STATUS_PAR_ERR;
        }
        if (szDataSendBits > ((DEF_FIFO_LENGTH - DEF_CRC_LENGTH) * 8)) {
            // Note: Adding CRC requires at least two bytes of free space. If the transmitted data is already greater than or equal to 64, an error needs to be returned
            NRF_LOG_INFO("Adding CRC requires data length less than or equal to 62.");
            return STATUS_PAR_ERR;
        }
        // Calculate and append CRC byte data to the buffer
        crc_14a_append(pDataSend, szDataSendBits / 8);
        // CRC is also sent as part of the data, so the total length needs to be added to the CRC length here
        szDataSendBits += DEF_CRC_LENGTH * 8;
    }

    if (autoSelect || szDataSendBits) {
        // override openRFField if we need to select or to send data
        openRFField = true;
    }
    if (openRFField && ! g_is_reader_antenna_on) { // Open rf field?
        pcd_14a_reader_reset();
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
    }

    if (autoSelect) {
        picc_14a_tag_t ti;
        status = pcd_14a_reader_scan_once(&ti);
        // Determine whether the card search was successful
        if (status != STATUS_HF_TAG_OK) {
            pcd_14a_reader_antenna_off();
            return status;
        }
    }

    // Is there any data that needs to be sent
    if (szDataSendBits) {
        // If there is no need to receive data, the data receiving cache needs to be empty, otherwise a specified timeout value needs to be set
        // Caching old timeout values
        uint16_t oldWaitRespTimeout = g_com_timeout_ms;
        if (waitResp) {
            // Then set the new values in
            g_com_timeout_ms = waitRespTimeout;
        } else {
            pDataRecv = NULL;
        }
        if (szDataSendBits % 8) {
            status = pcd_14a_reader_bits_transfer(
                         pDataSend,
                         szDataSendBits,
                         NULL,
                         pDataRecv,
                         NULL,
                         pszDataRecv,
                         szDataRecvBitMax
                     );
        } else {
            status = pcd_14a_reader_bytes_transfer(
                         PCD_TRANSCEIVE,
                         pDataSend,
                         szDataSendBits / 8,
                         pDataRecv,
                         pszDataRecv,
                         szDataRecvBitMax
                     );
        }

        // If we need to receive data, we need to perform further operations on the data based on the remaining configuration after receiving it
        if (waitResp) {
            // Number of bits to bytes
            uint8_t finalRecvBytes = (*pszDataRecv / 8) + (*pszDataRecv % 8 > 0 ? 1 : 0);
            // If CRC verification is required, we need to perform CRC calculation
            if (checkCrc) {
                if (finalRecvBytes >= 3) {  // Ensure at least three bytes (one byte of data+two bytes of CRC)
                    // Calculate and store CRC
                    uint8_t crc_buff[DEF_CRC_LENGTH] = { 0x00 };
                    crc_14a_calculate(pDataRecv, finalRecvBytes - DEF_CRC_LENGTH, crc_buff);
                    // Verify CRC
                    if (pDataRecv[finalRecvBytes - 2] != crc_buff[0] ||  pDataRecv[finalRecvBytes - 1] != crc_buff[1]) {
                        // We have found an error in CRC verification and need to inform the upper computer!
                        *pszDataRecv = 0;
                        status = STATUS_HF_ERR_CRC;
                    } else {
                        // If the CRC needs to be verified by the device and the device determines that the CRC is normal,
                        // we will return the data without CRC
                        *pszDataRecv = finalRecvBytes - DEF_CRC_LENGTH;
                    }
                } else {
                    // The data is insufficient to support the length of the CRC, so it is returned as is
                    *pszDataRecv = 0;
                }
            } else {
                // Do not verify CRC, all data is returned as is
                *pszDataRecv = finalRecvBytes;
            }
            // We need to recover the timeout value
            g_com_timeout_ms = oldWaitRespTimeout;
        } else {
            *pszDataRecv = 0;
        }
    }

    // Finally, keep the field open as needed
    if (!keepField) {
        pcd_14a_reader_antenna_off();
    }

    return status;
}
