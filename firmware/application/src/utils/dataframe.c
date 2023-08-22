#include "dataframe.h"
#include "hex_utils.h"


#define NRF_LOG_MODULE_NAME data_frame
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


/*
 * *********************************************************************************************************************************
 *                          Variable length data frame format
 *                                  Designed by proxgrind
 *                                  Date: 20221205
 *
 *      0           1           2 3         45               6 7                    8                8 + n           8 + n + 1
 *  SOF(1byte)  LRC(1byte)  CMD(2byte)  Status(2byte)  Data Length(2byte)  Frame Head LRC(1byte)  Data(length)  Frame All LRC(1byte)
 *     0x11       0xEF        cmd(u16)    status(u16)      length(u16)              lrc(u8)          data(u8*)       lrc(u8)
 *
 *  The data length max is 512, frame length max is 1 + 1 + 2 + 2 + 2 + 1 + n + 1 = (10 + n)
 *  So, one frame will than 10 byte.
 * *********************************************************************************************************************************
 */

#define DATA_PACK_TRANSMISSION_ON   0x11
#define DATA_LRC_CUT(val)   ((uint8_t)(0x100 - val))


static uint8_t m_data_rx_buffer[DATA_PACK_MAX_DATA_LENGTH + DATA_PACK_BASE_LENGTH];
static uint8_t m_data_tx_buffer[DATA_PACK_MAX_DATA_LENGTH + DATA_PACK_BASE_LENGTH];
static uint16_t m_data_rx_position = 0;
static uint8_t m_data_rx_lrc = 0;
static uint16_t m_data_cmd;
static uint16_t m_data_status;
static uint16_t m_data_len;
static uint8_t *m_data_buffer;
static volatile bool m_data_completed = false;
static data_frame_cbk_t m_frame_process_cbk = NULL;
static data_frame_tx_t m_frame_tx_buf_info = {
    .buffer = m_data_tx_buffer,    // default buffer
};


/**
 * @brief 数据包创建，将创建之后的数据包放到缓冲区中，等待发送完毕之后设置非busy状态
 * @param cmd: 指令应答
 * @param status: 应答状态
 * @param length: 应答数据长度
 * @param data: 应答数据
 */
data_frame_tx_t *data_frame_make(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t lrc_tx = 0x00;
    uint16_t i, j;
    // sof
    m_frame_tx_buf_info.buffer[0] = DATA_PACK_TRANSMISSION_ON;
    // sof lrc
    lrc_tx += m_frame_tx_buf_info.buffer[0];
    m_frame_tx_buf_info.buffer[1] = DATA_LRC_CUT(lrc_tx);
    lrc_tx += m_frame_tx_buf_info.buffer[1];
    // cmd
    num_to_bytes(cmd, 2, &m_frame_tx_buf_info.buffer[2]);
    // status
    num_to_bytes(status, 2, &m_frame_tx_buf_info.buffer[4]);
    // length
    num_to_bytes(length, 2, &m_frame_tx_buf_info.buffer[6]);
    // head lrc
    for (i = 2; i < 8; i++) {
        lrc_tx += m_frame_tx_buf_info.buffer[i];
    }
    m_frame_tx_buf_info.buffer[8] = DATA_LRC_CUT(lrc_tx);
    lrc_tx += m_frame_tx_buf_info.buffer[8];
    // data
    if (length > 0 && data != NULL) {
        for (i = 9, j = 0; j < length; i++, j++) {
            m_frame_tx_buf_info.buffer[i] = data[j];
            lrc_tx += m_frame_tx_buf_info.buffer[i];
        }
    }
    // length out.
    m_frame_tx_buf_info.length = (length + DATA_PACK_BASE_LENGTH);
    // data all lrc
    m_data_tx_buffer[m_frame_tx_buf_info.length - 1] = DATA_LRC_CUT(lrc_tx);;
    return (&m_frame_tx_buf_info);
}

/**
 * @brief Data frame reset
 */
void data_frame_reset(void) {
    m_data_rx_position = 0;
    m_data_rx_lrc = 0;
}

/**
 * @brief 数据包接收，用于接收发送过来的数据包并且进行拼接处理
 * @param data: 接收到的字节数组
 * @param length: 接收到的字节数组的长度
 */
void data_frame_receive(uint8_t *data, uint16_t length) {
    // buffer wait process
    if (m_data_completed) {
        NRF_LOG_ERROR("Data frame wait process.");
        return;
    }
    // buffer overflow
    if (m_data_rx_position + length >= sizeof(m_data_rx_buffer)) {
        NRF_LOG_ERROR("Data frame wait overflow.");
        data_frame_reset();
        return;
    }
    // frame process
    for (int i = 0; i < length; i++) {
        // copy to buffer
        m_data_rx_buffer[m_data_rx_position] = data[i];
        if (m_data_rx_position < 2) {   // start of frame
            if (m_data_rx_position == 0) {
                if (m_data_rx_buffer[m_data_rx_position] != DATA_PACK_TRANSMISSION_ON) {
                    // not sof byte
                    NRF_LOG_ERROR("Data frame no sof byte.");
                    data_frame_reset();
                    return;
                }
            }
            if (m_data_rx_position == 1) {
                if (m_data_rx_buffer[m_data_rx_position] != DATA_LRC_CUT(m_data_rx_lrc)) {
                    // not sof lrc byte
                    NRF_LOG_ERROR("Data frame sof lrc error.");
                    data_frame_reset();
                    return;
                }
            }
        } else if (m_data_rx_position == 8) {  // frame head lrc
            if (m_data_rx_buffer[m_data_rx_position] != DATA_LRC_CUT(m_data_rx_lrc)) {
                // frame head lrc error
                NRF_LOG_ERROR("Data frame head lrc error.");
                data_frame_reset();
                return;
            }
            // frame head complete, cache info
            m_data_cmd = bytes_to_num(&m_data_rx_buffer[2], 2);
            m_data_status = bytes_to_num(&m_data_rx_buffer[4], 2);
            m_data_len = bytes_to_num(&m_data_rx_buffer[6], 2);
            NRF_LOG_INFO("Data frame data length %d.", m_data_len);
            // check data length
            if (m_data_len > DATA_PACK_MAX_DATA_LENGTH) {
                NRF_LOG_ERROR("Data frame data length too than of max.");
                data_frame_reset();
                return;
            }
        } else if (m_data_rx_position > 8) {   // frame data
            // check all data ready.
            if (m_data_rx_position == (8 + m_data_len + 1)) {
                if (m_data_rx_buffer[m_data_rx_position] == DATA_LRC_CUT(m_data_rx_lrc)) {
                    // ok, lrc for data is check success.
                    // and we are receive completed
                    m_data_buffer = m_data_len > 0 ? &m_data_rx_buffer[9] : NULL;
                    m_data_completed = true;
                } else {
                    // data frame lrc error
                    NRF_LOG_ERROR("Data frame finally lrc error.");
                    data_frame_reset();
                }
                return;
            }
        }
        // calculate lrc
        m_data_rx_lrc += data[i];
        // index update
        m_data_rx_position++;
    }
}

/**
 * @brief 数据包处理，当接收到的数据形成了一个完整的帧之后，
 *          将会通过此函数分发处理任务，此函数会回调通知数据处理者
 *          如果数据处理是耗时操作，则需要将此函数放在main循环中调用
 */
void data_frame_process(void) {
    // check if data frame
    if (m_data_completed) {
        // to process data frame
        if (m_frame_process_cbk != NULL) {
            m_frame_process_cbk(m_data_cmd, m_data_status, m_data_len, m_data_buffer);
        }
        // reset after process data frame.
        data_frame_reset();
        m_data_completed = false;
    }
}

/**
 * @brief 数据包处理回调注册
 */
void on_data_frame_complete(data_frame_cbk_t callback) {
    m_frame_process_cbk = callback;
}
