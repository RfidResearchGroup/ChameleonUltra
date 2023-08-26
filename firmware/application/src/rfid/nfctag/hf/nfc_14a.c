#include <hal/nrf_nfct.h>
#include <nrfx_nfct.h>
#include <nrf_gpio.h>

#define NRF_LOG_MODULE_NAME nfc
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

#include "hex_utils.h"
#include "crc_utils.h"
#include "nfc_mf1.h"

#include "rfid_main.h"
#include "syssleep.h"
#include "tag_emulation.h"


#if NFC_TAG_14A_RX_PARITY_AUTO_DEL_ENABLE
#define NRF_NFCT_PARITY_FRAMECONFIG 0x05;
#else
#define NRF_NFCT_PARITY_FRAMECONFIG 0x04;
#endif

//Use macro definition data to receive data enable to achieve
#define NRFX_NFCT_RX_BYTES                                                                                          \
    do {                                                                                                            \
        NRF_NFCT->RXD.FRAMECONFIG = NRF_NFCT_PARITY_FRAMECONFIG;                                                    \
        NRF_NFCT->PACKETPTR = (uint32_t)m_nfc_rx_buffer;                                                            \
        NRF_NFCT->MAXLEN = ((uint32_t)MAX_NFC_RX_BUFFER_SIZE << NFCT_MAXLEN_MAXLEN_Pos) & NFCT_MAXLEN_MAXLEN_Msk;   \
        NRF_NFCT->INTENSET = (NRF_NFCT_INT_RXFRAMESTART_MASK |                                                      \
                              NRF_NFCT_INT_RXFRAMEEND_MASK |                                                        \
                              NRF_NFCT_INT_RXERROR_MASK);                                                           \
        NRF_NFCT->TASKS_ENABLERXDATA = 1;                                                                           \
    } while(0);


//14443A protocol status machine
nfc_tag_14a_state_t m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;

// 14443A protocol processor
nfc_tag_14a_handler_t m_tag_handler = {
    .cb_reset = NULL,       // Tag Reset callback
    .cb_state = NULL,       // Label status machine callback
    .get_coll_res = NULL,   // Obtain packaging of anti -conflict resources of labels
};

// Byte mirror
const uint8_t ByteMirror[256] = {
    0x00, 0x80, 0x40, 0xc0, 0x20, 0xa0, 0x60, 0xe0,
    0x10, 0x90, 0x50, 0xd0, 0x30, 0xb0, 0x70, 0xf0,
    0x08, 0x88, 0x48, 0xc8, 0x28, 0xa8, 0x68, 0xe8,
    0x18, 0x98, 0x58, 0xd8, 0x38, 0xb8, 0x78, 0xf8,
    0x04, 0x84, 0x44, 0xc4, 0x24, 0xa4, 0x64, 0xe4,
    0x14, 0x94, 0x54, 0xd4, 0x34, 0xb4, 0x74, 0xf4,
    0x0c, 0x8c, 0x4c, 0xcc, 0x2c, 0xac, 0x6c, 0xec,
    0x1c, 0x9c, 0x5c, 0xdc, 0x3c, 0xbc, 0x7c, 0xfc,
    0x02, 0x82, 0x42, 0xc2, 0x22, 0xa2, 0x62, 0xe2,
    0x12, 0x92, 0x52, 0xd2, 0x32, 0xb2, 0x72, 0xf2,
    0x0a, 0x8a, 0x4a, 0xca, 0x2a, 0xaa, 0x6a, 0xea,
    0x1a, 0x9a, 0x5a, 0xda, 0x3a, 0xba, 0x7a, 0xfa,
    0x06, 0x86, 0x46, 0xc6, 0x26, 0xa6, 0x66, 0xe6,
    0x16, 0x96, 0x56, 0xd6, 0x36, 0xb6, 0x76, 0xf6,
    0x0e, 0x8e, 0x4e, 0xce, 0x2e, 0xae, 0x6e, 0xee,
    0x1e, 0x9e, 0x5e, 0xde, 0x3e, 0xbe, 0x7e, 0xfe,
    0x01, 0x81, 0x41, 0xc1, 0x21, 0xa1, 0x61, 0xe1,
    0x11, 0x91, 0x51, 0xd1, 0x31, 0xb1, 0x71, 0xf1,
    0x09, 0x89, 0x49, 0xc9, 0x29, 0xa9, 0x69, 0xe9,
    0x19, 0x99, 0x59, 0xd9, 0x39, 0xb9, 0x79, 0xf9,
    0x05, 0x85, 0x45, 0xc5, 0x25, 0xa5, 0x65, 0xe5,
    0x15, 0x95, 0x55, 0xd5, 0x35, 0xb5, 0x75, 0xf5,
    0x0d, 0x8d, 0x4d, 0xcd, 0x2d, 0xad, 0x6d, 0xed,
    0x1d, 0x9d, 0x5d, 0xdd, 0x3d, 0xbd, 0x7d, 0xfd,
    0x03, 0x83, 0x43, 0xc3, 0x23, 0xa3, 0x63, 0xe3,
    0x13, 0x93, 0x53, 0xd3, 0x33, 0xb3, 0x73, 0xf3,
    0x0b, 0x8b, 0x4b, 0xcb, 0x2b, 0xab, 0x6b, 0xeb,
    0x1b, 0x9b, 0x5b, 0xdb, 0x3b, 0xbb, 0x7b, 0xfb,
    0x07, 0x87, 0x47, 0xc7, 0x27, 0xa7, 0x67, 0xe7,
    0x17, 0x97, 0x57, 0xd7, 0x37, 0xb7, 0x77, 0xf7,
    0x0f, 0x8f, 0x4f, 0xcf, 0x2f, 0xaf, 0x6f, 0xef,
    0x1f, 0x9f, 0x5f, 0xdf, 0x3f, 0xbf, 0x7f, 0xff,
};

// RATS FSDI length check table
const uint16_t ats_fsdi_table[] = {
    // 0 - 8
    16, 24, 32, 40, 48, 64, 96, 128, 256,
    // 9 - F
    256, 256, 256, 256, 256, 256, 256,
};


// Whether it is responding to
static volatile  bool m_is_responded = false;
// Receiving buffer
static uint8_t m_nfc_rx_buffer[MAX_NFC_RX_BUFFER_SIZE] = { 0x00 };
static uint8_t m_nfc_tx_buffer[MAX_NFC_TX_BUFFER_SIZE] = { 0x00 };
// The N -secondary connection needs to use SAK, when the "third 'bit' in SAK is 1 is 1, the logo UID is incomplete
static uint8_t m_uid_incomplete_sak[]   = { 0x04, 0xda, 0x17 };

/**
 * @brief Calculate BCC
 *
 */
void nfc_tag_14a_create_bcc(uint8_t *pbtData, size_t szLen, uint8_t *pbtBcc) {
    // It is best to reset it when using the output buffer
    *pbtBcc = 0x00;
    do {
        *pbtBcc ^= *pbtData++;
    } while (--szLen);
}

/**
 * @brief Add BCC to the end of the data streaming
 *
 */
inline void nfc_tag_14a_append_bcc(uint8_t *pbtData, size_t szLen) {
    nfc_tag_14a_create_bcc(pbtData, szLen, pbtData + szLen);
}

/**
 * @brief Add CRC at the end of the data flow, remember:
 *  PBTData must have enough length to accommodate the CRC calculation results (two bytes)
 *
 */
inline void nfc_tag_14a_append_crc(uint8_t *pbtData, size_t szLen) {
    calc_14a_crc_lut(pbtData, szLen, pbtData + szLen);
}

/**
 * @brief Check whether the CRC is correct
 *
 */
bool nfc_tag_14a_checks_crc(uint8_t *pbtData, size_t szLen) {
    //if (szLen < 3) return false;
    uint8_t c1 = pbtData[szLen - 2];
    uint8_t c2 = pbtData[szLen - 1];
    nfc_tag_14a_append_crc(pbtData, szLen - 2);
    return pbtData[szLen - 2] == c1 && pbtData[szLen - 1] == c2;
}

/**
* @brief  : Bit frames for packaging ISO14443A
* Automatically conduct the merger of the parity of the coupling school and the data of the data
* @param   pbtTx: bitstream to be transmitted
*          szTxBits: The length of the buffer
*          pbtTxPar: bitstream of the puppet school inspection, the length of this data must be szTxBits / 8, that is,
* In fact, the composition of the bitstream after the merger is:
*                    data(1byte) - par(1bit) - data(1byte) - par(1bit) ...
*                      00001000  -   0       - 10101110    - 1
*                    This similar data structure
*          pbtFrame: The final assembled data buffer
* @retval :The length of the bitstream assembly results buffer. Note that it is the length of the bit.
*/
uint8_t nfc_tag_14a_wrap_frame(const uint8_t *pbtTx, const size_t szTxBits, const uint8_t *pbtTxPar, uint8_t *pbtFrame) {
    uint8_t btData;
    uint32_t uiBitPos;
    uint32_t uiDataPos = 0;
    size_t szBitsLeft = szTxBits;
    size_t szFrameBits = 0;

    // Make sure we should frame at least something
    if (szBitsLeft == 0)
        return 0;

    // Handle a short response (1byte) as a special case
    if (szBitsLeft < 9) {
        *pbtFrame = *pbtTx;
        szFrameBits = szTxBits;
        return szFrameBits;
    }
    // We start by calculating the frame length in bits
    szFrameBits = szTxBits + (szTxBits / 8);

    // Parse the data bytes and add the parity bits
    // This is really a sensitive process, mirror the frame bytes and append parity bits
    // buffer = mirror(frame-byte) + parity + mirror(frame-byte) + parity + ...
    // split "buffer" up in segments of 8 bits again and mirror them
    // air-bytes = mirror(buffer-byte) + mirror(buffer-byte) + mirror(buffer-byte) + ..
    while (1) {
        // Reset the temporary frame byte;
        uint8_t btFrame = 0;

        for (uiBitPos = 0; uiBitPos < 8; uiBitPos++) {
            // Copy as much data that fits in the frame byte
            btData = ByteMirror[pbtTx[uiDataPos]];
            btFrame |= (btData >> uiBitPos);
            // Save this frame byte
            *pbtFrame = ByteMirror[btFrame];
            // Set the remaining bits of the date in the new frame byte and append the parity bit
            btFrame = (btData << (8 - uiBitPos));
            btFrame |= ((pbtTxPar[uiDataPos] & 0x01) << (7 - uiBitPos));
            // Backup the frame bits we have so far
            pbtFrame++;
            *pbtFrame = ByteMirror[btFrame];
            // Increase the data (without parity bit) position
            uiDataPos++;
            // Test if we are done
            if (szBitsLeft < 9)
                return szFrameBits;
            szBitsLeft -= 8;
        }
        // Every 8 data bytes we lose one frame byte to the parities
        pbtFrame++;
    }
}

/**
* @brief  :Bit frame of ISO14443A
*           Automatically perform the unpacking of the puppet school inspection and the data
* @param  :pbtFrame: bitstream that will be dismissed
*          szFrameBits:The length of the buffer
*          pbtRx:Caps, data areas, data areas, data areas, data areas, data areas.
*          pbtRxPar: The buffer of the bitstream Store after the packaging, the coupling school inspection area
* @retval :The data length of the bitstream packaging, note that the length of the data area is the length of the data area.retval / 8
*/
uint8_t nfc_tag_14a_unwrap_frame(const uint8_t *pbtFrame, const size_t szFrameBits, uint8_t *pbtRx, uint8_t *pbtRxPar) {
    uint8_t btFrame;
    uint8_t btData;
    uint8_t uiBitPos;
    uint32_t uiDataPos = 0;
    uint8_t *pbtFramePos = (uint8_t *)pbtFrame;
    size_t szBitsLeft = szFrameBits;
    size_t szRxBits = 0;

    // Make sure we should frame at least something
    if (szBitsLeft == 0)
        return 0;

    // Handle a short response (1byte) as a special case
    if (szBitsLeft < 9) {
        *pbtRx = *pbtFrame;
        szRxBits = szFrameBits;
        return szRxBits;
    }

    // Calculate the data length in bits
    szRxBits = szFrameBits - (szFrameBits / 9);

    // Parse the frame bytes, remove the parity bits and store them in the parity array
    // This process is the reverse of WrapFrame(), look there for more info
    while (1) {
        for (uiBitPos = 0; uiBitPos < 8; uiBitPos++) {
            btFrame = ByteMirror[pbtFramePos[uiDataPos]];
            btData = (btFrame << uiBitPos);
            btFrame = ByteMirror[pbtFramePos[uiDataPos + 1]];
            btData |= (btFrame >> (8 - uiBitPos));
            pbtRx[uiDataPos] = ByteMirror[btData];
            if (pbtRxPar != NULL)
                pbtRxPar[uiDataPos] = ((btFrame >> (7 - uiBitPos)) & 0x01);
            // Increase the data (without parity bit) position
            uiDataPos++;
            // Test if we are done
            if (szBitsLeft < 9)
                return szRxBits;
            szBitsLeft -= 9;
        }
        // Every 8 data bytes we lose one frame byte to the parities
        pbtFramePos++;
    }
}

/**
 * @brief: Function for response reader core implemented
 * @param[in]   data       Send data buffer
 * @param[in]   bytes      Send data length
 * @param[in]   appendCrc  Auto append crc
 * @param[in]   delayMode  Timeslot, delay mode.
 */
#define NFC_14A_TX_BYTE_CORE(data, bytes, appendCrc, delayMode)                                                  \
    do {                                                                                                         \
        m_is_responded = true;                                                                                   \
        memcpy(m_nfc_tx_buffer, data, bytes);                                                                    \
        NRF_NFCT->PACKETPTR = (uint32_t)(m_nfc_tx_buffer);                                                       \
        NRF_NFCT->TXD.AMOUNT = (bytes << NFCT_TXD_AMOUNT_TXDATABYTES_Pos) & NFCT_TXD_AMOUNT_TXDATABYTES_Msk;     \
        NRF_NFCT->FRAMEDELAYMODE = delayMode;                                                                    \
        uint32_t reg = 0;                                                                                        \
        reg |= NFCT_TXD_FRAMECONFIG_PARITY_Msk;                                                                  \
        reg |= NFCT_TXD_FRAMECONFIG_DISCARDMODE_Msk;                                                             \
        reg |= NFCT_TXD_FRAMECONFIG_SOF_Msk;                                                                     \
        if (appendCrc) {                                                                                         \
            reg |= NFCT_TXD_FRAMECONFIG_CRCMODETX_Msk;                                                           \
        }                                                                                                        \
        NRF_NFCT->TXD.FRAMECONFIG = reg;                                                                         \
        NRF_NFCT->INTENSET = (NRF_NFCT_INT_TXFRAMESTART_MASK | NRF_NFCT_INT_TXFRAMEEND_MASK);                    \
        NRF_NFCT->TASKS_STARTTX = 1;                                                                             \
    } while(0);                                                                                                  \


/**@brief The function of sending the byte flow, this implementation automatically sends SOF
 *
 * @param[in]   data       The byte flow data to be sent
 * @param[in]   bytes       The length of the byte flow to be sent
 * @param[in]   appendCrc  Whether to send the byte flow, automatically send the CRC16 verification automatically
 */
void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool appendCrc) {
    NFC_14A_TX_BYTE_CORE(data, bytes, appendCrc, NRF_NFCT_FRAME_DELAY_MODE_WINDOW);
}

/**@brief The function of sending the byte flow, this implementation automatically sends SOF
 *
 * @param[in]   data        The byte flow data to be sent
 * @param[in]   bytes       The length of the byte flow to be sent
 * @param[in]   appendCrc   Whether to send the byte flow, automatically send the CRC16 verification automatically
 */
void nfc_tag_14a_tx_bytes_delay_freerun(uint8_t *data, uint32_t bytes, bool appendCrc) {
    NFC_14A_TX_BYTE_CORE(data, bytes, appendCrc, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**
 * @brief: Function for response reader core implemented
 * @param[in]   bits   Send bits length
 * @param[in]   mode   communication mode
 *
 */
#define NFC_14A_TX_BITS_CORE(bits, mode)                                                        \
    do {                                                                                        \
        NRF_NFCT->PACKETPTR = (uint32_t)(m_nfc_tx_buffer);                                      \
        NRF_NFCT->TXD.AMOUNT = bits;                                                            \
        NRF_NFCT->INTENSET = (NRF_NFCT_INT_TXFRAMESTART_MASK | NRF_NFCT_INT_TXFRAMEEND_MASK);   \
        NRF_NFCT->FRAMEDELAYMODE = mode;                                                        \
        NRF_NFCT->TXD.FRAMECONFIG = NFCT_TXD_FRAMECONFIG_SOF_Msk;                               \
        NRF_NFCT->TASKS_STARTTX = 1;                                                            \
    } while(0);                                                                                 \

/**@brief The function of sending the BIT stream, this implementation automatically sends SOF
 *
 * @param[in]   data   BIT stream data to be sent
 * @param[in]   bits   The length of the bit stream to be sent
 */
void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits) {
    m_is_responded = true;
    memcpy(m_nfc_tx_buffer, data, (bits / 8) + (bits % 8 > 0 ? 1 : 0));
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**@brief The function of sending n bits is implemented, and this implementation is automatically sent SOF
 *
 * @param[in]   data   BIT data to be sent
 * @param[in]   bits   To send a few bites
 */
void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits) {
    m_is_responded = true;
    m_nfc_tx_buffer[0] = data;
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**@brief The function of sending n bits is implemented, and this implementation is automatically sent SOF
 *
 * @param[in]   data   BIT data to be sent
 * @param[in]   bits   To send a few bites
 */
void nfc_tag_14a_tx_nbit_delay_window(uint8_t data, uint32_t bits) {
    m_is_responded = true;
    m_nfc_tx_buffer[0] = data;
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_WINDOW);
}

/**
 * 14A monitoring the packaging function of data processing from PCD
 */
void nfc_tag_14a_data_process(uint8_t *p_data) {
    // Compute the number of bits currently received
    uint16_t szDataBits = (NRF_NFCT->RXD.AMOUNT & (NFCT_RXD_AMOUNT_RXDATABITS_Msk | NFCT_RXD_AMOUNT_RXDATABYTES_Msk));
    // The resource that may be used in anti -collision
    nfc_tag_14a_coll_res_reference_t *auto_coll_res = m_tag_handler.get_coll_res != NULL ? m_tag_handler.get_coll_res() : NULL;

    // I don't know why, here the CPU must run empty for a period of time before the data can be received normally.
    // If you have any problems with the receiving data, please try to restore this. This is a problem found in 2021, but it disappeared again in 2022
    // It may be due to the update of the SDK version
    // for (int i = 0; i < 88; i++) __NOP();

    // Be sure to ensure that the received data is correct.IntersectionIntersection
    if (0 == szDataBits || (szDataBits > (MAX_NFC_RX_BUFFER_SIZE * 8))) {
        // NRF_LOG_INFO("Invalid size: %d\n", szDataBits);
        // If there are abnormal data received, we skip it directly without processing,
        // Because of this error receiving event caused by this possible interference
        return;
    }
    // Manually draw frame, separate data and strange school inspection
#if !NFC_TAG_14A_RX_PARITY_AUTO_DEL_ENABLE
    if (szDataBits >= 9) {
        //Since we do not need a strange school test for the time being, discard it directly when we take it out
        szDataBits = nfc_tag_14a_unwrap_frame(p_data, szDataBits, p_data, NULL);
    }
#endif

    // Start processing the received data, if it is a special frame, you can hand over the data to this link
    if (szDataBits <= 8) {
        // We may receive a Wupa or REQA instruction, or other special instructions
        bool isREQA = (p_data[0] == NFC_TAG_14A_CMD_REQA);
        bool isWUPA = (p_data[0] == NFC_TAG_14A_CMD_WUPA);
        // The trigger conditions are: REQA response in non -Halt mode
        // Temporary through: Wupa response in non -choice state, no matter what state is in the state, you can use the Wupa instruction to wake up
        if ((szDataBits == 7) && ((isREQA && m_tag_state_14a != NFC_TAG_STATE_14A_HALTED) || isWUPA)) {
            // The receiver of the 14A communication is notified, the internal state machine is reset
            if (m_tag_handler.cb_reset != NULL) {
                m_tag_handler.cb_reset();
            }
            // Only in the case that can provide anti -collision resources,
            if (auto_coll_res != NULL) {
                // The status machine is set to the preparation state, and the next operation is to enter the card selection link
                m_tag_state_14a = NFC_TAG_STATE_14A_READY;
                // After receiving the WUPA or REQA instruction, we need to reply to ATQA
                nfc_tag_14a_tx_bytes(auto_coll_res->atqa, 2, false);
                // NRF_LOG_INFO("ATQA reply.");
            } else {
                m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                NRF_LOG_INFO("Auto anti-collision resource no exists.");
            }
            return;
        } else {
            // TODOHere you can match some other instructions, call back some registered processing functions to handle this logic separately
            // Normal communication process will not have N bits of frames, because it is the anti -conflict frame used in the 14A protocol for BIT
            // So you can handle this protocol frame separately here to realize the tag similar to the UID back door card (Chinese Magic)
            // Note that if we find REQA or WUPA, we will not repeat the processing (only the special ratio special frame)
            if ((!isREQA && !isWUPA) && m_tag_handler.cb_state != NULL) {
                // If the 7bit processor is registered and successfully processed this command, the state machine update is completed
                m_tag_handler.cb_state(p_data, szDataBits);
                return;
            }
        }
        return;
    }
    //Make corresponding treatment according to the status of the current card
    switch (m_tag_state_14a) {
        // If you do not handle any tasks in the idle state and the dormant state, let the news from the stars go with the wind ~
        case NFC_TAG_STATE_14A_IDLE:
        case NFC_TAG_STATE_14A_HALTED: {
            break;
        }
        // Preparation status, processing news related to anti -collision
        case NFC_TAG_STATE_14A_READY: {
            static uint8_t uid[5] = { 0x00 };
            nfc_tag_14a_cascade_level_t level;
            // Extract cascade level
            if (szDataBits >= 16) {
                // Matching grade joint instructions
                switch (p_data[0]) {
                    case NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_1:
                        level = NFC_TAG_14A_CASCADE_LEVEL_1;
                        break;
                    case NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_2:
                        level = NFC_TAG_14A_CASCADE_LEVEL_2;
                        break;
                    case NFC_TAG_14A_CMD_ANTICOLL_OR_SELECT_3:
                        level = NFC_TAG_14A_CASCADE_LEVEL_3;
                        break;
                    case NFC_TAG_14A_CMD_HALT:
                        if (p_data[1] == 0x00) {
                            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                        }
                        return;
                    default: {
                        // After receiving the wrong level instruction, directly reset the status machine
                        NRF_LOG_INFO("[MFEMUL_SELECT] Incorrect cascade level received: %02x", p_data[0]);
                        m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                        return;
                    }
                }
                // Match the length of UID and prepare for the return data of UID
                switch (*auto_coll_res->size) {
                    case NFC_TAG_14A_UID_SINGLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    // The first level, only once
                            // The 4 -byte label can only be connected at most at one time
                            memcpy(uid, auto_coll_res->uid, 4);
                        } else {    // 4 -byte cards can never perform second -level coupons
                            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                            return;
                        }
                        break;
                    }
                    case NFC_TAG_14A_UID_DOUBLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    //At the first time, there is one left
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[0];
                            uid[2] = auto_coll_res->uid[1];
                            uid[3] = auto_coll_res->uid[2];
                        } else if (level == NFC_TAG_14A_CASCADE_LEVEL_2) {  //The second level is complete
                            memcpy(uid, auto_coll_res->uid + 3, 4);
                        } else {    //The 7 -byte card can never perform the third level
                            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                            return;
                        }
                        break;
                    }
                    case NFC_TAG_14A_UID_TRIPLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    // At the first level, there are two left
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[0];
                            uid[2] = auto_coll_res->uid[1];
                            uid[3] = auto_coll_res->uid[2];
                        } else if (level == NFC_TAG_14A_CASCADE_LEVEL_2) {  // The second level, there is still the remaining first -level joint
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[3];
                            uid[2] = auto_coll_res->uid[4];
                            uid[3] = auto_coll_res->uid[5];
                        } else {    // The last step of the 10 -byte card
                            memcpy(uid, auto_coll_res->uid + 6, 4);
                        }
                        break;
                    }
                }
                // BCC calculation, complete the final anti -collision data preparation
                nfc_tag_14a_append_bcc(uid, 4);
            } else {
                // Receive the grade joint instructions of the error length, reset the status machine
                m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                return;
            }
            // Incoming SELECT ALL for any cascade level
            if (szDataBits == 16 && p_data[1] == 0x20) {
                nfc_tag_14a_tx_bytes(uid, 5, false);
                // NRF_LOG_INFO("[MFEMUL_SELECT] SEL Reply.");
                break;
            }
            // Incoming SELECT CLx for any cascade level
            if (szDataBits == 72 && p_data[1] == 0x70) {
                if (memcmp(&p_data[2], uid, 4) == 0) {
                    bool cl_finished = (*auto_coll_res->size == NFC_TAG_14A_UID_SINGLE_SIZE && level == NFC_TAG_14A_CASCADE_LEVEL_1) ||
                                       (*auto_coll_res->size == NFC_TAG_14A_UID_DOUBLE_SIZE && level == NFC_TAG_14A_CASCADE_LEVEL_2) ||
                                       (*auto_coll_res->size == NFC_TAG_14A_UID_TRIPLE_SIZE && level == NFC_TAG_14A_CASCADE_LEVEL_3);
                    // NRF_LOG_INFO("SELECT CLx %02x%02x%02x%02x received\n", p_data[2], p_data[3], p_data[4], p_data[5]);
                    if (cl_finished) {
                        // NRF_LOG_INFO("[MFEMUL_SELECT] m_tag_state_14a = MFEMUL_WORK");
                        m_tag_state_14a = NFC_TAG_STATE_14A_ACTIVE;
                        nfc_tag_14a_tx_bytes(auto_coll_res->sak, 1, true);
                    } else {
                        // It is necessary to continue the level, so we need to respond to a data that marks the incomplete UID in SAK
                        nfc_tag_14a_tx_bytes(m_uid_incomplete_sak, 3, false);
                    }
                } else {
                    // IDLE, not our UID
                    m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                    // NRF_LOG_INFO("[MFEMUL_SELECT] m_tag_state_14a = MFEMUL_IDLE");
                }
                break;
            }
            // Unknown selection procedure
            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
            // NRF_LOG_INFO("[MFEMUL_SELECT] Unknown selection procedure");
            break;
        }
        // Activation status, re-post processing of any message
        case NFC_TAG_STATE_14A_ACTIVE: {
            // You need to judge whether you have received instructions that need to be handled directly without forwarding
            if (szDataBits == 32) {
                // Halt instruction
                if (p_data[0] == NFC_TAG_14A_CMD_HALT && p_data[1] == 0x00 && p_data[2] == 0x57 && p_data[3] == 0xCD) {
                    // Set the status machine to the suspension state, and then wait for the next round of communication
                    m_tag_state_14a = NFC_TAG_STATE_14A_HALTED;
                    return;
                }
                // RATS instruction
                if (p_data[0] == NFC_TAG_14A_CMD_RATS && nfc_tag_14a_checks_crc(p_data, 4)) {
                    // Make sure the sub -packaging opens the support of ATS
                    if (auto_coll_res->ats->length > 0) {
                        // Take out FSD and return according to the maximum FSD
                        uint8_t fsd = ats_fsdi_table[p_data[1] >> 4 & 0x0F] - 2;
                        // If the FSD is larger than the set of ATS, then returns normal ATS data, otherwise the data of the FSD limited length will be returned
                        uint8_t len = fsd >= auto_coll_res->ats->length ? auto_coll_res->ats->length : fsd;
                        // Back to ATS data according to FSD, FSD is the largest frame size supported by PCD. After removing CRC, it is the actual data frame size support
                        nfc_tag_14a_tx_bytes(auto_coll_res->ats->data, len, true);
                    } else {
                        nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                    }
                    // After handling the explicitly sending RATS instructions outside the outside, wait directly for the next round of communication
                    return;
                }
            }
            // No processing is successful, it may be some other data. You need to re-post processing
            if (m_tag_handler.cb_state != NULL) {    //Activation status, transfer the message to other registered processor processing
                m_tag_handler.cb_state(p_data, szDataBits);
                break;
            }
        }
    }
}

extern bool g_usb_led_marquee_enable;

/**
 * 14A incident callback function
 */
void nfc_tag_14a_event_callback(nrfx_nfct_evt_t const *p_event) {
    // Select action to process.
    switch (p_event->evt_id) {
        case NRFX_NFCT_EVT_FIELD_DETECTED: {
            sleep_timer_stop();

            g_is_tag_emulating = true;
            g_usb_led_marquee_enable = false;

            set_slot_light_color(1);
            TAG_FIELD_LED_ON()

            NRF_LOG_INFO("HF FIELD DETECTED");

            //Turn off the automatic anti -collision, MCU management all the interaction process, and then enable the NFC peripherals so that Io can be performed after enable
            // 20221108 Fix the different enable switching process of NRF52840 and NRF52832
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
            nrfx_nfct_autocolres_disable();
            nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
#else
            (*(uint32_t *)(0x4000559C)) |= (0x1UL);   // == nrfx_nfct_autocolres_disable();
            NRF_NFCT->TASKS_ACTIVATE = 1;
#endif

            //Directly enable receiving
            NRFX_NFCT_RX_BYTES
            break;
        }
        case NRFX_NFCT_EVT_FIELD_LOST: {
            g_is_tag_emulating = false;
            // call sleep_timer_start *after* unsetting g_is_tag_emulating
            sleep_timer_start(SLEEP_DELAY_MS_FIELD_NFC_LOST);

            TAG_FIELD_LED_OFF()
            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;

            NRF_LOG_INFO("HF FIELD LOST");
            break;
        }
        case NRFX_NFCT_EVT_TX_FRAMESTART: {
            // NRF_LOG_INFO("TX start.\n");
            // NRF_LOG_INFO("TX config is %d.\n", nrf_nfct_tx_frame_config_get(NRF_NFCT));
            break;
        }
        case NRFX_NFCT_EVT_TX_FRAMEEND: {
            // NRF_LOG_INFO("TX end.\n");
            // After the transmission is over, you need to be able to receive it
            NRFX_NFCT_RX_BYTES
            break;
        }
        case NRFX_NFCT_EVT_RX_FRAMEEND: {
            set_slot_light_color(1);
            TAG_FIELD_LED_ON()

            // NRF_LOG_INFO("RX FRAMEEND.\n");
            // TODO Remember a bug, if you do not reply to the message after receiving the message, you need to manually enable you
            //   Otherwise, the nrfx_nfct_evt_tx_frameend conditions above will not be triggered, and nrfx_nfct_rx_bytes will not be called
            // All the next communication will have problems. How can I play if there is a problem? Play an egg.
            m_is_responded = false;
            // One more layer of pressure stack, but it seems to have little effect on performance
            // This function processes the data sent by the card reader, and then read that you don't need to reply to the card reader. If you need it, reply
            // Don't reply if you don't need it, it makes sense, right?This is science.
            nfc_tag_14a_data_process(m_nfc_rx_buffer);
            // The above prompt tells us that when we do not need to reply to the card reader, we need to manually enable it
            if (!m_is_responded) {
                NRFX_NFCT_RX_BYTES
            }
            break;
        }
        case NRFX_NFCT_EVT_ERROR: {
            // According to the error reasons, the log prints to help the development of the possibilities during development
            switch (p_event->params.error.reason) {
                case NRFX_NFCT_ERROR_FRAMEDELAYTIMEOUT: {
                    //If we respond to the label in the communication window, but we did not respond in time, then we need to make an error printing
                    // If this error appears very frequently, it may be that the MCU processing speed does not keep up. At this time, the developer needs to optimize the code
                    if (m_is_responded) {
                        NRF_LOG_ERROR("NRFX_NFCT_ERROR_FRAMEDELAYTIMEOUT: %d", m_tag_state_14a);
                    }
                    break;
                }
                case NRFX_NFCT_ERROR_NUM: {
                    NRF_LOG_ERROR("NRFX_NFCT_ERROR_NUM");
                    break;
                }
            }
            break;
        }
        default: {
            NRF_LOG_INFO("No NFCT Event processor: %d\n", p_event->evt_id);
            break;
        }
    }
}

/**
 * The 14A status machine update function, which can set the 14A label to the specified state
 * @param state New state
 */
void nfc_tag_14a_set_state(nfc_tag_14a_state_t state) {
    m_tag_state_14a = state;
}

/**
 * 14A processor registration function
 * @param handler Processor handle
 */
void nfc_tag_14a_set_handler(nfc_tag_14a_handler_t *handler) {
    if (handler != NULL) {
        // Take it directly to the implementation of the introduction to our global object
        m_tag_handler.cb_reset = handler->cb_reset;
        m_tag_handler.cb_state = handler->cb_state;
        m_tag_handler.get_coll_res = handler->get_coll_res;
    }
}

static enum  {
    NFC_SENSE_STATE_NONE,
    NFC_SENSE_STATE_DISABLE,
    NFC_SENSE_STATE_ENABLE,
} m_nfc_sense_state = NFC_SENSE_STATE_NONE;

/**
 * 14A field sensing enable and closed capacity to implement functions
 * @param enable Whether to make the field induction
 */
void nfc_tag_14a_sense_switch(bool enable) {
    if (m_nfc_sense_state == NFC_SENSE_STATE_NONE || m_nfc_sense_state == NFC_SENSE_STATE_DISABLE) {
        if (enable) {
            m_nfc_sense_state = NFC_SENSE_STATE_ENABLE;
            // Initialized interrupt event and callback
            nrfx_nfct_config_t nnct = { .rxtx_int_mask = (uint32_t)0xFFFFFFFF, .cb = nfc_tag_14a_event_callback };
            if (nrfx_nfct_init(&nnct) != NRFX_SUCCESS) {
                NRF_LOG_INFO("Cannot setup NFC!");
            }
            // Starting field sensing
            nrfx_nfct_enable();
        }
    } else {
        if (!enable) {
            m_nfc_sense_state = NFC_SENSE_STATE_DISABLE;
            //Directly anti -initialization NFC peripherals can turn off NFC field induction
            // SDK inside us to call us nrfx_nfct_disable
            nrfx_nfct_uninit();
        }
    }
}

bool is_valid_uid_size(uint8_t uid_length) {
    return uid_length == NFC_TAG_14A_UID_SINGLE_SIZE ||
           uid_length == NFC_TAG_14A_UID_DOUBLE_SIZE ||
           uid_length == NFC_TAG_14A_UID_TRIPLE_SIZE;
}
