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
#if NRF_MODULE_ENABLED(BLE_LBS_C)

#include "ble_lbs_c.h"
#include "ble_db_discovery.h"
#include "ble_types.h"
#include "ble_gattc.h"
#define NRF_LOG_MODULE_NAME ble_lbs_c
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
    ble_lbs_c_t * p_ble_lbs_c = (ble_lbs_c_t *)p_ctx;

    NRF_LOG_DEBUG("A GATT Client error has occurred on conn_handle: 0X%X", conn_handle);

    if (p_ble_lbs_c->error_handler != NULL)
    {
        p_ble_lbs_c->error_handler(nrf_error);
    }
}


/**@brief Function for handling Handle Value Notification received from the SoftDevice.
 *
 * @details This function uses the Handle Value Notification received from the SoftDevice
 *          and checks whether it is a notification of Button state from the peer. If
 *          it is, this function decodes the state of the button and sends it to the
 *          application.
 *
 * @param[in] p_ble_lbs_c Pointer to the Led Button Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_hvx(ble_lbs_c_t * p_ble_lbs_c, ble_evt_t const * p_ble_evt)
{
    // Check if the event is on the link for this instance.
    if (p_ble_lbs_c->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        return;
    }
    // Check if this is a Button notification.
    if (p_ble_evt->evt.gattc_evt.params.hvx.handle == p_ble_lbs_c->peer_lbs_db.button_handle)
    {
        if (p_ble_evt->evt.gattc_evt.params.hvx.len == 1)
        {
            ble_lbs_c_evt_t ble_lbs_c_evt;

            ble_lbs_c_evt.evt_type                   = BLE_LBS_C_EVT_BUTTON_NOTIFICATION;
            ble_lbs_c_evt.conn_handle                = p_ble_lbs_c->conn_handle;
            ble_lbs_c_evt.params.button.button_state = p_ble_evt->evt.gattc_evt.params.hvx.data[0];
            p_ble_lbs_c->evt_handler(p_ble_lbs_c, &ble_lbs_c_evt);
        }
    }
}


/**@brief Function for handling the Disconnected event received from the SoftDevice.
 *
 * @details This function checks whether the disconnect event is happening on the link
 *          associated with the current instance of the module. If the event is happening, the function sets the instance's
 *          conn_handle to invalid.
 *
 * @param[in] p_ble_lbs_c Pointer to the Led Button Client structure.
 * @param[in] p_ble_evt   Pointer to the BLE event received.
 */
static void on_disconnected(ble_lbs_c_t * p_ble_lbs_c, ble_evt_t const * p_ble_evt)
{
    if (p_ble_lbs_c->conn_handle == p_ble_evt->evt.gap_evt.conn_handle)
    {
        p_ble_lbs_c->conn_handle                    = BLE_CONN_HANDLE_INVALID;
        p_ble_lbs_c->peer_lbs_db.button_cccd_handle = BLE_GATT_HANDLE_INVALID;
        p_ble_lbs_c->peer_lbs_db.button_handle      = BLE_GATT_HANDLE_INVALID;
        p_ble_lbs_c->peer_lbs_db.led_handle         = BLE_GATT_HANDLE_INVALID;
    }
}


void ble_lbs_on_db_disc_evt(ble_lbs_c_t * p_ble_lbs_c, ble_db_discovery_evt_t const * p_evt)
{
    // Check if the LED Button Service was discovered.
    if (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE &&
        p_evt->params.discovered_db.srv_uuid.uuid == LBS_UUID_SERVICE &&
        p_evt->params.discovered_db.srv_uuid.type == p_ble_lbs_c->uuid_type)
    {
        ble_lbs_c_evt_t evt;

        evt.evt_type    = BLE_LBS_C_EVT_DISCOVERY_COMPLETE;
        evt.conn_handle = p_evt->conn_handle;

        for (uint32_t i = 0; i < p_evt->params.discovered_db.char_count; i++)
        {
            const ble_gatt_db_char_t * p_char = &(p_evt->params.discovered_db.charateristics[i]);
            switch (p_char->characteristic.uuid.uuid)
            {
                case LBS_UUID_LED_CHAR:
                    evt.params.peer_db.led_handle = p_char->characteristic.handle_value;
                    break;
                case LBS_UUID_BUTTON_CHAR:
                    evt.params.peer_db.button_handle      = p_char->characteristic.handle_value;
                    evt.params.peer_db.button_cccd_handle = p_char->cccd_handle;
                    break;

                default:
                    break;
            }
        }

        NRF_LOG_DEBUG("LED Button Service discovered at peer.");
        //If the instance was assigned prior to db_discovery, assign the db_handles
        if (p_ble_lbs_c->conn_handle != BLE_CONN_HANDLE_INVALID)
        {
            if ((p_ble_lbs_c->peer_lbs_db.led_handle         == BLE_GATT_HANDLE_INVALID)&&
                (p_ble_lbs_c->peer_lbs_db.button_handle      == BLE_GATT_HANDLE_INVALID)&&
                (p_ble_lbs_c->peer_lbs_db.button_cccd_handle == BLE_GATT_HANDLE_INVALID))
            {
                p_ble_lbs_c->peer_lbs_db = evt.params.peer_db;
            }
        }

        p_ble_lbs_c->evt_handler(p_ble_lbs_c, &evt);

    }
}


uint32_t ble_lbs_c_init(ble_lbs_c_t * p_ble_lbs_c, ble_lbs_c_init_t * p_ble_lbs_c_init)
{
    uint32_t      err_code;
    ble_uuid_t    lbs_uuid;
    ble_uuid128_t lbs_base_uuid = {LBS_UUID_BASE};

    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c);
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c_init);
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c_init->evt_handler);
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c_init->p_gatt_queue);

    p_ble_lbs_c->peer_lbs_db.button_cccd_handle = BLE_GATT_HANDLE_INVALID;
    p_ble_lbs_c->peer_lbs_db.button_handle      = BLE_GATT_HANDLE_INVALID;
    p_ble_lbs_c->peer_lbs_db.led_handle         = BLE_GATT_HANDLE_INVALID;
    p_ble_lbs_c->conn_handle                    = BLE_CONN_HANDLE_INVALID;
    p_ble_lbs_c->evt_handler                    = p_ble_lbs_c_init->evt_handler;
    p_ble_lbs_c->p_gatt_queue                   = p_ble_lbs_c_init->p_gatt_queue;
    p_ble_lbs_c->error_handler                  = p_ble_lbs_c_init->error_handler;

    err_code = sd_ble_uuid_vs_add(&lbs_base_uuid, &p_ble_lbs_c->uuid_type);
    if (err_code != NRF_SUCCESS)
    {
        return err_code;
    }
    VERIFY_SUCCESS(err_code);

    lbs_uuid.type = p_ble_lbs_c->uuid_type;
    lbs_uuid.uuid = LBS_UUID_SERVICE;

    return ble_db_discovery_evt_register(&lbs_uuid);
}

void ble_lbs_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    if ((p_context == NULL) || (p_ble_evt == NULL))
    {
        return;
    }

    ble_lbs_c_t * p_ble_lbs_c = (ble_lbs_c_t *)p_context;

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTC_EVT_HVX:
            on_hvx(p_ble_lbs_c, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnected(p_ble_lbs_c, p_ble_evt);
            break;

        default:
            break;
    }
}


/**@brief Function for configuring the CCCD.
 *
 * @param[in] p_ble_lbs_c Pointer to the LED Button Client structure.
 * @param[in] enable      Whether to enable or disable the CCCD.
 *
 * @return NRF_SUCCESS if the CCCD configure was successfully sent to the peer.
 */
static uint32_t cccd_configure(ble_lbs_c_t * p_ble_lbs_c, bool enable)
{
    NRF_LOG_DEBUG("Configuring CCCD. CCCD Handle = %d, Connection Handle = %d",
                  p_ble_lbs_c->peer_lbs_db.button_cccd_handle,
                  p_ble_lbs_c->conn_handle);

    nrf_ble_gq_req_t cccd_req;
    uint16_t         cccd_val = enable ? BLE_GATT_HVX_NOTIFICATION : 0;
    uint8_t          cccd[WRITE_MESSAGE_LENGTH];

    cccd[0] = LSB_16(cccd_val);
    cccd[1] = MSB_16(cccd_val);

    cccd_req.type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    cccd_req.error_handler.cb            = gatt_error_handler;
    cccd_req.error_handler.p_ctx         = p_ble_lbs_c;
    cccd_req.params.gattc_write.handle   = p_ble_lbs_c->peer_lbs_db.button_cccd_handle;
    cccd_req.params.gattc_write.len      = WRITE_MESSAGE_LENGTH;
    cccd_req.params.gattc_write.offset   = 0;
    cccd_req.params.gattc_write.p_value  = cccd;
    cccd_req.params.gattc_write.write_op = BLE_GATT_OP_WRITE_REQ;

    return nrf_ble_gq_item_add(p_ble_lbs_c->p_gatt_queue, &cccd_req, p_ble_lbs_c->conn_handle);
}


uint32_t ble_lbs_c_button_notif_enable(ble_lbs_c_t * p_ble_lbs_c)
{
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c);

    if (p_ble_lbs_c->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    return cccd_configure(p_ble_lbs_c,
                          true);
}


uint32_t ble_lbs_led_status_send(ble_lbs_c_t * p_ble_lbs_c, uint8_t status)
{
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c);

    if (p_ble_lbs_c->conn_handle == BLE_CONN_HANDLE_INVALID)
    {
        return NRF_ERROR_INVALID_STATE;
    }

    NRF_LOG_DEBUG("Writing LED status 0x%x", status);

    nrf_ble_gq_req_t write_req;

    memset(&write_req, 0, sizeof(nrf_ble_gq_req_t));

    write_req.type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    write_req.error_handler.cb            = gatt_error_handler;
    write_req.error_handler.p_ctx         = p_ble_lbs_c;
    write_req.params.gattc_write.handle   = p_ble_lbs_c->peer_lbs_db.led_handle;
    write_req.params.gattc_write.len      = sizeof(status);
    write_req.params.gattc_write.p_value  = &status;
    write_req.params.gattc_write.offset   = 0;
    write_req.params.gattc_write.write_op = BLE_GATT_OP_WRITE_CMD; 

    return nrf_ble_gq_item_add(p_ble_lbs_c->p_gatt_queue, &write_req, p_ble_lbs_c->conn_handle);
}

uint32_t ble_lbs_c_handles_assign(ble_lbs_c_t    * p_ble_lbs_c,
                                  uint16_t         conn_handle,
                                  const lbs_db_t * p_peer_handles)
{
    VERIFY_PARAM_NOT_NULL(p_ble_lbs_c);

    p_ble_lbs_c->conn_handle = conn_handle;
    if (p_peer_handles != NULL)
    {
        p_ble_lbs_c->peer_lbs_db = *p_peer_handles;
    }
    return nrf_ble_gq_conn_handle_register(p_ble_lbs_c->p_gatt_queue, conn_handle);
}

#endif // NRF_MODULE_ENABLED(BLE_LBS_C)
