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
#if NRF_MODULE_ENABLED(BLE_CTS_C)
#include <string.h>
#include "ble.h"
#include "ble_srv_common.h"
#include "ble_gattc.h"
#include "ble_cts_c.h"
#include "ble_date_time.h"
#include "ble_db_discovery.h"
#define NRF_LOG_MODULE_NAME ble_cts_c
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

#define CTS_YEAR_MIN 1582                     /**< The lowest valid Current Time year is the year when the Western calendar was introduced. */
#define CTS_YEAR_MAX 9999                     /**< The highest possible Current Time year. */

#define CTS_C_CURRENT_TIME_EXPECTED_LENGTH 10 /**< |     Year        |Month   |Day     |Hours   |Minutes |Seconds |Weekday |Fraction|Reason  |
                                                   |     2 bytes     |1 byte  |1 byte  |1 byte  |1 byte  |1 byte  |1 byte  |1 byte  |1 byte  | = 10 bytes. */


/**@brief Function for intercepting errors of GATTC and the BLE GATT Queue.
 *
 * @param[in] nrf_error   Error code.
 * @param[in] p_ctx       Parameter from the event handler.
 * @param[in] conn_handle Connection handle.
 */
static void gatt_error_handler(uint32_t   nrf_error,
                               void     * p_ctx,
                               uint16_t   conn_handle)
{
    ble_cts_c_t * p_cts = (ble_cts_c_t *)p_ctx;

    NRF_LOG_DEBUG("A GATT Client error has occurred on conn_handle: 0X%X", conn_handle);

    if (p_cts->error_handler != NULL)
    {
        p_cts->error_handler(nrf_error);
    }
}


/**@brief Function for handling events from the Database Discovery module.
 *
 * @details This function handles an event from the Database Discovery module, and determines
 *          whether it relates to the discovery of Current Time Service at the peer. If it does, this function
 *          calls the application's event handler to indicate that the Current Time Service was
 *          discovered at the peer. The function also populates the event with service-related
 *          information before providing it to the application.
 *
 * @param[in] p_evt Pointer to the event received from the Database Discovery module.
 *
 */
void ble_cts_c_on_db_disc_evt(ble_cts_c_t * p_cts, ble_db_discovery_evt_t * p_evt)
{
    NRF_LOG_DEBUG("Database Discovery handler called with event 0x%x", p_evt->evt_type);

    ble_cts_c_evt_t evt;
    const ble_gatt_db_char_t * p_chars = p_evt->params.discovered_db.charateristics;

    evt.conn_handle = p_evt->conn_handle;

    // Check if the Current Time Service was discovered.
    if (   (p_evt->evt_type == BLE_DB_DISCOVERY_COMPLETE)
        && (p_evt->params.discovered_db.srv_uuid.uuid == BLE_UUID_CURRENT_TIME_SERVICE)
        && (p_evt->params.discovered_db.srv_uuid.type == BLE_UUID_TYPE_BLE))
    {
        // Find the handles of the Current Time characteristic.
        for (uint32_t i = 0; i < p_evt->params.discovered_db.char_count; i++)
        {
            if (p_evt->params.discovered_db.charateristics[i].characteristic.uuid.uuid ==
                BLE_UUID_CURRENT_TIME_CHAR)
            {
                // Found Current Time characteristic. Store CCCD and value handle and break.
                evt.params.char_handles.cts_handle      = p_chars->characteristic.handle_value;
                evt.params.char_handles.cts_cccd_handle = p_chars->cccd_handle;
                break;
            }
        }

        NRF_LOG_INFO("Current Time Service discovered at peer.");

        evt.evt_type    = BLE_CTS_C_EVT_DISCOVERY_COMPLETE;
    }
    else if ((p_evt->evt_type == BLE_DB_DISCOVERY_SRV_NOT_FOUND) ||
             (p_evt->evt_type == BLE_DB_DISCOVERY_ERROR))
    {
        evt.evt_type    = BLE_CTS_C_EVT_DISCOVERY_FAILED;
    }
    else
    {
        return;
    }

    p_cts->evt_handler(p_cts, &evt);
}


uint32_t ble_cts_c_init(ble_cts_c_t * p_cts, ble_cts_c_init_t const * p_cts_init)
{
    // Verify that the parameters needed to initialize this instance of CTS are not NULL.
    VERIFY_PARAM_NOT_NULL(p_cts);
    VERIFY_PARAM_NOT_NULL(p_cts_init);
    VERIFY_PARAM_NOT_NULL(p_cts_init->error_handler);
    VERIFY_PARAM_NOT_NULL(p_cts_init->evt_handler);
    VERIFY_PARAM_NOT_NULL(p_cts_init->p_gatt_queue);

    static ble_uuid_t cts_uuid;

    BLE_UUID_BLE_ASSIGN(cts_uuid, BLE_UUID_CURRENT_TIME_SERVICE);

    p_cts->evt_handler                  = p_cts_init->evt_handler;
    p_cts->error_handler                = p_cts_init->error_handler;
    p_cts->conn_handle                  = BLE_CONN_HANDLE_INVALID;
    p_cts->char_handles.cts_handle      = BLE_GATT_HANDLE_INVALID;
    p_cts->char_handles.cts_cccd_handle = BLE_GATT_HANDLE_INVALID;
    p_cts->p_gatt_queue                 = p_cts_init->p_gatt_queue;

    return ble_db_discovery_evt_register(&cts_uuid);
}


/**@brief Function for decoding a read from the Current Time characteristic.
 *
 * @param[in] p_time  Current Time structure.
 * @param[in] p_data  Pointer to the buffer containing the Current Time.
 * @param[in] length  Length of the buffer containing the Current Time.
 *
 * @retval NRF_SUCCESS 			If the time struct is valid.
 * @retval NRF_ERROR_DATA_SIZE 	If the length does not match the expected size of the data.
 */
static uint32_t current_time_decode(current_time_char_t * p_time,
                                    uint8_t const       * p_data,
                                    uint32_t const        length)
{
    //lint -save -e415 -e416 "Access of out of bounds pointer" "Creation of out of bounds pointer"

    if (length != CTS_C_CURRENT_TIME_EXPECTED_LENGTH)
    {
        // Return to prevent accessing the out-of-bounds data.
        return NRF_ERROR_DATA_SIZE;
    }

    NRF_LOG_DEBUG("Current Time read response data:");
    NRF_LOG_HEXDUMP_DEBUG(p_data, 10);

    uint32_t index = 0;

    // Date.
    index += ble_date_time_decode(&p_time->exact_time_256.day_date_time.date_time, p_data);

    // Day of week.
    p_time->exact_time_256.day_date_time.day_of_week = p_data[index++];

    // Fractions of a second.
    p_time->exact_time_256.fractions256 = p_data[index++];

    // Reason for updating the time.
    p_time->adjust_reason.manual_time_update              = (p_data[index] >> 0) & 0x01;
    p_time->adjust_reason.external_reference_time_update  = (p_data[index] >> 1) & 0x01;
    p_time->adjust_reason.change_of_time_zone             = (p_data[index] >> 2) & 0x01;
    p_time->adjust_reason.change_of_daylight_savings_time = (p_data[index] >> 3) & 0x01;

    //lint -restore
    return NRF_SUCCESS;
}


/**@brief Function for decoding a read from the Current Time characteristic.
 *
 * @param[in] p_time  Current Time struct.
 *
 * @retval NRF_SUCCESS 				If the time struct is valid.
 * @retval NRF_ERROR_INVALID_DATA 	If the time is out of bounds.
 */
static uint32_t current_time_validate(current_time_char_t * p_time)
{
    // Year.
    if (   (p_time->exact_time_256.day_date_time.date_time.year > CTS_YEAR_MAX)
        || ((p_time->exact_time_256.day_date_time.date_time.year < CTS_YEAR_MIN)
         && (p_time->exact_time_256.day_date_time.date_time.year != 0)))
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Month.
    if (p_time->exact_time_256.day_date_time.date_time.month > 12)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Day.
    if (p_time->exact_time_256.day_date_time.date_time.day > 31)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Hours.
    if (p_time->exact_time_256.day_date_time.date_time.hours > 23)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Minutes.
    if (p_time->exact_time_256.day_date_time.date_time.minutes > 59)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Seconds.
    if (p_time->exact_time_256.day_date_time.date_time.seconds > 59)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    // Day of week.
    if (p_time->exact_time_256.day_date_time.day_of_week > 7)
    {
        return NRF_ERROR_INVALID_DATA;
    }

    return NRF_SUCCESS;
}


/**@brief Function for reading the Current Time. The time is decoded, and then validated.
 *        Depending on the outcome, the CTS event handler will be called with
 *        the Current Time event or an invalid time event.
 *
 * @param[in] p_cts      Current Time Service client structure.
 * @param[in] p_ble_evt  Event received from the BLE stack.
 */
static void current_time_read(ble_cts_c_t * p_cts, ble_evt_t const * p_ble_evt)
{
    ble_cts_c_evt_t evt;
    uint32_t        err_code = NRF_SUCCESS;

    // Check whether the event is on the same connection as this CTS instance
    if (p_cts->conn_handle != p_ble_evt->evt.gattc_evt.conn_handle)
    {
        return;
    }

    if (p_ble_evt->evt.gattc_evt.gatt_status == BLE_GATT_STATUS_SUCCESS)
    {
        err_code = current_time_decode(&evt.params.current_time,
                                       p_ble_evt->evt.gattc_evt.params.read_rsp.data,
                                       p_ble_evt->evt.gattc_evt.params.read_rsp.len);

        if (err_code != NRF_SUCCESS)
        {
            // The data length was invalid. Decoding was not completed.
            evt.evt_type = BLE_CTS_C_EVT_INVALID_TIME;
        }
        else
        {
            // Verify that the time is valid.
            err_code = current_time_validate(&evt.params.current_time);

            if (err_code != NRF_SUCCESS)
            {
                // Invalid time received.
                evt.evt_type = BLE_CTS_C_EVT_INVALID_TIME;
            }
            else
            {
                // Valid time reveiced.
                evt.evt_type = BLE_CTS_C_EVT_CURRENT_TIME;
            }
        }
        p_cts->evt_handler(p_cts, &evt);
    }
}


/**@brief Function for handling the Disconnect event.
 *
 * @param[in] p_cts      Current Time Service client structure.
 * @param[in] p_ble_evt  Event received from the BLE stack.
 */
static void on_disconnect(ble_cts_c_t * p_cts, ble_evt_t const * p_ble_evt)
{
    if (p_cts->conn_handle == p_ble_evt->evt.gap_evt.conn_handle)
    {
        p_cts->conn_handle = BLE_CONN_HANDLE_INVALID;
    }

    if (ble_cts_c_is_cts_discovered(p_cts))
    {
        // There was a valid instance of CTS on the peer. Send an event to the
        // application, so that it can do any clean up related to this module.
        ble_cts_c_evt_t evt;

        evt.evt_type = BLE_CTS_C_EVT_DISCONN_COMPLETE;

        p_cts->evt_handler(p_cts, &evt);
        p_cts->char_handles.cts_handle      = BLE_GATT_HANDLE_INVALID;
        p_cts->char_handles.cts_cccd_handle = BLE_GATT_HANDLE_INVALID;
    }
}


void ble_cts_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    ble_cts_c_t * p_cts = (ble_cts_c_t *)p_context;
    NRF_LOG_DEBUG("BLE event handler called with event 0x%x", p_ble_evt->header.evt_id);

    switch (p_ble_evt->header.evt_id)
    {
        case BLE_GATTC_EVT_READ_RSP:
            current_time_read(p_cts, p_ble_evt);
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            on_disconnect(p_cts, p_ble_evt);
            break;

        default:
            // No implementation needed.
            break;
    }
}


uint32_t ble_cts_c_current_time_read(ble_cts_c_t const * p_cts)
{
    if (!ble_cts_c_is_cts_discovered(p_cts))
    {
        return NRF_ERROR_NOT_FOUND;
    }

    nrf_ble_gq_req_t read_req;

    memset(&read_req, 0, sizeof(nrf_ble_gq_req_t));

    read_req.type                     = NRF_BLE_GQ_REQ_GATTC_READ;
    read_req.error_handler.cb         = gatt_error_handler;
    read_req.error_handler.p_ctx      = (ble_cts_c_t *)p_cts;
    read_req.params.gattc_read.handle = p_cts->char_handles.cts_handle;
    read_req.params.gattc_read.offset = 0;

    return nrf_ble_gq_item_add(p_cts->p_gatt_queue, &read_req, p_cts->conn_handle);
}


uint32_t ble_cts_c_handles_assign(ble_cts_c_t               * p_cts,
                                  const uint16_t              conn_handle,
                                  const ble_cts_c_handles_t * p_peer_handles)
{
    VERIFY_PARAM_NOT_NULL(p_cts);

    p_cts->conn_handle = conn_handle;
    if (p_peer_handles != NULL)
    {
        p_cts->char_handles.cts_cccd_handle = p_peer_handles->cts_cccd_handle;
        p_cts->char_handles.cts_handle      = p_peer_handles->cts_handle;
    }

    return nrf_ble_gq_conn_handle_register(p_cts->p_gatt_queue, conn_handle);
}
#endif // NRF_MODULE_ENABLED(BLE_CTS_C)
