/**
 * Copyright (c) 2016 - 2021, Nordic Semiconductor ASA
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
/* Disclaimer: This client implementation of the Apple Notification Center Service can and will be changed at any time by Nordic Semiconductor ASA.
 * Server implementations such as the ones found in iOS can be changed at any time by Apple and may cause this client implementation to stop working.
 */

#include "ancs_app_attr_get.h"
#include "nrf_ble_ancs_c.h"
#include "sdk_macros.h"
#include "nrf_log.h"
#include "string.h"

#define GATTC_OPCODE_SIZE                1      /**< Size of the GATTC OPCODE. */
#define GATTC_ATTR_HANDLE_SIZE           4      /**< Size of the attribute handle. */


#define ANCS_GATTC_WRITE_PAYLOAD_LEN_MAX (BLE_GATT_ATT_MTU_DEFAULT - GATTC_OPCODE_SIZE - GATTC_ATTR_HANDLE_SIZE)  /**< Maximum length of the data that can be sent in one write. */


/**@brief Enumeration for keeping track of the state-based encoding while requesting app attributes. */
typedef enum
{
    APP_ATTR_COMMAND_ID, /**< Currently encoding the command ID. */
    APP_ATTR_APP_ID,     /**< Currently encoding the app ID. */
    APP_ATTR_ATTR_ID,    /**< Currently encoding the attribute ID. */
    APP_ATTR_DONE        /**< Encoding done. */
}encode_app_attr_t;


/**@brief Function for determining whether an attribute is requested.
 *
 * @param[in] p_ancs iOS notification structure. This structure must be supplied by
 *                   the application. It identifies the particular client instance to use.
 *
 * @return True  If it is requested
 * @return False If it is not requested.
*/
static bool app_attr_is_requested(ble_ancs_c_t * p_ancs, uint32_t attr_id)
{
    if (p_ancs->ancs_app_attr_list[attr_id].get == true)
    {
        return true;
    }
    return false;
}

/**@brief Function for counting the number of attributes that will be requested upon a "get app attributes" command.
 *
 * @param[in] p_ancs iOS notification structure. This structure must be supplied by
 *                   the application. It identifies the particular client instance to use.
 *
 * @return           Number of attributes that will be requested upon a "get app attributes" command.
*/
static uint32_t app_attr_nb_to_get(ble_ancs_c_t * p_ancs)
{
    uint32_t attr_nb_to_get = 0;
    for (uint32_t i = 0; i < (sizeof(p_ancs->ancs_app_attr_list)/sizeof(ble_ancs_c_attr_list_t)); i++)
    {
        if (app_attr_is_requested(p_ancs,i))
        {
            attr_nb_to_get++;
        }
    }
    return attr_nb_to_get;
}


/**@brief Function for encoding the command ID as part of assembling a "get app attributes" command.
 *
 * @param[in]     p_ancs       iOS notification structure. This structure must be supplied by
 *                             the application. It identifies the particular client instance to use.
 * @param[in]     handle_value The handle that receives the execute command.
 * @param[in]     p_offset     Pointer to the offset for the write.
 * @param[in]     p_index      Pointer to the length encoded so far for the current write.
 * @param[in,out] p_gq_req     Pointer to the BLE GATT request structure.
 */
static ret_code_t queued_write_tx_message(ble_ancs_c_t     * p_ancs,
                                          uint16_t           handle_value,
                                          uint16_t         * p_offset,
                                          uint32_t         * p_index,
                                          nrf_ble_gq_req_t * p_gq_req)
{
    NRF_LOG_DEBUG("Starting new tx message.");

    p_gq_req->type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    p_gq_req->error_handler.cb            = p_ancs->gatt_err_handler;
    p_gq_req->error_handler.p_ctx         = p_ancs;
    p_gq_req->params.gattc_write.len      = *p_index;
    p_gq_req->params.gattc_write.offset   = *p_offset;
    p_gq_req->params.gattc_write.write_op = BLE_GATT_OP_PREP_WRITE_REQ;
    p_gq_req->params.gattc_write.handle   = handle_value;

    return nrf_ble_gq_item_add(p_ancs->p_gatt_queue, p_gq_req, p_ancs->conn_handle);
}


/**@brief Function for encoding the command ID as part of assembling a "get app attributes" command.
 *
 * @param[in]     p_index    Pointer to the length encoded so far for the current write.
 * @param[in,out] p_gq_req   Pointer to the BLE GATT request structure.
 */
static encode_app_attr_t app_attr_encode_cmd_id(uint32_t         * index,
                                                nrf_ble_gq_req_t * p_gq_req)
{
    uint8_t * p_value = (uint8_t *)p_gq_req->params.gattc_write.p_value;
    NRF_LOG_DEBUG("Encoding command ID.");

    // Encode Command ID.
    p_value[(*index)++] = BLE_ANCS_COMMAND_ID_GET_APP_ATTRIBUTES;
    return APP_ATTR_APP_ID;
}

/**@brief Function for encoding the app ID as part of assembling a "get app attributes" command.
 *
 * @param[in] p_ancs     					iOS notification structure. This structure must be supplied by
 *                       					the application. It identifies the particular client instance to use.
 * @param[in] p_app_id   					The app ID of the app for which to request app attributes.
 * @param[in] app_id_len 					Length of the app ID.
 * @param[in] p_index    					Pointer to the length encoded so far for the current write.
 * @param[in] p_offset   					Pointer to the accumulated offset for the next write.
 * @param[in] p_gq_req   					Pointer to the BLE GATT request structure.
 * @param[in] p_app_id_bytes_encoded_count 	Variable to keep count of the encoded app ID bytes.
 *                                         	As long as it is lower than the length of the app ID,
 *                                         	parsing continues.
 */
static encode_app_attr_t app_attr_encode_app_id(ble_ancs_c_t     * p_ancs,
                                                uint32_t         * p_index,
                                                uint16_t         * p_offset,
                                                nrf_ble_gq_req_t * p_gq_req,
                                                const uint8_t    * p_app_id,
                                                const uint32_t     app_id_len,
                                                uint32_t         * p_app_id_bytes_encoded_count)
{
    ret_code_t   err_code;
    uint8_t    * p_value = (uint8_t *)p_gq_req->params.gattc_write.p_value;

    NRF_LOG_DEBUG("Encoding app ID.");
    if (*p_index >= ANCS_GATTC_WRITE_PAYLOAD_LEN_MAX)
    {
        err_code = queued_write_tx_message(p_ancs,
                                           p_ancs->service.control_point_char.handle_value, 
                                           p_offset, 
                                           p_index, 
                                           p_gq_req);

        if ((err_code != NRF_SUCCESS) && (p_ancs->error_handler != NULL))
        {
            p_ancs->error_handler(err_code);
        }

        *(p_offset) += *p_index;
        *p_index = 0;
    }

    //Encode app identifier.
    if (*p_app_id_bytes_encoded_count == app_id_len)
    {
        p_value[(*p_index)++] = '\0';
        (*p_app_id_bytes_encoded_count)++;
    }
    NRF_LOG_DEBUG("%c", p_app_id[(*p_app_id_bytes_encoded_count)]);
    if (*p_app_id_bytes_encoded_count < app_id_len)
    {
        p_value[(*p_index)++] = p_app_id[(*p_app_id_bytes_encoded_count)++];
    }
    if (*p_app_id_bytes_encoded_count > app_id_len)
    {
        return APP_ATTR_ATTR_ID;
    }
    return APP_ATTR_APP_ID;
}

/**@brief Function for encoding the attribute ID as part of assembling a "get app attributes" command.
 *
 * @param[in]     p_ancs       iOS notification structure. This structure must be supplied by
 *                             the application. It identifies the particular client instance to use.
 * @param[in]     p_index      Pointer to the length encoded so far for the current write.
 * @param[in]     p_offset     Pointer to the accumulated offset for the next write.
 * @param[in,out] p_gq_req     Pointer to the BLE GATT request structure.
 * @param[in]     p_attr_count Pointer to a variable that iterates the possible app attributes.
 */
static encode_app_attr_t app_attr_encode_attr_id(ble_ancs_c_t     * p_ancs,
                                                 uint32_t         * p_index,
                                                 uint16_t         * p_offset,
                                                 nrf_ble_gq_req_t * p_gq_req,
                                                 uint32_t         * p_attr_count,
                                                 uint32_t         * attr_get_total_nb)
{
    ret_code_t   err_code;
    uint8_t    * p_value = (uint8_t *)p_gq_req->params.gattc_write.p_value;

    NRF_LOG_DEBUG("Encoding attribute ID.");
    if ((*p_index) >= ANCS_GATTC_WRITE_PAYLOAD_LEN_MAX)
    {
        err_code = queued_write_tx_message(p_ancs,
                                           p_ancs->service.control_point_char.handle_value,
                                           p_offset,
                                           p_index, 
                                           p_gq_req);

        if ((err_code != NRF_SUCCESS) && (p_ancs->error_handler != NULL))
        {
            p_ancs->error_handler(err_code);
        }
        *(p_offset) += *p_index;
        *p_index = 0;
    }
    //Encode Attribute ID.
    if (*p_attr_count < BLE_ANCS_NB_OF_APP_ATTR)
    {
        if (app_attr_is_requested(p_ancs, *p_attr_count))
        {
            p_value[(*p_index)] = *p_attr_count;
            p_ancs->number_of_requested_attr++;
            (*p_index)++;
            NRF_LOG_DEBUG("offset %i", *p_offset);
        }
        (*p_attr_count)++;
    }
    if (*p_attr_count == BLE_ANCS_NB_OF_APP_ATTR)
    {
        return APP_ATTR_DONE;
    }
    return APP_ATTR_APP_ID;
}

/**@brief Function for writing the "execute write" command to a handle for a given connection.
 *
 * @param[in] p_ancs       iOS notification structure. This structure must be supplied by
 *                         the application. It identifies the particular client instance to use.
 * @param[in] handle_value Handle that receives the "execute write" command.
 * @param[in] p_gq_req     Pointer to the BLE GATT request structure.
 */
static ret_code_t app_attr_execute_write(ble_ancs_c_t     * p_ancs,
                                         uint16_t           handle_value,
                                         nrf_ble_gq_req_t * p_gq_req)
{
    NRF_LOG_DEBUG("Sending Execute Write command.");
    memset(p_gq_req, 0, sizeof(nrf_ble_gq_req_t));

    p_gq_req->type                        = NRF_BLE_GQ_REQ_GATTC_WRITE;
    p_gq_req->error_handler.cb            = p_ancs->gatt_err_handler;
    p_gq_req->error_handler.p_ctx         = p_ancs;
    p_gq_req->params.gattc_write.handle   = handle_value;
    p_gq_req->params.gattc_write.offset   = 0;
    p_gq_req->params.gattc_write.write_op = BLE_GATT_OP_EXEC_WRITE_REQ;
    p_gq_req->params.gattc_write.flags    = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE;
    p_gq_req->params.gattc_write.len      = 0;

    return nrf_ble_gq_item_add(p_ancs->p_gatt_queue, p_gq_req, p_ancs->conn_handle);
}


/**@brief Function for sending a "get app attributes" request.
 *
 * @details Since the app ID may not fit in a single write, long write
 *          with a state machine is used to encode the "get app attributes" request.
 *
 * @param[in] p_ancs     iOS notification structure. This structure must be supplied by
 *                       the application. It identifies the particular client instance to use.
 * @param[in] p_app_id   The app ID of the app for which to request app attributes.
 * @param[in] app_id_len Length of the app ID.
 *
*/
static uint32_t app_attr_get(ble_ancs_c_t  * p_ancs,
                             const uint8_t * p_app_id,
                             uint32_t        app_id_len)
{
    uint32_t          index                      = 0;
    uint32_t          attr_bytes_encoded_count   = 0;
    uint16_t          offset                     = 0;
    uint32_t          app_id_bytes_encoded_count = 0;
    encode_app_attr_t state                      = APP_ATTR_COMMAND_ID;
    ret_code_t        err_code;

    p_ancs->number_of_requested_attr             = 0;

    uint32_t         attr_get_total_nb = app_attr_nb_to_get(p_ancs);
    nrf_ble_gq_req_t ancs_req;
    uint8_t          gatt_value[BLE_ANCS_WRITE_MAX_MSG_LENGTH];

    memset(&ancs_req, 0, sizeof(nrf_ble_gq_req_t));

    ancs_req.params.gattc_write.p_value = gatt_value;

    while (state != APP_ATTR_DONE)
    {
        switch (state)
        {
            case APP_ATTR_COMMAND_ID:
                state = app_attr_encode_cmd_id(&index,
                                               &ancs_req);
            break;
            case APP_ATTR_APP_ID:
                state = app_attr_encode_app_id(p_ancs,
                                               &index,
                                               &offset,
                                               &ancs_req,
                                               p_app_id,
                                               app_id_len,
                                               &app_id_bytes_encoded_count);
            break;
            case APP_ATTR_ATTR_ID:
                state = app_attr_encode_attr_id(p_ancs,
                                                &index,
                                                &offset,
                                                &ancs_req,
                                                &attr_bytes_encoded_count,
                                                &attr_get_total_nb);
                break;
            case APP_ATTR_DONE:
                break;
            default:
                break;
        }
    }
    err_code = queued_write_tx_message(p_ancs,
                                       p_ancs->service.control_point_char.handle_value,
                                       &offset,
                                       &index,
                                       &ancs_req);
    VERIFY_SUCCESS(err_code);

    err_code = app_attr_execute_write(p_ancs,
                                      p_ancs->service.control_point_char.handle_value,
                                      &ancs_req);

    p_ancs->parse_info.expected_number_of_attrs = p_ancs->number_of_requested_attr;

    return err_code;
}


uint32_t ancs_c_app_attr_request(ble_ancs_c_t          * p_ancs,
                                         const uint8_t * p_app_id,
                                         uint32_t        len)
{
    uint32_t err_code;

    if (len == 0)
    {
        return NRF_ERROR_DATA_SIZE;
    }
    if (p_app_id[len] != '\0') // App ID to be requested must be null-terminated.
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    p_ancs->parse_info.parse_state = COMMAND_ID;
    err_code                       = app_attr_get(p_ancs, p_app_id, len);
    VERIFY_SUCCESS(err_code);
    return NRF_SUCCESS;
}
