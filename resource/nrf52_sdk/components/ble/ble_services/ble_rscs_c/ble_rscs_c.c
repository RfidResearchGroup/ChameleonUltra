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
#if NRF_MODULE_ENABLED(BLE_RSCS_C)
#include "ble_rscs_c.h"
#include "ble_db_discovery.h"
#include "ble_types.h"
#include "ble_gattc.h"

#define NRF_LOG_MODULE_NAME ble_rscs_c
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#define WRITE_MESSAGE_LENGTH   BLE_CCCD_VALUE_LEN    /**< Length of the write message for CCCD. */


/**@brief Function for intercepting the errors of GATTC and the BLE GATT Queue.
 *
 * @param[in] nrf_error   Error code.
 * @param[in] p_ctx       Parameter from the event handler.
 * @param[in] conn_handle Connection handle.
 */
static void gatt_error_handler(uint32_t   nrf_error,
                               void     * p_ctx,
                               uint16_t   conn_handle)
{
    ble_rscs_c_t * p_ble_rscs_c = (ble_rscs_c_t *)p_ctx;

    NRF_LOG_DEBUG("A GATT Client error has occurred on conn_handle: 0X%X", conn_handle);

    if (p_ble_rscs_c->error_handler != NULL)
    {
        p_ble_rscs_c->error_handler(nrf_error);
    }
}


/**@brief     Function for handling Handle Value Notification received from the SoftDevice.
 *
 * @details   This function uses the Handle Value Notification received from the SoftDevice
 *            and checks whether it is a notification of the Running Speed and Cadence measurement from
 *            the peer. If it is, this function decodes the Running Speed measurement and sends it
 *            to the application.
 *
 * @param[in] p_ble_rscs_c Pointer to the Running Speed and Cadence Client structure.
 * @param[in] p_ble_evt    Pointer to the BLE event received.
 */
static void on_hvx(ble_rscs_c_t * p_ble_rscs_c, const ble_evt_t * p_ble_evt)
{
    const ble_gattc_evt_hvx_t * p_notif = &p_ble_evt->evt.gattc_evt.params.hvx;

    // Check if the event is on the link for this instance
    if (p_ble_rscs_c->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        return;
    }

    // Check if this is a Running Speed and Cadence notification.
    if (p_ble_evt->evt.gattc_evt.params.hvx.handle == p_ble_rscs_c->peer_db.rsc_handle)
    {
        uint32_t         index = 0;
        ble_rscs_c_evt_t ble_rscs_c_evt;
        ble_rscs_c_evt.evt_type    = BLE_RSCS_C_EVT_RSC_NOTIFICATION;
        ble_rscs_c_evt.conn_handle = p_ble_evt->evt.gattc_evt.conn_handle;

        //lint -save -e415 -e416 -e662 "Access of out of bounds pointer" "Creation of out of bounds pointer"

        // Flags field
        ble_rscs_c_evt.params.rsc.is_inst_stride_len_present = p_notif->data[index] >> BLE_RSCS_INSTANT_STRIDE_LEN_PRESENT    & 0x01;
        ble_rscs_c_evt.params.rsc.is_total_distance_present  = p_notif->data[index] >> BLE_RSCS_TOTAL_DISTANCE_PRESENT        & 0x01;
        ble_rscs_c_evt.params.rsc.is_running                 = p_notif->data[index] >> BLE_RSCS_WALKING_OR_RUNNING_STATUS_BIT & 0x01;
        index++;

        // Instantaneous speed
        ble_rscs_c_evt.params.rsc.inst_speed = uint16_decode(&p_notif->data[index]);
        index += sizeof(uint16_t);

        // Instantaneous cadence
        ble_rscs_c_evt.params.rsc.inst_cadence = p_notif->data[index];
        index++;

        // Instantaneous stride length
        if (ble_rscs_c_evt.params.rsc.is_inst_stride_len_present == true)
        {
            ble_rscs_c_evt.params.rsc.inst_stride_length = uint16_decode(&p_notif->data[index]);
            index += sizeof(uint16_t);
        }

        // Total distance field
        if (ble_rscs_c_evt.params.rsc.is_total_distance_present == true)
        {
            ble_rscs_c_evt.params.rsc.total_distance = uint32_decode(&p_notif->data[index]);
            //index += sizeof(uint32_t);
        }

        p_ble_rscs_c->evt_handler(p_ble_rscs_c, &ble_rscs_c_evt);

        //lint -restore
    }
}


/**@brief     Function for handling events from the Database Discovery module.
 *
 * @details   This function handles an event from the Database Discovery module, and determines
 *            whether it relates to the discovery of Running Speed and Cadence service at the peer. If it does, the function
 *            calls the application's event handler to indicate that the Running Speed and Cadence
 *            service was discovered at the peer. The function also populates the event with service-related
 * 			  information before providing it to the application.
 *
 * @param[in] p_evt Pointer to the event received from the Database Discovery module.
 *
 */
void ble_rscs_on_db_disc_evt(ble_rscs_c_t * p_ble_rscs_c, const ble_db_discovery_evt_t * p_evt)
{
    // Check if the Heart Rate Service was discovered.
    if (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE &&
        p_evt->params.discovered_db.srv_uuid.uuid == BLE_UUID_RUNNING_SPEED_AND_CADENCE &&
        p_evt->params.discovered_db.srv_uuid.type == BLE_UUID_TYPE_BLE)
    {
        ble_rscs_c_evt_t evt;
        evt.conn_handle = p_evt->conn_handle;

        // Find the CCCD Handle of the Running Speed and Cadence characteristic.
        for (uint32_t i = 0; i < p_evt->params.discovered_db.char_count; i++)
        {
            if (p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid ==
                BLE_UUID_RSC_MEASUREMENT_CHAR)
            {
                // Found Running Speed and Cadence characteristic. Store CCCD handle and break.
                evt.params.rscs_db.rsc_cccd_handle =
                    p_evt->params.discovered_db.charateristics[i].cccd_handle;
                evt.params.rscs_db.rsc_handle      =
                    p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
                break;
            }
        }

        NRF_LOG_DEBUG("Running Speed and Cadence Service discovered at peer.");

        // If the instance has been assigned prior to db_discovery, assign the db_handles.
        if (p_ble_rscs_c->conn_handle != BLE_CONN_HANDLE_INVALID)
        {
            if ((p_ble_rscs_c->peer_db.rsc_cccd_handle == BLE_GATT_HANDLE_INVALID)&&
                (p_ble_rscs_c->peer_db.rsc_handle == BLE_GATT_HANDLE_INVALID))
            {
                p_ble_rscs_c->peer_db = evt.params.rscs_db;
            }
        }

        evt.evt_type = BLE_RSCS_C_EVT_DISCOVERY_COMPLETE;

        p_ble_rscs_c->evt_handler(p_ble_rscs_c, &evt);
    }
}


uint32_t ble_rscs_c_init(ble_rscs_c_t * p_ble_rscs_c, ble_rscs_c_init_t * p_ble_rscs_c_init)
{
    VERIFY_PARAM_NOT_NULL(p_ble_rscs_c);
    VERIFY_PARAM_NOT_NULL(p_ble_rscs_c_init);

    ble_uuid_t rscs_uuid;

    rscs_uuid.type = BLE_UUID_TYPE_BLE;
    rscs_uuid.uuid = BLE_UUID_RUNNING_SPEED_AND_CADENCE;

    p_ble_rscs_c->evt_handler             = p_ble_rscs_c_init->evt_handler;
    p_ble_rscs_c->error_handler           = p_ble_rscs_c_init->error_handler;
    p_ble_rscs_c->conn_handle             = BLE_CONN_HANDLE_INVALID;
    p_ble_rscs_c->peer_db.rsc_cccd_handle = BLE_GATT_HANDLE_INVALID;
    p_ble_rscs_c->peer_db.rsc_handle      = BLE_GATT_HANDLE_INVALID;
    p_ble_rscs_c->p_gatt_queue            = p_ble_rscs_c_init->p_gatt_queue;

    return ble_db_discovery_evt_register(&rscs_uuid);
}


uint32_t ble_rscs_c_handles_assign(ble_rscs_c_t *    p_ble_rscs_c,
                                   uint16_t          conn_handle,
                                   ble_rscs_c_db_t * p_peer_handles)
{
    VERIFY_PARAM_NOT_NULL(p_ble_rscs_c);
    p_ble_rscs_c->conn_handle = conn_handle;
    if (p_peer_handles != NULL)
    {
        p_ble_rscs_c->peer_db = *p_peer_handles;
    }

    return nrf_ble_gq_conn_handle_register(p_ble_rscs_c->p_gatt_queue, conn_handle);
}


/**@brief     Function for handling Disconnected event received from the SoftDevice.
 *
 * @details   This function check whether the disconnect event is happening on the link
 *            associated with the current instance of the module. If the event is happening, the function sets the instance's
 *            conn_handle to invalid.
 *
 * @param[in] p_ble_rscs_c Pointer to the RSC Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_disconnected(ble_rscs_c_t * p_ble_rscs_c, const ble_evt_t * p_ble_evt)
{
    if (p_ble_rscs_c->conn_handle == p_ble_evt->evt.gap_evt.conn_handle)
    {
        p_ble_rscs_c->conn_handle             = BLE_CONN_HANDLE_INVALID;
        p_ble_rscs_c->peer_db.rsc_cccd_handle = BLE_GATT_HANDLE_INVALID;
        p_ble_rscs_c->peer_db.rsc_handle      = BLE_GATT_HANDLE_INVALID;
    }
}


void ble_rscs_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    if ((p_context == NULL) || (p_ble_evt == NULL))
    {
        return;
    }

    ble_rscs_c_t * p_ble_rscs_c = (ble_rscs_c_t *)p_context;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTC_EVT_HVX:
            on_hvx(p_ble_rscs_c, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnected(p_ble_rscs_c, p_ble_evt);
            break;

        default:
            break;
    }
}


/**@brief Function for creating a message for writing to the CCCD.
 */
static uint32_t cccd_configure(ble_rscs_c_t * p_ble_rscs_c, bool enable)
{
    NRF_LOG_DEBUG("Configuring CCCD. CCCD Handle = %d, Connection Handle = %d",
                  p_ble_rscs_c->peer_db.rsc_cccd_handle,
                  p_ble_rscs_c->conn_handle);

    uint8_t          cccd[WRITE_MESSAGE_LENGTH];
    uint16_t         cccd_val = enable ? BLE_GATT_HVX_NOTIFICATION : 0;
    nrf_ble_gq_req_t rscs_c_req;

    cccd[0] = LSB_16(cccd_val);
    cccd[1] = MSB_16(cccd_val);

    memset(&rscs_c_req, 0, sizeof(rscs_c_req));
    rscs_c_req.type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    rscs_c_req.error_handler.cb            = gatt_error_handler;
    rscs_c_req.error_handler.p_ctx         = p_ble_rscs_c;
    rscs_c_req.params.gattc_write.handle   = p_ble_rscs_c->peer_db.rsc_cccd_handle;
    rscs_c_req.params.gattc_write.len      = WRITE_MESSAGE_LENGTH;
    rscs_c_req.params.gattc_write.p_value  = cccd;
    rscs_c_req.params.gattc_write.offset   = 0;
    rscs_c_req.params.gattc_write.write_op = BLE_GATT_OP_WRITE_REQ;

    return nrf_ble_gq_item_add(p_ble_rscs_c->p_gatt_queue, &rscs_c_req, p_ble_rscs_c->conn_handle);
}


uint32_t ble_rscs_c_rsc_notif_enable(ble_rscs_c_t * p_ble_rscs_c)
{
    VERIFY_PARAM_NOT_NULL(p_ble_rscs_c);

    if (p_ble_rscs_c->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    return cccd_configure(p_ble_rscs_c, true);
}

/** @}
 *  @endcond
 */
#endif // NRF_MODULE_ENABLED(BLE_RSCS_C)
