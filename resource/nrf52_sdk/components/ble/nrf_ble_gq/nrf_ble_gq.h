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
/** @file
 *
 * @defgroup nrf_ble_gq BLE GATT Queue
 * @{
 * @ingroup ble_sdk_lib
 * @brief  Queue for the BLE GATT requests.
 *
 * @details The BLE GATT Queue (BGQ) module can be used to queue BLE GATT requests if the SoftDevice is not
 *          able to handle them at the moment. In this case, processing of queued request is
 *          postponed. Later on, when corresponding BLE event indicates that the SoftDevice may be
 *          free, the request is retried. For conceptual documentation of this module, see
 *          @ref lib_ble_gatt_queue.
 *
 */
#ifndef NRF_BLE_GQ_H__
#define NRF_BLE_GQ_H__

#include <stdint.h>
#include "sdk_common.h"
#include "nrf_memobj.h"
#include "nrf_queue.h"
#include "nrf_sdh_ble.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@brief   Macro for defining a nrf_ble_gq_t instance with default parameters.
 *
 * @param   _name            Name of the instance.
 * @param   _max_connections The maximal number of connection handles that can be registered.
 * @param   _queue_size      The maximal number of nrf_ble_gq_req_t instances that queue can hold.
 * @hideinitializer
 */
#define NRF_BLE_GQ_DEF(_name, _max_connections, _queue_size)        \
    NRF_BLE_GQ_CUSTOM_DEF(_name,                                    \
                          _max_connections,                         \
                          _queue_size,                              \
                          NRF_BLE_GQ_DATAPOOL_ELEMENT_SIZE,         \
                          NRF_BLE_GQ_DATAPOOL_ELEMENT_COUNT)


#if !(defined(__LINT__))
/**@brief   Macro for defining a nrf_ble_gq_t instance.
 *
 * @param   _name            Name of the instance.
 * @param   _max_connections The maximal number of connection handles that can be registered.
 * @param   _queue_size      The maximal number of nrf_ble_gq_req_t instances that queue can hold.
 * @param   _pool_elem_size  Size of a single element in the pool of memory objects.
 * @param   _pool_elem_count Number of elements in the pool of memory objects.
 * @hideinitializer
 */
#define NRF_BLE_GQ_CUSTOM_DEF(_name, _max_connections, _queue_size, _pool_elem_size, _pool_elem_count) \
    static uint16_t CONCAT_2(_name, conn_handles_arr)[] =                                              \
    {                                                                                                  \
        MACRO_REPEAT(_max_connections, NRF_BLE_GQ_CONN_HANDLE_INIT)                                    \
    };                                                                                                 \
    STATIC_ASSERT(ARRAY_SIZE(CONCAT_2(_name, conn_handles_arr)) == (_max_connections));                \
    NRF_QUEUE_ARRAY_DEF(nrf_ble_gq_req_t, CONCAT_2(_name, req_queue), _queue_size,                     \
                        NRF_QUEUE_MODE_NO_OVERFLOW, _max_connections);                                 \
    NRF_QUEUE_DEF(uint16_t, CONCAT_2(_name, purge_queue), _max_connections,                            \
                  NRF_QUEUE_MODE_NO_OVERFLOW);                                                         \
    NRF_MEMOBJ_POOL_DEF(CONCAT_2(_name, pool), _pool_elem_size, _pool_elem_count);                     \
    static nrf_ble_gq_t _name =                                                                        \
    {                                                                                                  \
        .max_conns      = (_max_connections),                                                          \
        .p_conn_handles = CONCAT_2(_name, conn_handles_arr),                                           \
        .p_req_queue    = CONCAT_2(_name, req_queue),                                                  \
        .p_purge_queue  = &CONCAT_2(_name, purge_queue),                                               \
        .p_data_pool    = &CONCAT_2(_name, pool)                                                       \
    };                                                                                                 \
    NRF_SDH_BLE_OBSERVER(_name ## _obs,                                                                \
                         NRF_BLE_GQ_BLE_OBSERVER_PRIO,                                                 \
                         nrf_ble_gq_on_ble_evt, &_name)
#else
#define NRF_BLE_GQ_CUSTOM_DEF(_name, _max_connections, _queue_size, _pool_elem_size, _pool_elem_count) \
    static nrf_ble_gq_t _name;
#endif // !(defined(__LINT__))

/**@brief Helping macro used to properly initialize connection handle array for nrf_ble_gq_t instance.
 *        Used in @ref NRF_BLE_GQ_CUSTOM_DEF.
 */
#define NRF_BLE_GQ_CONN_HANDLE_INIT(_arg) BLE_CONN_HANDLE_INVALID,

/**@brief BLE GATT request types. */
typedef enum
{
    NRF_BLE_GQ_REQ_GATTC_READ,     /**< GATTC Read Request. See @ref nrf_ble_gq_gattc_read_t and @ref sd_ble_gattc_read */
    NRF_BLE_GQ_REQ_GATTC_WRITE,    /**< GATTC Write Request. See @ref nrf_ble_gq_gattc_write_t and @ref sd_ble_gattc_write */
    NRF_BLE_GQ_REQ_SRV_DISCOVERY,  /**< GATTC Service Discovery Request. See @ref nrf_ble_gq_gattc_write_t and @ref sd_ble_gattc_primary_services_discover. */
    NRF_BLE_GQ_REQ_CHAR_DISCOVERY, /**< GATTC Characteristic Discovery Request. See @ref nrf_ble_gq_gattc_char_disc_t and @ref sd_ble_gattc_characteristics_discover. */
    NRF_BLE_GQ_REQ_DESC_DISCOVERY, /**< GATTC Characteristic Descriptor Discovery Request. See @ref nrf_ble_gq_gattc_desc_disc_t and @ref sd_ble_gattc_descriptors_discover*/
    NRF_BLE_GQ_REQ_GATTS_HVX,      /**< GATTS Handle Value Notification or Indication. See @ref nrf_ble_gq_gatts_hvx_t and @ref ble_gatts_hvx_params_t */
    NRF_BLE_GQ_REQ_NUM             /**< Total number of different GATT Request types */
} nrf_ble_gq_req_type_t;

/**@brief Pointer used to describe error handler for GATTC request. */
typedef void (* nrf_ble_gq_req_error_cb_t) (uint32_t   nrf_error,
                                            void     * p_context,
                                            uint16_t   conn_handle);

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_GATTC_READ request type. */
typedef struct
{
    uint16_t handle; /**< Handle of the Attribute to be read. */
    uint16_t offset; /**< Offset into the Attribute Value to be read. */
} nrf_ble_gq_gattc_read_t;

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_GATTC_WRITE request type. */
typedef ble_gattc_write_params_t nrf_ble_gq_gattc_write_t;

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_SRV_DISCOVERY request type. */
typedef struct
{
    uint16_t   start_handle;    /**< The start handle value used during service discovery. */
    ble_uuid_t srvc_uuid;       /**< The service UUID to be found. */
} nrf_ble_gq_gattc_srv_discovery_t;

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_CHAR_DISCOVERY request type. */
typedef ble_gattc_handle_range_t nrf_ble_gq_gattc_char_disc_t;

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_DESC_DISCOVERY request type. */
typedef ble_gattc_handle_range_t nrf_ble_gq_gattc_desc_disc_t;

/**@brief Structure used to describe @ref NRF_BLE_GQ_REQ_GATTS_HVX request type. */
typedef ble_gatts_hvx_params_t nrf_ble_gq_gatts_hvx_t;

/**@brief Structure used to handle SoftDevice error. */
typedef struct
{
    nrf_ble_gq_req_error_cb_t   cb;    /**< Error handler to be called in case of an error from SoftDevice. */
    void                      * p_ctx; /**< Parameter to the error handler. */
} nrf_ble_gq_req_error_handler_t;


/**@brief Structure used to describe BLE GATT request. */
typedef struct
{
    nrf_ble_gq_req_type_t            type;          /**< Type of request. */
    nrf_memobj_t                   * p_mem_obj;     /**< Memory object for data that cannot be contained in request descriptor. */
    nrf_ble_gq_req_error_handler_t   error_handler; /**< Error handler structure. */
    union
    {
        nrf_ble_gq_gattc_read_t          gattc_read;      /**< GATTC read parameters. Filled when nrf_ble_gq_req_t::type is @ref NRF_BLE_GQ_REQ_GATTC_READ. */
        nrf_ble_gq_gattc_write_t         gattc_write;     /**< GATTC write parameters. Filled when nrf_ble_gq_req_t::type is @ref NRF_BLE_GQ_REQ_GATTC_WRITE. */
        nrf_ble_gq_gattc_srv_discovery_t gattc_srv_disc;  /**< GATTC Service discovery parameters. Filled when nrf_ble_gq_req_t::type is @ref NRF_BLE_GQ_REQ_SRV_DISCOVERY. */
        nrf_ble_gq_gattc_char_disc_t     gattc_char_disc; /**< GATTC characteristic discovery parameters. Filled when nrf_ble_gq_req_t::type is @ref NRF_BLE_GQ_REQ_CHAR_DISCOVERY. */
        nrf_ble_gq_gattc_desc_disc_t     gattc_desc_disc; /**< GATTC characteristic descriptor discovery parameters. Filled when nrf_ble_gq_req_t::type is NRF_BLE_GQ_REQ_DESC_DISCOVERY. */
        nrf_ble_gq_gatts_hvx_t           gatts_hvx;       /**< GATTS Handle Value Notification or Indication Parameters. Filled when nrf_ble_gq_req_t::type is @ref NRF_BLE_GQ_REQ_GATTS_HVX. */
    } params;
} nrf_ble_gq_req_t;

/**@brief Descriptor for the BLE GATT Queue instance. */
typedef struct
{
    uint16_t            const max_conns;      /**< Maximal number of connection handles that can be registered. */
    uint16_t                * p_conn_handles; /**< Pointer to array with registered connection handles.*/
    nrf_queue_t const * const p_req_queue;    /**< Pointer to array of queue instances used to hold nrf_ble_gq_req_t instances.*/
    nrf_queue_t const * const p_purge_queue;  /**< Pointer to the queue instance used to hold indexes of queues to purge.*/
    nrf_memobj_pool_t const * p_data_pool;    /**< Memory pool used to obtain nrf_memobj_t instances.*/
} nrf_ble_gq_t;


/**@brief Function for adding a GATT request to the BGQ instance.
 *
 * @details This function adds a request to the BGQ instance and allocates necessary memory
 *          for data that can be held within the request descriptor. If the SoftDevice is free,
 *          this request will be processed immediately. Otherwise, the request remains in
 *          in the queue and is processed later.
 *
 * @param[in] p_gatt_queue  Pointer to the BGQ instance.
 * @param[in] p_req         Pointer to the request.
 * @param[in] conn_handle   Connection handle associated with the request.
 *
 * @retval    NRF_SUCCESS             If the request was added successfully.
 * @retval    NRF_ERROR_NULL          Any parameter was NULL.
 * @retval    NRF_ERROR_NO_MEM        There was no room in the queue or in the data pool.
 * @retval    NRF_ERROR_INVALID_PARAM If \p conn_handle is not registered or type of request -
 *                                    \p p_req is not valid.
 * @retval    err_code				  Other request specific error codes may be returned.
 */
ret_code_t nrf_ble_gq_item_add(nrf_ble_gq_t const * const p_gatt_queue,
                               nrf_ble_gq_req_t   * const p_req,
                               uint16_t                   conn_handle);


/**@brief Function for registering connection handle in the BGQ instance.
 *
 * @details This function is used for registering connection handle in the BGQ instance. From this
 *          point, the BGQ instance can handle GATT requests associated with the handle until connection
 *          is no longer valid (disconnect event occurs).
 *
 * @param[in] p_gatt_queue  Pointer to the BGQ instance.
 * @param[in] conn_handle   Connection handle.
 *
 * @retval    NRF_SUCCESS      If the registration was successful.
 * @retval    NRF_ERROR_NULL   If \p p_gatt_queue was NULL.
 * @retval    NRF_ERROR_NO_MEM If there was no space for another connection handle.
 */
ret_code_t nrf_ble_gq_conn_handle_register(nrf_ble_gq_t * const p_gatt_queue, uint16_t conn_handle);


/**@brief     Function for handling BLE events from the SoftDevice.
 *
 * @details   This function handles the BLE events received from the SoftDevice. If a BLE
 *            event is relevant to the BGQ module, it is used to update internal variables,
 *            process queued GATT requests and, if necessary, send errors to the application.
 *
 * @param[in] p_ble_evt   Pointer to the BLE event.
 * @param[in] p_context   Pointer to the BGQ instance.
 */
void nrf_ble_gq_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context);


#ifdef __cplusplus
}
#endif

#endif // NRF_BLE_GQ_H__

/** @} */
