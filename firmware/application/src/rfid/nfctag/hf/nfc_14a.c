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

// 使用宏定义展开数据的接收使能实现
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


// 14443a协议状态机
nfc_tag_14a_state_t m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;

// 14443a协议处理器
nfc_tag_14a_handler_t m_tag_handler = {
    .cb_reset = NULL,       // 标签重置回调
    .cb_state = NULL,       // 标签状态机回调
    .get_coll_res = NULL,   // 标签的防冲突资源的获取封装
};

// 字节镜像
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

// RATS FSDI 长度查表
const uint16_t ats_fsdi_table[] = {
    // 0 - 8
    16, 24, 32, 40, 48, 64, 96, 128, 256,
    // 9 - F
    256, 256, 256, 256, 256, 256, 256,
};


// 当前是否已经应答
static volatile  bool m_is_responded = false;
// 接收缓冲区
static uint8_t m_nfc_rx_buffer[MAX_NFC_RX_BUFFER_SIZE] = { 0x00 };
static uint8_t m_nfc_tx_buffer[MAX_NFC_TX_BUFFER_SIZE] = { 0x00 };
// N次级联需要用上的SAK，在SAK中的 '第三个' 比特为1时，标志UID不完整
static uint8_t m_uid_incomplete_sak[]   = { 0x04, 0xda, 0x17 };

/**
 * @brief 计算BCC
 *
 */
void nfc_tag_14a_create_bcc(uint8_t *pbtData, size_t szLen, uint8_t *pbtBcc) {
    // 最好是在使用输出缓冲区时重置其
    *pbtBcc = 0x00;
    do {
        *pbtBcc ^= *pbtData++;
    } while (--szLen);
}

/**
 * @brief 追加BCC到数据流尾部
 *
 */
inline void nfc_tag_14a_append_bcc(uint8_t *pbtData, size_t szLen) {
    nfc_tag_14a_create_bcc(pbtData, szLen, pbtData + szLen);
}

/**
 * @brief 在数据流尾部追加CRC，记住：
 *  pbtData一定要有足够的长度去容纳CRC计算结果（两个字节）
 *
 */
inline void nfc_tag_14a_append_crc(uint8_t *pbtData, size_t szLen) {
    calc_14a_crc_lut(pbtData, szLen, pbtData + szLen);
}

/**
 * @brief 检查CRC是否正确
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
* @brief  ：包装ISO14443A的比特帧
*           自动进行奇偶校验位与数据的包装合并
* @param  ：pbtTx：将要传输的比特流
*          szTxBits：比特流的长度
*          pbtTxPar：奇偶校验位的比特流，这个数据的长度一定是 szTxBits / 8，也就是说
*                    实际上合并之后的比特流的组成结构为：
*                    data(1byte) - par(1bit) - data(1byte) - par(1bit) ...
*                      00001000  -   0       - 10101110    - 1
*                    这种类似的数据结构
*          pbtFrame： 最终组装完成的数据的缓冲区
* @retval ：比特流组装结果缓冲区的长度，注意，是比特长度，换算字节请除以8再取模
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
* @brief  ：解包ISO14443A的比特帧
*           自动进行奇偶校验位与数据的解包分离
* @param  ：pbtFrame：将要解包的比特流
*          szFrameBits：比特流的长度
*          pbtRx：解包后的比特流的存放的缓冲区，数据区
*          pbtRxPar： 解包后的比特流的存放的缓冲区，奇偶校验位区
* @retval ：比特流解包后的数据长度，注意，是数据区的比特流长度，换算字节请 retval / 8
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
 * @briedf: Function for response reader core implemented
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


/**@brief 发送字节流的函数实现，此实现自动发送SOF
 *
 * @param[in]   data        要发送的字节流数据
 * @param[in]   bytes       要发送的字节流的长度
 * @param[in]   appendCrc   是否在发送完成字节流后，自动追加发送crc16校验
 */
void nfc_tag_14a_tx_bytes(uint8_t *data, uint32_t bytes, bool appendCrc) {
    NFC_14A_TX_BYTE_CORE(data, bytes, appendCrc, NRF_NFCT_FRAME_DELAY_MODE_WINDOW);
}

/**@brief 发送字节流的函数实现，此实现自动发送SOF
 *
 * @param[in]   data        要发送的字节流数据
 * @param[in]   bytes       要发送的字节流的长度
 * @param[in]   appendCrc   是否在发送完成字节流后，自动追加发送crc16校验
 */
void nfc_tag_14a_tx_bytes_delay_freerun(uint8_t *data, uint32_t bytes, bool appendCrc) {
    NFC_14A_TX_BYTE_CORE(data, bytes, appendCrc, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**
 * @briedf: Function for response reader core implemented
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

/**@brief 发送bit流的函数实现，此实现自动发送SOF
 *
 * @param[in]   data   要发送的bit流数据
 * @param[in]   bits   要发送的bit流的长度
 */
void nfc_tag_14a_tx_bits(uint8_t *data, uint32_t bits) {
    m_is_responded = true;
    memcpy(m_nfc_tx_buffer, data, (bits / 8) + (bits % 8 > 0 ? 1 : 0));
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**@brief 发送N个bit的函数实现，此实现自动发送SOF
 *
 * @param[in]   data   要发送的bit数据
 * @param[in]   bits   要发送几个bit
 */
void nfc_tag_14a_tx_nbit(uint8_t data, uint32_t bits) {
    m_is_responded = true;
    m_nfc_tx_buffer[0] = data;
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_FREERUN);
}

/**@brief 发送N个bit的函数实现，此实现自动发送SOF
 *
 * @param[in]   data   要发送的bit数据
 * @param[in]   bits   要发送几个bit
 */
void nfc_tag_14a_tx_nbit_delay_window(uint8_t data, uint32_t bits) {
    m_is_responded = true;
    m_nfc_tx_buffer[0] = data;
    NFC_14A_TX_BITS_CORE(bits, NRF_NFCT_FRAME_DELAY_MODE_WINDOW);
}

/**
 * 14a监听到PCD过来的数据处理的封装函数
 */
void nfc_tag_14a_data_process(uint8_t *p_data) {
    // 统计一下当前收到的bit数
    uint16_t szDataBits = (NRF_NFCT->RXD.AMOUNT & (NFCT_RXD_AMOUNT_RXDATABITS_Msk | NFCT_RXD_AMOUNT_RXDATABYTES_Msk));
    // 防冲撞可能要用上的资源
    nfc_tag_14a_coll_res_referen_t *auto_coll_res = m_tag_handler.get_coll_res != NULL ? m_tag_handler.get_coll_res() : NULL;

    // 我也不知道为什么，这里CPU必须要空跑一段周期，数据才能正常收到。
    // 如果接收数据有任何问题，请尝试恢复此处，这个是2021年发现的问题，但是2022年又消失了
    // 可能是由于更新了SDK版本
    // for (int i = 0; i < 88; i++) __NOP();

    // 一定要确保接收到的数据无误，上限和下限都要判断处理！！！
    if (0 == szDataBits || (szDataBits > (MAX_NFC_RX_BUFFER_SIZE * 8))) {
        // NRF_LOG_INFO("Invalid size: %d\n", szDataBits);
        // 如果有异常的数据接收到，我们直接跳过，不处理，
        // 因为这个有可能干扰导致的错误接收事件
        return;
    }
    // 手动抽帧，分离数据和奇偶校验位
#if !NFC_TAG_14A_RX_PARITY_AUTO_DEL_ENABLE
    if (szDataBits >= 9) {
        // 由于我们暂时不需奇偶校验位，所以取出的时候直接丢弃
        szDataBits = nfc_tag_14a_unwrap_frame(p_data, szDataBits, p_data, NULL);
    }
#endif

    // 开始处理接收到的数据，如果是比特帧可以将数据交由此环节处理
    if (szDataBits <= 8) {
        // 我们可能收到了一个wupa或者reqa指令，或者其他的特殊指令
        bool isREQA = (p_data[0] == NFC_TAG_14A_CMD_REQA);
        bool isWUPA = (p_data[0] == NFC_TAG_14A_CMD_WUPA);
        // 触发条件为：非halt模式下的REQA响应
        // 暂时全通过：非选择状态下的WUPA响应，现在无论处于何种状态都能用WUPA指令唤醒
        if ((szDataBits == 7) && ((isREQA && m_tag_state_14a != NFC_TAG_STATE_14A_HALTED) || isWUPA)) {
            // 通知14a通信的接管者们，该重置内部状态机了
            if (m_tag_handler.cb_reset != NULL) {
                m_tag_handler.cb_reset();
            }
            // 仅在能提供防冲撞资源的情况下，
            if (auto_coll_res != NULL) {
                // 状态机设置为准备状态，下次操作是进入选卡环节
                m_tag_state_14a = NFC_TAG_STATE_14A_READY;
                // 收到了wupa或者reqa指令，此时我们需要回复atqa
                nfc_tag_14a_tx_bytes(auto_coll_res->atqa, 2, false);
                // NRF_LOG_INFO("ATQA reply.");
            } else {
                m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                NRF_LOG_INFO("Auto anti-collision resource no exists.");
            }
            return;
        } else {
            // TODO 此处可以匹配一些其他的指令，回调一些注册好的处理函数单独处理此逻辑
            // 正常通信过程不会有N个比特的帧，因为那是14a协议里面用于面向bit的防冲突帧
            // 所以此处可以单独处理这个协议帧，实现类似UID后门卡的标签（chinese magic）
            // 注意，我们如果是发现了REQA或者WUPA，就不去重复处理了（只处理特殊比特帧）
            if ((!isREQA && !isWUPA) && m_tag_handler.cb_state != NULL) {
                // 如果7bit的处理器被注册并且成功的处理了此命令，则完成此次状态机更新
                m_tag_handler.cb_state(p_data, szDataBits);
                return;
            }
        }
        return;
    }
    // 根据当前卡片的状态做出相应的处理
    switch (m_tag_state_14a) {
        // 空闲状态和休眠状态不处理任何任务，就让来自星星的消息随风而去吧~
        case NFC_TAG_STATE_14A_IDLE:
        case NFC_TAG_STATE_14A_HALTED: {
            break;
        }
        // 准备状态，处理跟防冲撞有关的消息
        case NFC_TAG_STATE_14A_READY: {
            static uint8_t uid[5] = { 0x00 };
            nfc_tag_14a_cascade_level_t level;
            // Extract cascade level
            if (szDataBits >= 16) {
                // 匹配级联指令
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
                        // 收到了错误的级联指令，直接重置状态机
                        NRF_LOG_INFO("[MFEMUL_SELECT] Incorrect cascade level received: %02x", p_data[0]);
                        m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                        return;
                    }
                }
                // 匹配UID长度，为uid的返回数据做准备
                switch (*auto_coll_res->size) {
                    case NFC_TAG_14A_UID_SINGLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    // 首次级联，只有一次
                            // 4字节的标签最多只能一次级联
                            memcpy(uid, auto_coll_res->uid, 4);
                        } else {    // 4字节的卡永远不能进行第二次级联
                            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                            return;
                        }
                        break;
                    }
                    case NFC_TAG_14A_UID_DOUBLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    // 首次级联，还剩一次
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[0];
                            uid[2] = auto_coll_res->uid[1];
                            uid[3] = auto_coll_res->uid[2];
                        } else if (level == NFC_TAG_14A_CASCADE_LEVEL_2) {  // 第二次级联已经完整
                            memcpy(uid, auto_coll_res->uid + 3, 4);
                        } else {    // 7字节的卡永远不能进行第三次级联
                            m_tag_state_14a = NFC_TAG_STATE_14A_IDLE;
                            return;
                        }
                        break;
                    }
                    case NFC_TAG_14A_UID_TRIPLE_SIZE: {
                        if (level == NFC_TAG_14A_CASCADE_LEVEL_1) {    // 首次级联，还剩两次
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[0];
                            uid[2] = auto_coll_res->uid[1];
                            uid[3] = auto_coll_res->uid[2];
                        } else if (level == NFC_TAG_14A_CASCADE_LEVEL_2) {  // 第二次级联，还剩余一次级联
                            uid[0] = NFC_TAG_14A_CASCADE_CT;
                            uid[1] = auto_coll_res->uid[3];
                            uid[2] = auto_coll_res->uid[4];
                            uid[3] = auto_coll_res->uid[5];
                        } else {    // 10字节的卡的最后一次级联
                            memcpy(uid, auto_coll_res->uid + 6, 4);
                        }
                        break;
                    }
                }
                // BCC计算，完成最终的防冲撞数据准备
                nfc_tag_14a_append_bcc(uid, 4);
            } else {
                // 收到了错误长度的级联指令，重置状态机
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
                        // 此处需要继续级联，因此我们需要回应一个在SAK内标志UID不完整的数据
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
        // 激活状态，转发处理任何消息
        case NFC_TAG_STATE_14A_ACTIVE: {
            // 需要判断是否收到了需要直接处理而不转发的指令
            if (szDataBits == 32) {
                // HALT指令
                if (p_data[0] == NFC_TAG_14A_CMD_HALT && p_data[1] == 0x00 && p_data[2] == 0x57 && p_data[3] == 0xCD) {
                    // 将状态机置为中止态，然后等待下一轮通信
                    m_tag_state_14a = NFC_TAG_STATE_14A_HALTED;
                    return;
                }
                // RATS指令
                if (p_data[0] == NFC_TAG_14A_CMD_RATS && nfc_tag_14a_checks_crc(p_data, 4)) {
                    // 确保子封装开启了ATS的支持
                    if (auto_coll_res->ats->length > 0) {
                        // 将FSD取出，根据最大FSD进行返回
                        uint8_t fsd = ats_fsdi_table[p_data[1] >> 4 & 0x0F] - 2;
                        // 如果fsd大于设置的ats长度，那么就返回正常的ats数据，否则返回fsd限定长度的数据
                        uint8_t len = fsd >= auto_coll_res->ats->length ? auto_coll_res->ats->length : fsd;
                        // 根据FSD返回ATS数据，FSD是PCD支持的最大帧大小，去掉CRC后才是实际的数据帧大小支持
                        nfc_tag_14a_tx_bytes(auto_coll_res->ats->data, len, true);
                    } else {
                        nfc_tag_14a_tx_nbit_delay_window(NAK_INVALID_OPERATION_TBIV, 4);
                    }
                    // 在外部直接处理了明文发送的RATS指令之后直接等待下一轮通信
                    return;
                }
            }
            // 没有处理成功，可能是其他的一些数据，需要转发处理
            if (m_tag_handler.cb_state != NULL) {    // 激活状态，将消息转由其他被注册的处理器处理
                m_tag_handler.cb_state(p_data, szDataBits);
                break;
            }
        }
    }
}

extern bool g_usb_led_marquee_enable;

/**
 * 14a事件回调函数
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

            // 关闭自动防冲撞，MCU管理所有的交互过程，然后使能NFC外设，使能之后就可以进行IO了
            // 20221108 修复nrf52840与nrf52832不同的使能切换流程
#if defined(NRF52833_XXAA) || defined(NRF52840_XXAA)
            nrfx_nfct_autocolres_disable();
            nrfx_nfct_state_force(NRFX_NFCT_STATE_ACTIVATED);
#else
            (*(uint32_t *)(0x4000559C)) |= (0x1UL);   // == nrfx_nfct_autocolres_disable();
            NRF_NFCT->TASKS_ACTIVATE = 1;
#endif

            // 直接使能接收
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
            // 传输结束后需要使能接收
            NRFX_NFCT_RX_BYTES
            break;
        }
        case NRFX_NFCT_EVT_RX_FRAMEEND: {
            set_slot_light_color(1);
            TAG_FIELD_LED_ON()

            // NRF_LOG_INFO("RX FRAMEEND.\n");
            // TODO 谨记一个BUG，在收到消息后如果不回复消息，就需要手动使能接收
            //   不然上面的 NRFX_NFCT_EVT_TX_FRAMEEND 条件不会触发，不会调用 NRFX_NFCT_RX_BYTES
            //   接下来的所有的通信，都将会出问题，出问题了还怎么玩，玩个蛋嘞。
            m_is_responded = false;
            // 多一层压栈，但是似乎对性能影响不大
            // 这个函数处理了读卡器发过来的数据，然后看没有需要回复读卡器，有需要的话就回复
            // 没需要的就不回复，很有道理是吧？这就是科学。
            nfc_tag_14a_data_process(m_nfc_rx_buffer);
            // 上面的提示告诉我们，我们在不需要回复读卡器的时候，需要手动使能接收
            if (!m_is_responded) {
                NRFX_NFCT_RX_BYTES
            }
            break;
        }
        case NRFX_NFCT_EVT_ERROR: {
            // 根据错误原因，进行日志打印，以帮助开发时排查可能性的BUG
            switch (p_event->params.error.reason) {
                case NRFX_NFCT_ERROR_FRAMEDELAYTIMEOUT: {
                    // 如果我们在通信窗口中回应了标签但是却是没有及时回应，那就需要进行报错打印
                    // 如果此错误非常频繁的出现，则可能是MCU处理速度没跟上，此时开发者就需要优化代码了
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
            NRF_LOG_INFO("No NFCT Event processer: %d\n", p_event->evt_id);
            break;
        }
    }
}

/**
 * 14A的状态机更新函数，可将14A标签置为指定的状态
 * @param state 新的状态
 */
void nfc_tag_14a_set_state(nfc_tag_14a_state_t state) {
    m_tag_state_14a = state;
}

/**
 * 14A的处理器注册函数
 * @param handler 处理器句柄
 */
void nfc_tag_14a_set_handler(nfc_tag_14a_handler_t *handler) {
    if (handler != NULL) {
        // 直接取出传入的实现赋值到我们的全局对象即可
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
 * 14A的场感应使能和闭能实现函数
 * @param enable 是否使能场感应
 */
void nfc_tag_14a_sense_switch(bool enable) {
    if (m_nfc_sense_state == NFC_SENSE_STATE_NONE || m_nfc_sense_state == NFC_SENSE_STATE_DISABLE) {
        if (enable) {
            m_nfc_sense_state = NFC_SENSE_STATE_ENABLE;
            // 初始化中断事件和回调
            nrfx_nfct_config_t nnct = { .rxtx_int_mask = (uint32_t)0xFFFFFFFF, .cb = nfc_tag_14a_event_callback };
            if (nrfx_nfct_init(&nnct) != NRFX_SUCCESS) {
                NRF_LOG_INFO("Cannot setup NFC!");
            }
            // 启动场感应
            nrfx_nfct_enable();
        }
    } else {
        if (!enable) {
            m_nfc_sense_state = NFC_SENSE_STATE_DISABLE;
            // 直接反初始化NFC外设即可关闭NFC场感应
            // SDK内部帮我们调用了 nrfx_nfct_disable
            nrfx_nfct_uninit();
        }
    }
}

bool is_valid_uid_size(uint8_t uid_length) {
    return uid_length == NFC_TAG_14A_UID_SINGLE_SIZE ||
           uid_length == NFC_TAG_14A_UID_DOUBLE_SIZE ||
           uid_length == NFC_TAG_14A_UID_TRIPLE_SIZE;
}
