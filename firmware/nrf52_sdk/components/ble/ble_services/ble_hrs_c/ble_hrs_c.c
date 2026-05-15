/**
 * Copyright (c) 2012 - 2021, Nordic Semiconductor ASA
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form, except as embedded into a Nordic
 *    Semiconductor ASA integrated circuit in a product or a software update for
 *    such product, must reproduce the above copyright notice, this list of
 *    conditions and the following disclaimer in the documentation and/or other
 *    materials provided with the distribution.
 *
 * 3. Neither the name of Nordic Semiconductor ASA nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * 4. This software, with or without modification, must only be used with a
 *    Nordic Semiconductor ASA integrated circuit.
 *
 * 5. Any software provided in binary form under this license must not be reverse
 *    engineered, decompiled, modified and/or disassembled.
 *
 * THIS SOFTWARE IS PROVIDED BY NORDIC SEMICONDUCTOR ASA "AS IS" AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY, NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL NORDIC SEMICONDUCTOR ASA OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
/**@cond To Make Doxygen skip documentation generation for this file.
 * @{
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(BLE_HRS_C)
#include "ble_hrs_c.h"
#include "ble_db_discovery.h"
#include "ble_types.h"
#include "ble_gattc.h"

#define NRF_LOG_MODULE_NAME ble_hrs_c
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#define HRM_FLAG_MASK_HR_16BIT  (0x01 << 0)           /**< Bit mask used to extract the type of heart rate value. This is used to find if the received heart rate is a 16 bit value or an 8 bit value. */
#define HRM_FLAG_MASK_HR_RR_INT (0x01 << 4)           /**< Bit mask used to extract the presence of RR_INTERVALS. This is used to find if the received measurement includes RR_INTERVALS. */


/**@brief Function for interception of the errors of GATTC and the BLE GATT Queue.
 *
 * @param[in] nrf_error   Error code.
 * @param[in] p_ctx       Parameter from the event handler.
 * @param[in] conn_handle Connection handle.
 */
static void gatt_error_handler(uint32_t   nrf_error,
                               void     * p_ctx,
                               uint16_t   conn_handle)
{
    ble_hrs_c_t * p_ble_hrs_c = (ble_hrs_c_t *)p_ctx;

    NRF_LOG_DEBUG("A GATT Client error has occurred on conn_handle: 0X%X", conn_handle);

    if (p_ble_hrs_c->error_handler != NULL)
    {
        p_ble_hrs_c->error_handler(nrf_error);
    }
}


/**@brief     Function for handling Handle Value Notification received from the SoftDevice.
 *
 * @details   This function uses the Handle Value Notification received from the SoftDevice
 *            and checks whether it is a notification of the heart rate measurement from the peer. If
 *            it is, this function decodes the heart rate measurement and sends it to the
 *            application.
 *
 * @param[in] p_ble_hrs_c Pointer to the Heart Rate Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_hvx(ble_hrs_c_t * p_ble_hrs_c, const ble_evt_t * p_ble_evt)
{
    // Check if the event is on the link for this instance.
    if (p_ble_hrs_c->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        NRF_LOG_DEBUG("Received HVX on link 0x%x, not associated to this instance. Ignore.",
                      p_ble_evt->evt.gattc_evt.conn_handle);
        return;
    }

    NRF_LOG_DEBUG("Received HVX on link 0x%x, hrm_handle 0x%x",
    p_ble_evt->evt.gattc_evt.params.hvx.handle,
    p_ble_hrs_c->peer_hrs_db.hrm_handle);

    // Check if this is a heart rate notification.
    if (p_ble_evt->evt.gattc_evt.params.hvx.handle == p_ble_hrs_c->peer_hrs_db.hrm_handle)
    {
        ble_hrs_c_evt_t ble_hrs_c_evt;
        uint32_t        index = 0;

        ble_hrs_c_evt.evt_type                    = BLE_HRS_C_EVT_HRM_NOTIFICATION;
        ble_hrs_c_evt.conn_handle                 = p_ble_hrs_c->conn_handle;
        ble_hrs_c_evt.params.hrm.rr_intervals_cnt = 0;

        if (!(p_ble_evt->evt.gattc_evt.params.hvx.data[index++] & HRM_FLAG_MASK_HR_16BIT))
        {
            // 8-bit heart rate value received.
            ble_hrs_c_evt.params.hrm.hr_value = p_ble_evt->evt.gattc_evt.params.hvx.data[index++];  //lint !e415 suppress Lint Warning 415: Likely access out of bond
        }
        else
        {
            // 16-bit heart rate value received.
            ble_hrs_c_evt.params.hrm.hr_value =
                uint16_decode(&(p_ble_evt->evt.gattc_evt.params.hvx.data[index]));
            index += sizeof(uint16_t);
        }

        if ((p_ble_evt->evt.gattc_evt.params.hvx.data[0] & HRM_FLAG_MASK_HR_RR_INT))
        {
            uint32_t i;
            /*lint --e{415} --e{416} --e{662} --e{661} -save suppress Warning 415: possible access out of bond */
            for (i = 0; i < BLE_HRS_C_RR_INTERVALS_MAX_CNT; i ++)
            {
                if (index >= p_ble_evt->evt.gattc_evt.params.hvx.len)
                {
                    break;
                }
                ble_hrs_c_evt.params.hrm.rr_intervals[i] =
                    uint16_decode(&(p_ble_evt->evt.gattc_evt.params.hvx.data[index]));
                index += sizeof(uint16_t);
            }
            /*lint -restore*/
            ble_hrs_c_evt.params.hrm.rr_intervals_cnt = (uint8_t)i;
        }
        p_ble_hrs_c->evt_handler(p_ble_hrs_c, &ble_hrs_c_evt);
    }
}


/**@brief     Function for handling Disconnected event received from the SoftDevice.
 *
 * @details   This function checks whether the disconnect event is happening on the link
 *            associated with the current instance of the module. If the event is happening, the function sets the instance's
 *            conn_handle to invalid.
 *
 * @param[in] p_ble_hrs_c Pointer to the Heart Rate Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_disconnected(ble_hrs_c_t * p_ble_hrs_c, const ble_evt_t * p_ble_evt)
{
    if (p_ble_hrs_c->conn_handle == p_ble_evt->evt.gap_evt.conn_handle)
    {
        p_ble_hrs_c->conn_handle                 = BLE_CONN_HANDLE_INVALID;
        p_ble_hrs_c->peer_hrs_db.hrm_cccd_handle = BLE_GATT_HANDLE_INVALID;
        p_ble_hrs_c->peer_hrs_db.hrm_handle      = BLE_GATT_HANDLE_INVALID;
    }
}


void ble_hrs_on_db_disc_evt(ble_hrs_c_t * p_ble_hrs_c, const ble_db_discovery_evt_t * p_evt)
{
    // Check if the Heart Rate Service was discovered.
    if (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE &&
        p_evt->params.discovered_db.srv_uuid.uuid == BLE_UUID_HEART_RATE_SERVICE &&
        p_evt->params.discovered_db.srv_uuid.type == BLE_UUID_TYPE_BLE)
    {
        // Find the CCCD Handle of the Heart Rate Measurement characteristic.
        uint32_t i;

        ble_hrs_c_evt_t evt;

        evt.evt_type    = BLE_HRS_C_EVT_DISCOVERY_COMPLETE;
        evt.conn_handle = p_evt->conn_handle;

        for (i = 0; i < p_evt->params.discovered_db.char_count; i++)
        {
            if (p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid ==
                BLE_UUID_HEART_RATE_MEASUREMENT_CHAR)
            {
                // Found Heart Rate characteristic. Store CCCD handle and break.
                evt.params.peer_db.hrm_cccd_handle =
                    p_evt->params.discovered_db.charateristics[i].cccd_handle;
                evt.params.peer_db.hrm_handle =
                    p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
                break;
            }
        }

        NRF_LOG_DEBUG("Heart Rate Service discovered at peer.");
        //If the instance has been assigned prior to db_discovery, assign the db_handles.
        if (p_ble_hrs_c->conn_handle != BLE_CONN_HANDLE_INVALID)
        {
            if ((p_ble_hrs_c->peer_hrs_db.hrm_cccd_handle == BLE_GATT_HANDLE_INVALID)&&
                (p_ble_hrs_c->peer_hrs_db.hrm_handle == BLE_GATT_HANDLE_INVALID))
            {
                p_ble_hrs_c->peer_hrs_db = evt.params.peer_db;
            }
        }


        p_ble_hrs_c->evt_handler(p_ble_hrs_c, &evt);
    }
}


uint32_t ble_hrs_c_init(ble_hrs_c_t * p_ble_hrs_c, ble_hrs_c_init_t * p_ble_hrs_c_init)
{
    VERIFY_PARAM_NOT_NULL(p_ble_hrs_c);
    VERIFY_PARAM_NOT_NULL(p_ble_hrs_c_init);

    ble_uuid_t hrs_uuid;

    hrs_uuid.type = BLE_UUID_TYPE_BLE;
    hrs_uuid.uuid = BLE_UUID_HEART_RATE_SERVICE;

    p_ble_hrs_c->evt_handler                 = p_ble_hrs_c_init->evt_handler;
    p_ble_hrs_c->error_handler               = p_ble_hrs_c_init->error_handler;
    p_ble_hrs_c->p_gatt_queue                = p_ble_hrs_c_init->p_gatt_queue;
    p_ble_hrs_c->conn_handle                 = BLE_CONN_HANDLE_INVALID;
    p_ble_hrs_c->peer_hrs_db.hrm_cccd_handle = BLE_GATT_HANDLE_INVALID;
    p_ble_hrs_c->peer_hrs_db.hrm_handle      = BLE_GATT_HANDLE_INVALID;

    return ble_db_discovery_evt_register(&hrs_uuid);
}

void ble_hrs_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    ble_hrs_c_t * p_ble_hrs_c = (ble_hrs_c_t *)p_context;

    if ((p_ble_hrs_c == NULL) || (p_ble_evt == NULL))
    {
        return;
    }

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTC_EVT_HVX:
            on_hvx(p_ble_hrs_c, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnected(p_ble_hrs_c, p_ble_evt);
            break;

        default:
            break;
    }
}


/**@brief Function for creating a message for writing to the CCCD.
 */
static uint32_t cccd_configure(ble_hrs_c_t * p_ble_hrs_c, bool enable)
{
    NRF_LOG_DEBUG("Configuring CCCD. CCCD Handle = %d, Connection Handle = %d",
                  p_ble_hrs_c->peer_hrs_db.hrm_cccd_handle,
                  p_ble_hrs_c->conn_handle);

    nrf_ble_gq_req_t hrs_c_req;
    uint8_t          cccd[BLE_CCCD_VALUE_LEN];
    uint16_t         cccd_val = enable ? BLE_GATT_HVX_NOTIFICATION : 0;

    cccd[0] = LSB_16(cccd_val);
    cccd[1] = MSB_16(cccd_val);

    memset(&hrs_c_req, 0, sizeof(hrs_c_req));

    hrs_c_req.type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    hrs_c_req.error_handler.cb            = gatt_error_handler;
    hrs_c_req.error_handler.p_ctx         = p_ble_hrs_c;
    hrs_c_req.params.gattc_write.handle   = p_ble_hrs_c->peer_hrs_db.hrm_cccd_handle;
    hrs_c_req.params.gattc_write.len      = BLE_CCCD_VALUE_LEN;
    hrs_c_req.params.gattc_write.p_value  = cccd;
    hrs_c_req.params.gattc_write.write_op = BLE_GATT_OP_WRITE_REQ;

    return nrf_ble_gq_item_add(p_ble_hrs_c->p_gatt_queue, &hrs_c_req, p_ble_hrs_c->conn_handle);
}


uint32_t ble_hrs_c_hrm_notif_enable(ble_hrs_c_t * p_ble_hrs_c)
{
    VERIFY_PARAM_NOT_NULL(p_ble_hrs_c);

    return cccd_configure(p_ble_hrs_c, true);
}


uint32_t ble_hrs_c_handles_assign(ble_hrs_c_t    * p_ble_hrs_c,
                                  uint16_t         conn_handle,
                                  const hrs_db_t * p_peer_hrs_handles)
{
    VERIFY_PARAM_NOT_NULL(p_ble_hrs_c);

    p_ble_hrs_c->conn_handle = conn_handle;
    if (p_peer_hrs_handles != NULL)
    {
        p_ble_hrs_c->peer_hrs_db = *p_peer_hrs_handles;
    }

    return nrf_ble_gq_conn_handle_register(p_ble_hrs_c->p_gatt_queue, conn_handle);
}
/** @}
 *  @endcond
 */
#endif // NRF_MODULE_ENABLED(BLE_HRS_C)
