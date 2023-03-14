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
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(BLE_BAS_C)
#include "ble_bas_c.h"
#include "ble_types.h"
#include "ble_db_discovery.h"
#include "ble_gattc.h"
#define NRF_LOG_MODULE_NAME ble_bas_c
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();


/**@brief Function for intercepting errors of GATTC and BLE GATT Queue.
 *
 * @param[in] nrf_error   Error code.
 * @param[in] p_ctx       Parameter from the event handler.
 * @param[in] conn_handle Connection handle.
 */
static void gatt_error_handler(uint32_t   nrf_error,
                               void     * p_ctx,
                               uint16_t   conn_handle)
{
    ble_bas_c_t * p_bas_c = (ble_bas_c_t *)p_ctx;

    NRF_LOG_DEBUG("A GATT Client error has occurred on conn_handle: 0X%X", conn_handle);

    if (p_bas_c->error_handler != NULL)
    {
        p_bas_c->error_handler(nrf_error);
    }
}


/**@brief     Function for handling read response events.
 *
 * @details   This function validates the read response and raises the appropriate
 *            event to the application.
 *
 * @param[in] p_bas_c   Pointer to the Battery Service Client Structure.
 * @param[in] p_ble_evt Pointer to the SoftDevice event.
 */
static void on_read_rsp(ble_bas_c_t * p_bas_c, ble_evt_t const * p_ble_evt)
{
    const ble_gattc_evt_read_rsp_t * p_response;

    // Check if the event is on the link for this instance.
    if (p_bas_c->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        return;
    }

    p_response = &p_ble_evt->evt.gattc_evt.params.read_rsp;

    if (p_response->handle == p_bas_c->peer_bas_db.bl_handle)
    {
        ble_bas_c_evt_t evt;

        evt.conn_handle = p_ble_evt->evt.gattc_evt.conn_handle;
        evt.evt_type = BLE_BAS_C_EVT_BATT_READ_RESP;

        evt.params.battery_level = p_response->data[0];

        p_bas_c->evt_handler(p_bas_c, &evt);
    }
}


/**@brief     Function for handling Handle Value Notification received from the SoftDevice.
 *
 * @details   This function handles the Handle Value Notification received from the SoftDevice
 *            and checks whether it is a notification of the Battery Level measurement from the peer. If
 *            it is, this function decodes the battery level measurement and sends it to the
 *            application.
 *
 * @param[in] p_ble_bas_c Pointer to the Battery Service Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_hvx(ble_bas_c_t * p_ble_bas_c, ble_evt_t const * p_ble_evt)
{
    // Check if the event is on the link for this instance.
    if (p_ble_bas_c->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        return;
    }
    // Check if this notification is a battery level notification.
    if (p_ble_evt->evt.gattc_evt.params.hvx.handle == p_ble_bas_c->peer_bas_db.bl_handle)
    {
        if (p_ble_evt->evt.gattc_evt.params.hvx.len == 1)
        {
            ble_bas_c_evt_t ble_bas_c_evt;
            ble_bas_c_evt.conn_handle = p_ble_evt->evt.gattc_evt.conn_handle;
            ble_bas_c_evt.evt_type    = BLE_BAS_C_EVT_BATT_NOTIFICATION;

            ble_bas_c_evt.params.battery_level = p_ble_evt->evt.gattc_evt.params.hvx.data[0];

            p_ble_bas_c->evt_handler(p_ble_bas_c, &ble_bas_c_evt);
        }
    }
}


void ble_bas_on_db_disc_evt(ble_bas_c_t * p_ble_bas_c, const ble_db_discovery_evt_t * p_evt)
{
    // Check if the Battery Service was discovered.
    if (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE
        &&
        p_evt->params.discovered_db.srv_uuid.uuid == BLE_UUID_BATTERY_SERVICE
        &&
        p_evt->params.discovered_db.srv_uuid.type == BLE_UUID_TYPE_BLE)
    {
        // Find the CCCD Handle of the Battery Level characteristic.
        uint8_t i;

        ble_bas_c_evt_t evt;
        evt.evt_type    = BLE_BAS_C_EVT_DISCOVERY_COMPLETE;
        evt.conn_handle = p_evt->conn_handle;
        for (i = 0; i < p_evt->params.discovered_db.char_count; i++)
        {
            if (p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid ==
                BLE_UUID_BATTERY_LEVEL_CHAR)
            {
                // Found Battery Level characteristic. Store CCCD handle and break.
                evt.params.bas_db.bl_cccd_handle =
                    p_evt->params.discovered_db.charateristics[i].cccd_handle;
                evt.params.bas_db.bl_handle =
                    p_evt->params.discovered_db.charateristics[i].characteristic.handle_value;
                break;
            }
        }

        NRF_LOG_DEBUG("Battery Service discovered at peer.");

        //If the instance has been assigned prior to db_discovery, assign the db_handles.
        if (p_ble_bas_c->conn_handle != BLE_CONN_HANDLE_INVALID)
        {
            if ((p_ble_bas_c->peer_bas_db.bl_cccd_handle == BLE_GATT_HANDLE_INVALID)&&
                (p_ble_bas_c->peer_bas_db.bl_handle      == BLE_GATT_HANDLE_INVALID))
            {
                p_ble_bas_c->peer_bas_db = evt.params.bas_db;
            }
        }
        p_ble_bas_c->evt_handler(p_ble_bas_c, &evt);
    }
    else if ((p_evt->evt_type == BLE_DB_DISCOVERY_SRV_NOT_FOUND) ||
             (p_evt->evt_type == BLE_DB_DISCOVERY_ERROR))
    {
        NRF_LOG_DEBUG("Battery Service discovery failure at peer. ");
    }
    else
    {
        // Do nothing.
    }
}


/**@brief Function for creating a message for writing to the CCCD.
 */
static uint32_t cccd_configure(ble_bas_c_t * p_ble_bas_c, bool notification_enable)
{
    NRF_LOG_DEBUG("Configuring CCCD. CCCD Handle = %d, Connection Handle = %d",
                  p_ble_bas_c->peer_bas_db.bl_cccd_handle,
                  p_ble_bas_c->conn_handle);

    nrf_ble_gq_req_t bas_c_req;
    uint8_t          cccd[BLE_CCCD_VALUE_LEN];
    uint16_t         cccd_val = notification_enable ? BLE_GATT_HVX_NOTIFICATION : 0;

    cccd[0] = LSB_16(cccd_val);
    cccd[1] = MSB_16(cccd_val);

    memset(&bas_c_req, 0, sizeof(bas_c_req));
 
    bas_c_req.type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    bas_c_req.error_handler.cb            = gatt_error_handler;
    bas_c_req.error_handler.p_ctx         = p_ble_bas_c;
    bas_c_req.params.gattc_write.handle   = p_ble_bas_c->peer_bas_db.bl_cccd_handle;
    bas_c_req.params.gattc_write.len      = BLE_CCCD_VALUE_LEN;
    bas_c_req.params.gattc_write.p_value  = cccd;
    bas_c_req.params.gattc_write.offset   = 0;
    bas_c_req.params.gattc_write.write_op = BLE_GATT_OP_WRITE_REQ;

    return nrf_ble_gq_item_add(p_ble_bas_c->p_gatt_queue, &bas_c_req, p_ble_bas_c->conn_handle);
}


uint32_t ble_bas_c_init(ble_bas_c_t * p_ble_bas_c, ble_bas_c_init_t * p_ble_bas_c_init)
{
    VERIFY_PARAM_NOT_NULL(p_ble_bas_c);
    VERIFY_PARAM_NOT_NULL(p_ble_bas_c_init);

    ble_uuid_t bas_uuid;

    bas_uuid.type                = BLE_UUID_TYPE_BLE;
    bas_uuid.uuid                = BLE_UUID_BATTERY_SERVICE;

    p_ble_bas_c->conn_handle                = BLE_CONN_HANDLE_INVALID;
    p_ble_bas_c->peer_bas_db.bl_cccd_handle = BLE_GATT_HANDLE_INVALID;
    p_ble_bas_c->peer_bas_db.bl_handle      = BLE_GATT_HANDLE_INVALID;
    p_ble_bas_c->evt_handler                = p_ble_bas_c_init->evt_handler;
    p_ble_bas_c->error_handler              = p_ble_bas_c_init->error_handler;
    p_ble_bas_c->p_gatt_queue               = p_ble_bas_c_init->p_gatt_queue;

    return ble_db_discovery_evt_register(&bas_uuid);
}


/**@brief     Function for handling the Disconnected event received from the SoftDevice.
 *
 * @details   This function checks whether the disconnect event is happening on the link
 *            associated with the current instance of the module. If the event is happening,
 *            the function sets the instance's conn_handle to invalid.
 *
 * @param[in] p_ble_bas_c Pointer to the Battery Service Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_disconnected(ble_bas_c_t * p_ble_bas_c, const ble_evt_t * p_ble_evt)
{
    if (p_ble_bas_c->conn_handle == p_ble_evt->evt.gap_evt.conn_handle)
    {
        p_ble_bas_c->conn_handle                = BLE_CONN_HANDLE_INVALID;
        p_ble_bas_c->peer_bas_db.bl_cccd_handle = BLE_GATT_HANDLE_INVALID;
        p_ble_bas_c->peer_bas_db.bl_handle      = BLE_GATT_HANDLE_INVALID;
    }
}


void ble_bas_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    if ((p_ble_evt == NULL) || (p_context == NULL))
    {
        return;
    }

    ble_bas_c_t * p_ble_bas_c = (ble_bas_c_t *)p_context;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTC_EVT_HVX:
            on_hvx(p_ble_bas_c, p_ble_evt);
            break;

        case BLE_GATTC_EVT_READ_RSP:
            on_read_rsp(p_ble_bas_c, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnected(p_ble_bas_c, p_ble_evt);
            break;

        default:
            break;
    }
}


uint32_t ble_bas_c_bl_notif_enable(ble_bas_c_t * p_ble_bas_c)
{
    VERIFY_PARAM_NOT_NULL(p_ble_bas_c);

    if (p_ble_bas_c->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    return cccd_configure(p_ble_bas_c, true);
}


uint32_t ble_bas_c_bl_read(ble_bas_c_t * p_ble_bas_c)
{
    VERIFY_PARAM_NOT_NULL(p_ble_bas_c);
    if (p_ble_bas_c->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    nrf_ble_gq_req_t bas_c_req;

    memset(&bas_c_req, 0, sizeof(bas_c_req));
    bas_c_req.type                     = NRF_BLE_GQ_REQ_GATTC_READ;
    bas_c_req.error_handler.cb         = gatt_error_handler;
    bas_c_req.error_handler.p_ctx      = p_ble_bas_c;
    bas_c_req.params.gattc_read.handle = p_ble_bas_c->peer_bas_db.bl_handle;

    return nrf_ble_gq_item_add(p_ble_bas_c->p_gatt_queue, &bas_c_req, p_ble_bas_c->conn_handle);
}


uint32_t ble_bas_c_handles_assign(ble_bas_c_t    * p_ble_bas_c,
                                  uint16_t         conn_handle,
                                  ble_bas_c_db_t * p_peer_handles)
{
    VERIFY_PARAM_NOT_NULL(p_ble_bas_c);

    p_ble_bas_c->conn_handle = conn_handle;
    if (p_peer_handles != NULL)
    {
        p_ble_bas_c->peer_bas_db = *p_peer_handles;
    }

    return nrf_ble_gq_conn_handle_register(p_ble_bas_c->p_gatt_queue, conn_handle);
}
#endif // NRF_MODULE_ENABLED(BLE_BAS_C)
