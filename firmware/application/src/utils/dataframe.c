#include "dataframe.h"
#include "netdata.h"
#include "app_util_platform.h"

#define NRF_LOG_MODULE_NAME data_frame
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static netdata_frame_raw_t m_netdata_frame_rx_buf;
static netdata_frame_raw_t m_netdata_frame_tx_buf;
static data_frame_tx_t m_frame_tx_buf_info = {
    .buffer = (uint8_t *) &m_netdata_frame_tx_buf,  // default buffer
};
static uint16_t m_data_rx_position = 0;
static uint16_t m_data_cmd;
static uint16_t m_data_status;
static uint16_t m_data_len;
static uint8_t *m_data_buffer;
static volatile bool m_data_completed = false;
static data_frame_origin_t m_data_origin = DATA_FRAME_ORIGIN_UNKNOWN;
static data_frame_origin_t m_dispatch_origin = DATA_FRAME_ORIGIN_UNKNOWN;
static bool m_dispatching = false;
static bool m_receiving = false;
static bool m_reset_pending = false;
static data_frame_cbk_t m_frame_process_cbk = NULL;

static uint8_t compute_lrc(uint8_t *buf, uint16_t bufsize) {
    uint8_t lrc = 0x00;
    for (uint16_t i = 0; i < bufsize; i++) {
        lrc += buf[i];
    }
    return 0x100 - lrc;
}

//
//  !!!!!!!!!!!!!!!!! NRF_LOG_HEXDUMP_INFO() printing long data can cause freezing and needs to be fixed. !!!!!!!!!!!!!!!!!
//  FIXME.
//

/**
 * @brief: create a packet, put the created data packet into the buffer, and wait for the post to set up a non busy state
 * @param cmd: instructionResponse
 * @param status:responseStatus
 * @param length: answerDataLength
 * @param data: answerData
 */
data_frame_tx_t *data_frame_make(uint16_t cmd, uint16_t status, uint16_t data_length, uint8_t *data) {
    if (data_length > 0 && data == NULL) {
        NRF_LOG_ERROR("data_frame_make error, null pointer.");
        return NULL;
    }
    if (data_length > 4096) {
        NRF_LOG_ERROR("data_frame_make error, too much data.");
        return NULL;
    }

    // NRF_LOG_INFO("TX Data frame: cmd = 0x%04x (%i), status = 0x%04x, length = %d%s", cmd, cmd, status, data_length, data_length > 0 ? ", data =" : "");
    // if (data_length > 0) {
    //     NRF_LOG_HEXDUMP_INFO(data, data_length);
    // }

    netdata_frame_postamble_t *tx_post = (netdata_frame_postamble_t *)((uint8_t *)&m_netdata_frame_tx_buf + sizeof(netdata_frame_preamble_t) + data_length);
    // sof
    m_netdata_frame_tx_buf.pre.sof = NETDATA_FRAME_SOF;
    // sof lrc
    m_netdata_frame_tx_buf.pre.lrc1 = compute_lrc((uint8_t *)&m_netdata_frame_tx_buf.pre, offsetof(netdata_frame_preamble_t, lrc1));
    // cmd
    m_netdata_frame_tx_buf.pre.cmd = U16HTONS(cmd);
    // status
    m_netdata_frame_tx_buf.pre.status = U16HTONS(status);
    // data_length
    m_netdata_frame_tx_buf.pre.len = U16HTONS(data_length);
    // head lrc
    m_netdata_frame_tx_buf.pre.lrc2 = compute_lrc((uint8_t *)&m_netdata_frame_tx_buf.pre, offsetof(netdata_frame_preamble_t, lrc2));
    // data
    if (data_length > 0) {
        memcpy(&m_netdata_frame_tx_buf.data, data, data_length);
    }
    // length out.
    m_frame_tx_buf_info.length = (sizeof(netdata_frame_preamble_t) + data_length + sizeof(netdata_frame_postamble_t));
    // data all lrc
    tx_post->lrc3 = compute_lrc((uint8_t *)&m_netdata_frame_tx_buf.data, data_length);
    return (&m_frame_tx_buf_info);
}

/**
 * @brief Data frame reset
 */
// Called only with interrupts masked; the RX owner must have released its buffer.
static void reset_locked(void) {
    m_data_rx_position = 0;
    m_data_origin = DATA_FRAME_ORIGIN_UNKNOWN;
    m_data_completed = false;
    m_reset_pending = false;
}

static void request_reset_locked(void) {
    if (m_dispatching) {
        return;
    }
    if (m_receiving) {
        m_reset_pending = true;
    } else {
        reset_locked();
    }
}

void data_frame_reset(void) {
    CRITICAL_REGION_ENTER();
    request_reset_locked();
    CRITICAL_REGION_EXIT();
}

data_frame_origin_t data_frame_get_origin(void) {
    return m_dispatch_origin;
}

void data_frame_disconnect(data_frame_origin_t origin) {
    CRITICAL_REGION_ENTER();
    if (m_data_origin == origin) {
        // Drop partial and queued commands from the old connection, not another transport.
        request_reset_locked();
    }
    CRITICAL_REGION_EXIT();
}

/**
 * @brief Package receiving, which is used to receive the sent from the data packet and perform splicing processing
 * @param data: Receive byte array
 * @param length:The length of the receiving byte array
 */
void data_frame_receive(uint8_t *data, uint16_t length) {
    data_frame_receive_from(data, length, DATA_FRAME_ORIGIN_UNKNOWN);
}

static void receive_owned(uint8_t *data, uint16_t length, data_frame_origin_t origin) {
    // buffer wait process
    if (m_data_completed) {
        NRF_LOG_ERROR("Data frame wait process.");
        return;
    }
    if (length == 0) {
        return;
    }
    if (data == NULL || (origin != DATA_FRAME_ORIGIN_UNKNOWN &&
                         origin != DATA_FRAME_ORIGIN_USB && origin != DATA_FRAME_ORIGIN_BLE)) {
        data_frame_reset();
        return;
    }
    if (m_data_rx_position != 0 && m_data_origin != origin) {
        // Never splice bytes from different transports into one command.
        data_frame_reset();
        return;
    }
    // buffer overflow
    if (m_data_rx_position + length > sizeof(m_netdata_frame_rx_buf)) {
        NRF_LOG_ERROR("Data frame wait overflow.");
        data_frame_reset();
        return;
    }
    // frame process
    if (m_data_rx_position == 0) {
        m_data_origin = origin;
    }
    for (int i = 0; i < length; i++) {
        // copy to buffer
        ((uint8_t *)(&m_netdata_frame_rx_buf))[m_data_rx_position] = data[i];
        if (m_data_rx_position == offsetof(netdata_frame_preamble_t, sof)) {
            if (m_netdata_frame_rx_buf.pre.sof != NETDATA_FRAME_SOF) {
                // not sof byte
                NRF_LOG_ERROR("Data frame no sof byte.");
                data_frame_reset();
                return;
            }
        } else if (m_data_rx_position == offsetof(netdata_frame_preamble_t, lrc1)) {
            if (m_netdata_frame_rx_buf.pre.lrc1 != compute_lrc((uint8_t *)&m_netdata_frame_rx_buf.pre, offsetof(netdata_frame_preamble_t, lrc1))) {
                // not sof lrc byte
                NRF_LOG_ERROR("Data frame sof lrc error.");
                data_frame_reset();
                return;
            }
        } else if (m_data_rx_position == offsetof(netdata_frame_preamble_t, lrc2)) {  // frame head lrc
            if (m_netdata_frame_rx_buf.pre.lrc2 != compute_lrc((uint8_t *)&m_netdata_frame_rx_buf.pre, offsetof(netdata_frame_preamble_t, lrc2))) {
                // frame head lrc error
                NRF_LOG_ERROR("Data frame head lrc error.");
                data_frame_reset();
                return;
            }
            // frame head complete, cache info
            m_data_cmd = U16NTOHS(m_netdata_frame_rx_buf.pre.cmd);
            m_data_status = U16NTOHS(m_netdata_frame_rx_buf.pre.status);
            m_data_len = U16NTOHS(m_netdata_frame_rx_buf.pre.len);
            NRF_LOG_INFO("Data frame data length %d.", m_data_len);
            // check data length
            if (m_data_len > NETDATA_MAX_DATA_LENGTH) {
                NRF_LOG_ERROR("Data frame data length larger than max.");
                data_frame_reset();
                return;
            }
        } else if (m_data_rx_position >= offsetof(netdata_frame_raw_t, data)) {   // frame data
            // check all data ready.
            if (m_data_rx_position == (sizeof(netdata_frame_preamble_t) + m_data_len)) {
                netdata_frame_postamble_t *rx_post = (netdata_frame_postamble_t *)((uint8_t *)&m_netdata_frame_rx_buf + sizeof(netdata_frame_preamble_t) + m_data_len);
                if (rx_post->lrc3 == compute_lrc((uint8_t *)&m_netdata_frame_rx_buf.data, m_data_len)) {
                    // ok, lrc for data is check success.
                    // and we are receive completed
                    m_data_buffer = m_data_len > 0 ? (uint8_t *)&m_netdata_frame_rx_buf.data : NULL;
                    m_data_completed = true;
                    // NRF_LOG_INFO("RX Data frame: cmd = 0x%04x (%i), status = 0x%04x, length = %d%s", m_data_cmd, m_data_cmd, m_data_status, m_data_len, m_data_len > 0 ? ", data =" : "");
                    // if (m_data_len > 0) {
                    //     NRF_LOG_HEXDUMP_INFO(m_data_buffer, m_data_len);
                    // }
                } else {
                    // data frame lrc error
                    NRF_LOG_ERROR("Data frame finally lrc error.");
                    data_frame_reset();
                }
                return;
            }
        }
        // index update
        m_data_rx_position++;
    }
}

void data_frame_receive_from(uint8_t *data, uint16_t length, data_frame_origin_t origin) {
    if (length == 0) {
        return;
    }
    bool acquired = false;
    CRITICAL_REGION_ENTER();
    if (!m_receiving && !m_dispatching && !m_data_completed) {
        m_receiving = true;
        if (m_data_rx_position == 0) {
            m_data_origin = origin;
        }
        acquired = true;
    }
    CRITICAL_REGION_EXIT();
    if (!acquired) {
        return;
    }
    // USB runs in main context; BLE may interrupt it. Drop overlapping ingress,
    // rather than mask interrupts while copying/checksumming up to 4096 bytes.
    receive_owned(data, length, origin);
    CRITICAL_REGION_ENTER();
    if (m_reset_pending) {
        reset_locked();
    }
    m_receiving = false;
    CRITICAL_REGION_EXIT();
}

/**
 * @brief After the data packet processing, when the received data forms a complete frame,
 *         This function will be distributed processing tasks through this function, which will be adjusted to notify the data processing of the data
 * If the data processing is time -consuming operation, you need to put this function in the main loop to call
 */
void data_frame_process(void) {
    data_frame_cbk_t callback = NULL;
    uint16_t cmd = 0, status = 0, length = 0;
    uint8_t *buffer = NULL;
    bool acquired = false;
    CRITICAL_REGION_ENTER();
    if (!m_dispatching && !m_receiving && m_data_completed) {
        m_dispatching = true;
        m_dispatch_origin = m_data_origin;
        callback = m_frame_process_cbk;
        cmd = m_data_cmd;
        status = m_data_status;
        length = m_data_len;
        buffer = m_data_buffer;
        acquired = true;
    }
    CRITICAL_REGION_EXIT();
    if (!acquired) {
        return;
    }
    if (callback != NULL) {
        callback(cmd, status, length, buffer);
    }
    CRITICAL_REGION_ENTER();
    m_dispatch_origin = DATA_FRAME_ORIGIN_UNKNOWN;
    reset_locked();
    m_dispatching = false;
    CRITICAL_REGION_EXIT();
}

/**
 * @brief Package processing registration registration
 */
void on_data_frame_complete(data_frame_cbk_t callback) {
    m_frame_process_cbk = callback;
}
