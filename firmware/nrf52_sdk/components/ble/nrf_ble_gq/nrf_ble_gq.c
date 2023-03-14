/**
 * Copyright (c) 2018 - 2021, Nordic Semiconductor ASA
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
#if NRF_MODULE_ENABLED(NRF_BLE_GQ)

#include "nrf_ble_gq.h"

#define NRF_LOG_MODULE_NAME nrf_ble_gq
#include "nrf_log.h"
NRF_LOG_MODULE_REGISTER();

/**@brief Pointer used to describe memory allocator for GATT request. */
typedef ret_code_t (* req_data_alloc_t) (nrf_memobj_pool_t const * p_data_pool, 
                                         nrf_ble_gq_req_t  * const p_req);


/**@brief Function allocates memory for data associated with @ref NRF_BLE_GQ_REQ_GATTC_WRITE
 *        request.
 *
 * @param[in] p_data_pool  Pointer to general memory pool.
 * @param[in] p_req        Pointer to GATTC write request.
 *
 * @retval    NRF_SUCCESS              If the write data was allocated successfully.
 * @retval    NRF_ERROR_INVALID_LENGTH If data to be written is too long.
 * @retval    NRF_ERROR_NO_MEM         If there was no room either in the data pool for new allocation.
 */
static ret_code_t gattc_write_alloc(nrf_memobj_pool_t const * p_data_pool,
                                    nrf_ble_gq_req_t  * const p_req)
{
    nrf_ble_gq_gattc_write_t * p_gattc_write = &p_req->params.gattc_write;

    // Check if the payload data is not too long.
    if (p_gattc_write->len > NRF_BLE_GQ_GATTC_WRITE_MAX_DATA_LEN)
    {
        return NRF_ERROR_INVALID_LENGTH;
    }

    // Allocate memory for GATTC write request.
    p_req->p_mem_obj = nrf_memobj_alloc(p_data_pool,
                                        p_gattc_write->len);
    if (p_req->p_mem_obj == NULL)
    {
        return NRF_ERROR_NO_MEM;
    }

    // Copy relevant data to the pool.
    nrf_memobj_write(p_req->p_mem_obj, (void *) p_gattc_write->p_value, p_gattc_write->len, 0);

    NRF_LOG_DEBUG("Pointer to allocated memory block: %p.", p_req->p_mem_obj);
    return NRF_SUCCESS;
}


/**@brief Function allocates memory for data associated with @ref NRF_BLE_GQ_REQ_GATTS_HVX
 *        request.
 *
 * @param[in] p_data_pool  Pointer to general memory pool.
 * @param[in] p_req        Pointer to GATTS hvx request.
 *
 * @retval    NRF_SUCCESS              If the notification or indication data was allocated successfully.
 * @retval    NRF_ERROR_INVALID_LENGTH If data to be written is too long.
 * @retval    NRF_ERROR_NO_MEM         If there was no room either in the data pool for new allocation.
 */
static ret_code_t gatts_hvx_alloc(nrf_memobj_pool_t const * p_data_pool,
                                  nrf_ble_gq_req_t  * const p_req)
{
    nrf_ble_gq_gatts_hvx_t * p_gatts_hvx = &p_req->params.gatts_hvx;

    // Check if the payload data is not too long.
    if (*p_gatts_hvx->p_len > NRF_BLE_GQ_GATTS_HVX_MAX_DATA_LEN)
    {
        return NRF_ERROR_INVALID_LENGTH;
    }

    // Allocate memory for GATTS notification or indication request.
    p_req->p_mem_obj = nrf_memobj_alloc(p_data_pool,
                                        *p_gatts_hvx->p_len + sizeof(uint16_t));
    if (p_req->p_mem_obj == NULL)
    {
        return NRF_ERROR_NO_MEM;
    }

    // Copy relevant data to the pool.
    nrf_memobj_write(p_req->p_mem_obj, (void *)p_gatts_hvx->p_len, sizeof(uint16_t), 0);
    nrf_memobj_write(p_req->p_mem_obj, 
                     (void *)p_gatts_hvx->p_data, 
                     *p_gatts_hvx->p_len, 
                     sizeof(uint16_t));

    NRF_LOG_DEBUG("Pointer to allocated memory block: %p.", p_req->p_mem_obj);
    return NRF_SUCCESS;
}


/**@brief Array of memory allocators for different types of @ref nrf_ble_gq_req_t. */
static const req_data_alloc_t m_req_data_alloc[NRF_BLE_GQ_REQ_NUM] =
{
    [NRF_BLE_GQ_REQ_GATTC_READ]     = NULL,
    [NRF_BLE_GQ_REQ_GATTC_WRITE]    = gattc_write_alloc,
    [NRF_BLE_GQ_REQ_SRV_DISCOVERY]  = NULL,
    [NRF_BLE_GQ_REQ_CHAR_DISCOVERY] = NULL,
    [NRF_BLE_GQ_REQ_DESC_DISCOVERY] = NULL,
    [NRF_BLE_GQ_REQ_GATTS_HVX]      = gatts_hvx_alloc
};


/**@brief Function handles error codes returned by GATT requests.
 *
 * @param[in] p_req       Pointer to GATT request.
 * @param[in] err_code    Error code returned by SoftDevice.
 * @param[in] conn_handle Connection handle.
 */
__STATIC_INLINE void request_err_code_handle(nrf_ble_gq_req_t const * const p_req,
                                             uint16_t                       conn_handle,
                                             ret_code_t                     err_code)
{
    if (err_code == NRF_SUCCESS)
    {
        NRF_LOG_DEBUG("SD GATT procedure (%d) succeeded on connection handle: %d.",
                       p_req->type,
                       conn_handle);
    }
    else
    {
        NRF_LOG_ERROR("SD GATT procedure (%d) failed on connection handle %d with error: 0x%08X.",
                      p_req->type, conn_handle, err_code);
        if (p_req->error_handler.cb != NULL)
        {
             p_req->error_handler.cb(err_code, p_req->error_handler.p_ctx, conn_handle);
        }
    }
}


/**@brief Function processes subsequent requests from the BGQ instance queue.
 *
 * @param[in] p_queue      Pointer to the queue instance.
 * @param[in] conn_handle  Connection handle.
 */
static void queue_process(nrf_queue_t const * const p_queue, uint16_t conn_handle)
{
    ret_code_t       err_code;
    nrf_ble_gq_req_t ble_req;

    NRF_LOG_DEBUG("Processing the request queue...");

    err_code = nrf_queue_peek(p_queue, &ble_req);
    if (err_code == NRF_SUCCESS) // Queue is not empty
    {
        switch (ble_req.type)
        {
            case NRF_BLE_GQ_REQ_GATTC_READ:
                NRF_LOG_DEBUG("GATTC Read Request");
                err_code = sd_ble_gattc_read(conn_handle,
                                             ble_req.params.gattc_read.handle,
                                             ble_req.params.gattc_read.offset);
                break;

            case NRF_BLE_GQ_REQ_GATTC_WRITE:
            {
                uint8_t write_data[NRF_BLE_GQ_GATTC_WRITE_MAX_DATA_LEN];

                // Retrieve allocated data.
                ble_req.params.gattc_write.p_value = write_data;
                nrf_memobj_read(ble_req.p_mem_obj,
                                (void *) ble_req.params.gattc_write.p_value,
                                ble_req.params.gattc_write.len, 0);

                NRF_LOG_DEBUG("GATTC Write Request");
                err_code = sd_ble_gattc_write(conn_handle,
                                              &ble_req.params.gattc_write);
            } break;

            case NRF_BLE_GQ_REQ_SRV_DISCOVERY:
            {
                NRF_LOG_DEBUG("GATTC Primary Service Discovery Request");
                err_code = sd_ble_gattc_primary_services_discover(conn_handle,
                                                                  ble_req.params.gattc_srv_disc.start_handle,
                                                                  &ble_req.params.gattc_srv_disc.srvc_uuid);
            } break;

            case NRF_BLE_GQ_REQ_CHAR_DISCOVERY:
            {
                NRF_LOG_DEBUG("GATTC Characteristic Discovery Request");
                err_code = sd_ble_gattc_characteristics_discover(conn_handle,
                                                                 &ble_req.params.gattc_char_disc);
            } break;

            case NRF_BLE_GQ_REQ_DESC_DISCOVERY:
            {
                NRF_LOG_DEBUG("GATTC Characteristic Descriptor Discovery Request")
                err_code = sd_ble_gattc_descriptors_discover(conn_handle,
                                                             &ble_req.params.gattc_desc_disc);
            } break;

            case NRF_BLE_GQ_REQ_GATTS_HVX:
            {
                uint8_t  hvx_data[NRF_BLE_GQ_GATTS_HVX_MAX_DATA_LEN];
                uint16_t len;
                uint16_t hvx_len;

                // Retrieve allocated data.
                ble_req.params.gatts_hvx.p_data = hvx_data;
                nrf_memobj_read(ble_req.p_mem_obj,
                                (void *) &hvx_len,
                                sizeof(uint16_t),
                                0);
                ble_req.params.gatts_hvx.p_len = &hvx_len;
                nrf_memobj_read(ble_req.p_mem_obj,
                                (void *) ble_req.params.gatts_hvx.p_data,
                                *ble_req.params.gatts_hvx.p_len,
                                sizeof(uint16_t));

                len = hvx_len;

                NRF_LOG_DEBUG("GATTS HVX");
                err_code = sd_ble_gatts_hvx(conn_handle,
                                            &ble_req.params.gatts_hvx);

                if ((err_code == NRF_SUCCESS) &&
                    (len != hvx_len))
                {
                    err_code = NRF_ERROR_DATA_SIZE;
                }
            } break;

            default:
                NRF_LOG_WARNING("Unimplemented GATT Request");
                break;
        }

        if (err_code == NRF_ERROR_BUSY) // Softdevice is processing another GATT request.
        {
            NRF_LOG_DEBUG("SD is currently busy. The GATT request procedure will be attempted \
                          again later.");
        }
        else
        {
            // Remove last request descriptor from the queue and free data associated with it.
            if (m_req_data_alloc[ble_req.type] != NULL)
            {
                nrf_memobj_free(ble_req.p_mem_obj);
                NRF_LOG_DEBUG("Pointer to freed memory block: %p.", ble_req.p_mem_obj);
            }
            UNUSED_RETURN_VALUE(nrf_queue_pop(p_queue, &ble_req));

            request_err_code_handle(&ble_req, conn_handle, err_code);
        }
    }
}


/**@brief Function purges all requests from BGQ instance queues that are
 *        no longer used by any connection.
 *
 * @param[in] p_gatt_queue Pointer to the BGQ instance.
 */
static void queues_purge(nrf_ble_gq_t const * const p_gatt_queue)
{
    ret_code_t err_code;
    uint16_t   conn_id;

    err_code = nrf_queue_pop(p_gatt_queue->p_purge_queue, &conn_id);

    while (err_code == NRF_SUCCESS)
    {
        nrf_ble_gq_req_t    ble_req;
        nrf_queue_t const * p_queue;

        NRF_LOG_DEBUG("Purging request queue with id: %d", conn_id);

        p_queue  = &p_gatt_queue->p_req_queue[conn_id];
        err_code = nrf_queue_pop(p_queue, &ble_req);

        while (err_code == NRF_SUCCESS)
        {
            // Free data associated with this request if there is any.
            if (m_req_data_alloc[ble_req.type] != NULL)
            {
                nrf_memobj_free(ble_req.p_mem_obj);
                NRF_LOG_DEBUG("Pointer to freed memory block: %p.", ble_req.p_mem_obj);
            }

            err_code = nrf_queue_pop(p_queue, &ble_req);
        }

        err_code = nrf_queue_pop(p_gatt_queue->p_purge_queue, &conn_id);
    }
}


/**@brief Function processes single GATT request without queue.
 *
 * @param[in] p_req        Pointer to GATT request.
 * @param[in] conn_handle  Connection handle.
 *
 * @retval  true   If request is accepted by Softdevice.
 * @retval  false  If Softdevice is busy and the request should be queued.
 */
static bool request_process(nrf_ble_gq_req_t const * const p_req, uint16_t conn_handle)
{
    ret_code_t err_code = NRF_SUCCESS;

    switch (p_req->type)
    {
        case NRF_BLE_GQ_REQ_GATTC_READ:
            NRF_LOG_DEBUG("GATTC Read Request");
            err_code = sd_ble_gattc_read(conn_handle,
                                         p_req->params.gattc_read.handle,
                                         p_req->params.gattc_read.offset);
            break;

        case NRF_BLE_GQ_REQ_GATTC_WRITE:
            NRF_LOG_DEBUG("GATTC Write Request");
            err_code = sd_ble_gattc_write(conn_handle,
                                          &p_req->params.gattc_write);
            break;

        case NRF_BLE_GQ_REQ_SRV_DISCOVERY:
            NRF_LOG_DEBUG("GATTC Primary Services Discovery Request");
            err_code = sd_ble_gattc_primary_services_discover(conn_handle,
                                                              p_req->params.gattc_srv_disc.start_handle,
                                                              &p_req->params.gattc_srv_disc.srvc_uuid);
            break;

        case NRF_BLE_GQ_REQ_CHAR_DISCOVERY:
            NRF_LOG_DEBUG("GATTC Characteristic Discovery Request");
            err_code = sd_ble_gattc_characteristics_discover(conn_handle,
                                                             &p_req->params.gattc_char_disc);
            break;

        case NRF_BLE_GQ_REQ_DESC_DISCOVERY:
            NRF_LOG_DEBUG("GATTC Characteristic Descriptor Request");
            err_code = sd_ble_gattc_descriptors_discover(conn_handle,
                                                         &p_req->params.gattc_desc_disc);
            break;

        case NRF_BLE_GQ_REQ_GATTS_HVX:
        {
            uint16_t len = *p_req->params.gatts_hvx.p_len;

            NRF_LOG_DEBUG("GATTS Notification or Indication");

            err_code = sd_ble_gatts_hvx(conn_handle,
                                        &p_req->params.gatts_hvx);

            if ((err_code == NRF_SUCCESS) &&
                (len != *p_req->params.gatts_hvx.p_len))
            {
                err_code = NRF_ERROR_DATA_SIZE;
            }

        } break;

        default:
            NRF_LOG_WARNING("Unimplemented GATT Request");
            break;
    }

    if (err_code == NRF_ERROR_BUSY) // Softdevice is processing another GATT request.
    {
        NRF_LOG_DEBUG("SD is currently busy. The GATT request procedure will be attempted \
                      again later.");
        return false;
    }
    else
    {
        request_err_code_handle(p_req, conn_handle, err_code);
        return true;
    }
}


/**@brief Function finds ID for the provided connection handle within nrf_ble_gq_t instance registry.
 *
 * @param[in] p_gatt_queue  Pointer to the nrf_ble_gq_t instance.
 * @param[in] conn_handle   Connection handle.
 *
 * @return    Connection ID.
 */
static uint16_t conn_handle_id_find(nrf_ble_gq_t const * const p_gatt_queue, uint16_t conn_handle)
{
    uint16_t id;

    for (id = 0; id < p_gatt_queue->max_conns; id++)
    {
        if (conn_handle == p_gatt_queue->p_conn_handles[id])
        {
            return id;
        }
    }
    return id;
}


/**@brief Function registers provided connection handle within nrf_ble_gq_t instance registry.
 *
 * @param[in] p_gatt_queue  Pointer to the nrf_ble_gq_t instance.
 * @param[in] conn_handle   Connection handle.
 *
 * @retval    NRF_SUCCESS       If the registration was successful.
 * @retval    NRF_ERROR_NO_MEM  If there was no space for another connection handle.
 */
static ret_code_t conn_handle_register(nrf_ble_gq_t const * const p_gatt_queue, uint16_t conn_handle)
{
    for (uint16_t id = 0; id < p_gatt_queue->max_conns; id++)
    {
        if (p_gatt_queue->p_conn_handles[id] == BLE_CONN_HANDLE_INVALID)
        {
            p_gatt_queue->p_conn_handles[id] = conn_handle;
            return NRF_SUCCESS;
        }
    }
    return NRF_ERROR_NO_MEM;
}


/**@brief Function checks if any connection handle is registered in nrf_ble_gq_t instance.
 *
 * @param[in] p_gatt_queue Pointer to the nrf_ble_gq_t instance.
 *
 * @retval    true   There is at least one registered connection handle.
 * @retval    false  Connection handle registry is empty.
 */
static bool is_any_conn_handle_registered(nrf_ble_gq_t const * const p_gatt_queue)
{
    for (uint16_t id = 0; id < p_gatt_queue->max_conns; id++)
    {
        if (p_gatt_queue->p_conn_handles[id] != BLE_CONN_HANDLE_INVALID)
        {
            return true;
        }
    }
    return false;
}


ret_code_t nrf_ble_gq_item_add(nrf_ble_gq_t const * const p_gatt_queue,
                               nrf_ble_gq_req_t   * const p_req,
                               uint16_t                   conn_handle)
{
    ret_code_t err_code = NRF_SUCCESS;
    uint16_t   conn_id;

    NRF_LOG_DEBUG("Adding item to the request queue");

    VERIFY_PARAM_NOT_NULL(p_gatt_queue);
    VERIFY_PARAM_NOT_NULL(p_req);

    // Purge queues that are no longer used by any connection.
    queues_purge(p_gatt_queue);

    // Check if connection handle is registered and if GATT request is valid.
    conn_id = conn_handle_id_find(p_gatt_queue, conn_handle);
    if ((p_req->type >= NRF_BLE_GQ_REQ_NUM) || (conn_id == p_gatt_queue->max_conns))
    {
        return NRF_ERROR_INVALID_PARAM;
    }

    // Try processing a request without buffering.
    if (nrf_queue_is_empty(&p_gatt_queue->p_req_queue[conn_id]))
    {
        bool req_processed = request_process(p_req, conn_handle);
        if (req_processed)
        {
            return err_code;
        }
    }

    // Prepare request for buffering and add it to the queue.
    if (m_req_data_alloc[p_req->type] != NULL)
    {
        VERIFY_PARAM_NOT_NULL(p_gatt_queue->p_data_pool);

        err_code = m_req_data_alloc[p_req->type](p_gatt_queue->p_data_pool, p_req);
        VERIFY_SUCCESS(err_code);
    }

    err_code = nrf_queue_push(&p_gatt_queue->p_req_queue[conn_id], p_req);
    if ((err_code != NRF_SUCCESS) && (m_req_data_alloc[p_req->type] != NULL))
    {
        nrf_memobj_free(p_req->p_mem_obj);
        NRF_LOG_DEBUG("Pointer to freed memory block: %p.", p_req->p_mem_obj);
    }

    // Check if Softdevice is still busy.
    queue_process(&p_gatt_queue->p_req_queue[conn_id], conn_handle);
    return err_code;
}


ret_code_t nrf_ble_gq_conn_handle_register(nrf_ble_gq_t * const p_gatt_queue, uint16_t conn_handle)
{
    ret_code_t err_code = NRF_SUCCESS;
    uint16_t   conn_id;

    VERIFY_PARAM_NOT_NULL(p_gatt_queue);

    // Purge queues that are no longer used by any connection.
    queues_purge(p_gatt_queue);

    // Allow instance to claim connection handle only if it has not been claimed already.
    conn_id = conn_handle_id_find(p_gatt_queue, conn_handle);
    if (conn_id == p_gatt_queue->max_conns)
    {
        NRF_LOG_DEBUG("Registering connection handle: 0x%04X", conn_handle);

        // Initialize/reset data pool if possible.
        if (!is_any_conn_handle_registered(p_gatt_queue))
        {
            err_code = nrf_memobj_pool_init(p_gatt_queue->p_data_pool);
        }

        err_code = conn_handle_register(p_gatt_queue, conn_handle);
        VERIFY_SUCCESS(err_code);
    }
    return err_code;
}


void nrf_ble_gq_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context)
{
    nrf_ble_gq_t * p_gatt_queue = (nrf_ble_gq_t *) p_context;
    uint16_t       conn_handle;
    uint16_t       conn_id;

    if ((p_ble_evt == NULL) || (p_gatt_queue == NULL))
    {
        return;
    }

    // Obtain connection handle and filter out the events that do not trigger queue processing.
    if (p_ble_evt->header.evt_id == BLE_GAP_EVT_DISCONNECTED)
    {
        conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
    }
    else if ((p_ble_evt->header.evt_id >= BLE_GATTC_EVT_BASE) &&
             (p_ble_evt->header.evt_id <= BLE_GATTC_EVT_LAST))
    {
        conn_handle = p_ble_evt->evt.gattc_evt.conn_handle;
    }
    else if ((p_ble_evt->header.evt_id >= BLE_GATTS_EVT_BASE) &&
             (p_ble_evt->header.evt_id <= BLE_GATTS_EVT_LAST))
    {
        conn_handle = p_ble_evt->evt.gatts_evt.conn_handle;
    }
    else
    {
        // These events are irrelevant for this module.
        return;
    }

    // Check if connection handle is registered.
    conn_id = conn_handle_id_find(p_gatt_queue, conn_handle);
    if (conn_id == p_gatt_queue->max_conns)
    {
        return;
    }

    // Perform operations on the queue.
    if (p_ble_evt->header.evt_id == BLE_GAP_EVT_DISCONNECTED)
    {
        p_gatt_queue->p_conn_handles[conn_id] = BLE_CONN_HANDLE_INVALID;
        UNUSED_RETURN_VALUE(nrf_queue_push(p_gatt_queue->p_purge_queue, &conn_id));
    }
    else
    {
        queue_process(&p_gatt_queue->p_req_queue[conn_id], conn_handle);
    }
}


#endif // NRF_MODULE_ENABLED(NRF_BLE_GQ)
