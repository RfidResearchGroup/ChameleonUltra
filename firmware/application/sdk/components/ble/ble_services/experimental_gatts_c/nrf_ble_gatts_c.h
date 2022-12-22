/**
 * Copyright (c) 2017 - 2021, Nordic Semiconductor ASA
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
 * @defgroup nrf_ble_gatts_c GATT Service Client
 * @{
 * @ingroup ble_sdk_srv
 * @brief GATT Service Client module.
 *
 * @details This module implements a client for the Generic Attribute Profile (GATT) Service.
 *          It subscribes to indications from the Service Changed characteristic (0x2A05).
 *
 * @note    The application must register this module as a BLE event observer with the
 *          NRF_SDH_BLE_OBSERVER macro. Example:
 *          @code
 *              ble_gatts_t instance;
 *              NRF_SDH_BLE_OBSERVER(anything, BLE_GATTS_BLE_OBSERVER_PRIO,
 *                                   nrf_ble_gatts_c_on_ble_evt, &instance);
 *          @endcode
*/


#ifndef NRF_BLE_GATTS_C_H__
#define NRF_BLE_GATTS_C_H__

#include <stdint.h>
#include "ble_gattc.h"
#include "ble.h"
#include "nrf_error.h"
#include "ble_srv_common.h"
#include "ble_db_discovery.h"
#include "nrf_ble_gq.h"
#include "sdk_errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@brief   Macro for defining a nrf_ble_gatts_c instance.
 *
 * @param   _name   Name of the instance.
 * @hideinitializer
 */
#define NRF_BLE_GATTS_C_DEF(_name)                                                                  \
static nrf_ble_gatts_c_t _name;                                                                     \
NRF_SDH_BLE_OBSERVER(_name ## _obs,                                                                 \
                     NRF_BLE_GATTS_C_BLE_OBSERVER_PRIO,                                             \
                     nrf_ble_gatts_c_on_ble_evt, &_name)

/** @brief Macro for defining multiple nrf_ble_gatts_c instances.
 *
 * @param   _name   Name of the array of instances.
 * @param   _cnt    Number of instances to define.
 * @hideinitializer
 */
#define NRF_BLE_GATTS_C_ARRAY_DEF(_name, _cnt)                 \
static nrf_ble_gatts_c_t _name[_cnt];                          \
NRF_SDH_BLE_OBSERVERS(_name ## _obs,                           \
                      NRF_BLE_GATTS_C_BLE_OBSERVER_PRIO,       \
                      nrf_ble_gatts_c_on_ble_evt, &_name, _cnt)

/**@brief   Type of the GATT Service client event. */
typedef enum
{
    NRF_BLE_GATTS_C_EVT_DISCOVERY_COMPLETE, /**< Event indicating that the GATT Service and Service Changed characteristic have been found on the peer. */
    NRF_BLE_GATTS_C_EVT_DISCOVERY_FAILED,   /**< Event indicating that the Service Changed characteristic has not been found on the peer. */
    NRF_BLE_GATTS_C_EVT_DISCONN_COMPLETE,   /**< Event indicating that the GATT Service client module finished processing the BLE_GAP_EVT_DISCONNECTED event. The application can use this event to do a cleanup related to the GATT Service client.*/
    NRF_BLE_GATTS_C_EVT_SRV_CHANGED,        /**< Event indicating that a Service Changed indication has been received. */
} nrf_ble_gatts_c_evt_type_t;

/**@brief   Structure containing the event from the Service Changed client module to the application.
 */
typedef struct
{
    nrf_ble_gatts_c_evt_type_t evt_type;           /**< Type of event. See @ref nrf_ble_gatts_c_evt_type_t. */
    uint16_t                   conn_handle;        /**< Handle of the connection for which this event occurred. */
    union
    {
        ble_gatt_db_char_t       srv_changed_char;   /**< Handles for the Service Changed characteristic. This is filled if the event type is @ref NRF_BLE_GATTS_C_EVT_DISCOVERY_COMPLETE. */
        ble_gattc_handle_range_t handle_range;       /**< The affected attribute handle range in which the service has changed. This will be provided if the event type is @ref NRF_BLE_GATTS_C_EVT_SRV_CHANGED.*/
    } params;
} nrf_ble_gatts_c_evt_t;

/**@brief   Service Changed handler type. */
typedef void (* nrf_ble_gatts_c_evt_handler_t)(nrf_ble_gatts_c_evt_t * p_evt);

/**@brief   Structure for holding the information related to the Service Changed indication at the server.
 *
 * @details A GATT Server never has more than one instance of the Service Changed characteristic.
 *          For this reason, you never need more than one instance of the GATT Service Client structure.
 *
 * @warning This structure must be zero-initialized.
 */
typedef struct
{
    bool                            initialized;      /**< Boolean indicating whether the context has been initialized. */
    bool                            char_found;       /**< Boolean indicating whether the Service Changed indication has been found.*/
    ble_gatt_db_char_t              srv_changed_char; /**< Information of the Service Changed characteristics. */
    uint16_t                        conn_handle;      /**< Active connection handle. */
    nrf_ble_gatts_c_evt_handler_t   evt_handler;      /**< Pointer to event handler function. */
    ble_srv_error_handler_t         err_handler;      /**< Pointer to error handler function. */
    nrf_ble_gq_t                  * p_gatt_queue;     /**< Pointer to the BLE GATT Queue instance. */
} nrf_ble_gatts_c_t;

/**@brief   Initialization parameters. These must be supplied when calling @ref nrf_ble_gatts_c_init. */
typedef struct
{
    nrf_ble_gatts_c_evt_handler_t   evt_handler; /**< Event handler that is called by the Service Changed Client module when any related event occurs. */
    ble_srv_error_handler_t         err_handler; /**< Error handler that is called by the Service Changed Client module if any error occurs. */
    nrf_ble_gq_t                  * p_gatt_queue;     /**< Pointer to the BLE GATT Queue instance. */
} nrf_ble_gatts_c_init_t;


/**@brief   Function for initializing the Service Changed client module.
 *
 * @param[in,out] p_gatts_c         Pointer to the GATT Service Client structure instance.
 * @param[in]     p_gatts_c_init    Init parameters containing the event handler that is called by
 *                                  the Service Changed Client module when any related event occurs.
 *
 * @retval  NRF_SUCCESS     If the service was initialized successfully.
 * @retval  NRF_ERROR_NULL  If any of the input parameters are NULL.
 * @retval  err_code        If functions from other modules return errors to this function,
 *                          the @ref nrf_error are propagated.
 */
ret_code_t nrf_ble_gatts_c_init(nrf_ble_gatts_c_t      * p_gatts_c,
                                nrf_ble_gatts_c_init_t * p_gatts_c_init);


/**@brief Function for enabling remote indication.
 *
 * @param[in,out]   p_gatts_c   Pointer to the Service Changed Client structure.
 * @param[in]       enable      True to enable Service Changed remote indication; false to disable.
 *
 * @retval NRF_SUCCESS Operation success.
 * @retval err_code    Otherwise, this API propagates the error code 
 *                     returned by function @ref nrf_ble_gq_item_add.
 */
ret_code_t nrf_ble_gatts_c_enable_indication(nrf_ble_gatts_c_t * p_gatts_c,
                                             bool                enable);


/**@brief   Function for handling events from the Database Discovery module.
 *
 * @details This function handles an event from the Database Discovery module, and determines
 *          whether it relates to the discovery of Service Changed characteristics at the peer. If it does,
 *          the function calls the application's event handler to indicate that the Service Changed
 *          characteristic has been discovered at the peer.
 *
 * @param[in,out] p_gatts_c     Pointer to the GATT Service Client structure instance.
 * @param[in]     p_evt         Pointer to the event received from the Database Discovery module.
 */
void nrf_ble_gatts_c_on_db_disc_evt(nrf_ble_gatts_c_t      const * p_gatts_c,
                                    ble_db_discovery_evt_t       * p_evt);


/**@brief   Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Event received from the BLE stack.
 * @param[in]   p_context   Context.
 */
void nrf_ble_gatts_c_on_ble_evt(ble_evt_t const * p_ble_evt,
                                void            * p_context);


/**@brief   Function for assigning handles to a GATT Service Client instance.
 *
 * @details Call this function when a link has been established with a peer to
 *          associate the link to an instance of the module. This makes it
 *          possible to handle several links and associate each link with a particular
 *          instance of the GATT Service Client module. The connection handle and attribute
 *          handles will be provided from the discovery event
 *          @ref NRF_BLE_GATTS_C_EVT_DISCOVERY_COMPLETE.
 *
 * @param[in,out]   p_gatts_c       Pointer to the GATT Service Client structure instance for
 *                                  associating the link.
 * @param[in]       conn_handle     Connection handle to be associated with the given
 *                                  GATT Service Client instance.
 * @param[in]       p_peer_handles  Attribute handles on the GATT Service server that you want this
 *                                  GATT Service Client to interact with.
 *
 * @retval NRF_SUCCESS    If the connection handle was successfully stored in the GATT Service instance.
 * @retval NRF_ERROR_NULL If any of the input parameters are NULL.
 * @retval err_code       Otherwise, this API propagates the error code returned by function
 *                        @ref nrf_ble_gq_item_add.   
 */
ret_code_t nrf_ble_gatts_c_handles_assign(nrf_ble_gatts_c_t        * p_gatts_c,
                                          uint16_t                   conn_handle,
                                          ble_gatt_db_char_t const * p_peer_handles);

#ifdef __cplusplus
}
#endif

#endif // NRF_BLE_GATTS_C_H__

/** @} */
