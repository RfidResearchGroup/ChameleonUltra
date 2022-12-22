/**
 * Copyright (c) 2014 - 2021, Nordic Semiconductor ASA
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
 * @defgroup ble_cts_c Current Time Service Client
 * @{
 * @ingroup ble_sdk_srv
 * @brief Current Time Service Client module.
 *
 * @details This module implements the Current Time Service (CTS) client-peripheral role of
 *          the Time Profile. After security is established, the module tries to discover the
 *          Current Time Service and its characteristic on the central side. If this succeeds,
 *          the application can trigger a read of the current time from the connected server.
 *
 *          The module informs the application about the successful discovery with the
 *          @ref BLE_CTS_C_EVT_DISCOVERY_COMPLETE event. The handles for the CTS server are now
 *          available in the @ref ble_cts_c_evt_t structure. These handles must be assigned to an
 *          instance of CTS_C with @ref ble_cts_c_handles_assign. For more information about the
 *          service discovery, see the ble_discovery module documentation: @ref lib_ble_db_discovery.
 *
 *          After assigning the handles to an instance of CTS_C, the application can use the function 
 *          @ref ble_cts_c_current_time_read to read the current time. If the read succeeds, it triggers either
 *          a @ref BLE_CTS_C_EVT_CURRENT_TIME event or a @ref BLE_CTS_C_EVT_INVALID_TIME event
 *          (depending whether the data that was read was actually a valid time). Then the read result is sent
 *          to the application. The current time is then available in the params field of the
 *          passed @ref ble_cts_c_evt_t structure.
 *
 * @note    The application must register this module as the BLE event observer by using the
 *          NRF_SDH_BLE_OBSERVER macro. Example:
 *          @code
 *              ble_cts_c_t instance;
 *              NRF_SDH_BLE_OBSERVER(anything, BLE_CTS_C_BLE_OBSERVER_PRIO,
 *                                   ble_cts_c_on_ble_evt, &instance);
 *          @endcode
 */

#ifndef BLE_CTS_C_H__
#define BLE_CTS_C_H__

#include "ble_srv_common.h"
#include "ble_gattc.h"
#include "ble.h"
#include "ble_date_time.h"
#include "ble_db_discovery.h"
#include "nrf_ble_gq.h"
#include "nrf_sdh_ble.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**@brief   Macro for defining a ble_bps instance.
 *
 * @param   _name   Name of the instance.
 * @hideinitializer
 */
#define BLE_CTS_C_DEF(_name)                                                                        \
static ble_cts_c_t _name;                                                                           \
NRF_SDH_BLE_OBSERVER(_name ## _obs,                                                                 \
                     BLE_CTS_C_BLE_OBSERVER_PRIO,                                                   \
                     ble_cts_c_on_ble_evt, &_name)

/** @brief Macro for defining multiple ble_cts_c instances.
 *
 * @param   _name   Name of the array of instances.
 * @param   _cnt    Number of instances to define.
 * @hideinitializer
 */
#define BLE_CTS_C_ARRAY_DEF(_name, _cnt)                 \
static ble_cts_c_t _name[_cnt];                          \
NRF_SDH_BLE_OBSERVERS(_name ## _obs,                     \
                      BLE_CTS_C_BLE_OBSERVER_PRIO,       \
                      ble_cts_c_on_ble_evt, &_name, _cnt)


/**@brief "Day Date Time" field of the "Exact Time 256" field of the Current Time characteristic. */
typedef struct
{
    ble_date_time_t date_time;
    uint8_t         day_of_week;
} day_date_time_t;

/**@brief "Exact Time 256" field of the Current Time characteristic. */
typedef struct
{
    day_date_time_t day_date_time;
    uint8_t         fractions256;
} exact_time_256_t;

/**@brief "Adjust Reason" field of the Current Time characteristic. */
typedef struct
{
    uint8_t manual_time_update              : 1;
    uint8_t external_reference_time_update  : 1;
    uint8_t change_of_time_zone             : 1;
    uint8_t change_of_daylight_savings_time : 1;
} adjust_reason_t;

/**@brief Data structure for the Current Time characteristic. */
typedef struct
{
    exact_time_256_t exact_time_256;
    adjust_reason_t  adjust_reason;
} current_time_char_t;

// Forward declaration of the ble_cts_c_t type.
typedef struct ble_cts_c_s ble_cts_c_t;

/**@brief Current Time Service client event type. */
typedef enum
{
    BLE_CTS_C_EVT_DISCOVERY_COMPLETE, /**< The Current Time Service was found at the peer. */
    BLE_CTS_C_EVT_DISCOVERY_FAILED,   /**< The Current Time Service was not found at the peer. */
    BLE_CTS_C_EVT_DISCONN_COMPLETE,   /**< Event indicating that the Current Time Service Client module finished processing the BLE_GAP_EVT_DISCONNECTED event. This event is triggered only if a valid instance of the Current Time Service was found at the server. The application can use this event to do a cleanup related to the Current Time Service client.*/
    BLE_CTS_C_EVT_CURRENT_TIME,       /**< A new Current Time reading has been received. */
    BLE_CTS_C_EVT_INVALID_TIME        /**< The Current Time value received from the peer is invalid.*/
} ble_cts_c_evt_type_t;

/**@brief Structure containing the handles related to the Heart Rate Service found on the peer. */
typedef struct
{
    uint16_t cts_handle;       /**< Handle of the Current Time characteristic, as provided by the SoftDevice. */
    uint16_t cts_cccd_handle;  /**< Handle of the CCCD of the Current Time characteristic. */
} ble_cts_c_handles_t;

/**@brief Current Time Service client event. */
typedef struct
{
    ble_cts_c_evt_type_t evt_type; /**< Type of event. */
    uint16_t             conn_handle; /**< Connection handle on which the CTS service was discovered on the peer device. This is filled if the evt_type is @ref BLE_CTS_C_EVT_DISCOVERY_COMPLETE.*/
    union
    {
        current_time_char_t current_time; /**< Current Time characteristic data. This is filled when the evt_type is @ref BLE_CTS_C_EVT_CURRENT_TIME. */
        ble_cts_c_handles_t char_handles;  /**< Handles related to Current Time, found on the peer device. This is filled when the evt_type is @ref BLE_HRS_C_EVT_DISCOVERY_COMPLETE.*/
    } params;
} ble_cts_c_evt_t;

/**@brief Current Time Service client event handler type. */
typedef void (* ble_cts_c_evt_handler_t) (ble_cts_c_t * p_cts, ble_cts_c_evt_t * p_evt);


/**@brief Current Time Service client structure. This structure contains status information for the client. */
struct ble_cts_c_s
{
    ble_cts_c_evt_handler_t   evt_handler;         /**< Event handler to be called for handling events from the Current Time Service client. */
    ble_srv_error_handler_t   error_handler;       /**< Function to be called if an error occurs. */
    ble_cts_c_handles_t       char_handles;        /**< Handles of Current Time characteristic at the peer. (Handles are provided by the BLE stack through the Database Discovery module.) */
    uint16_t                  conn_handle;         /**< Handle of the current connection. BLE_CONN_HANDLE_INVALID if not in a connection. */
    nrf_ble_gq_t            * p_gatt_queue;        /**< Pointer to the BLE GATT Queue instance. */
};

/**@brief Current Time Service client init structure. This structure contains all options and data needed for the initialization of the client.*/
typedef struct
{
    ble_cts_c_evt_handler_t   evt_handler;   /**< Event handler to be called for handling events from the Current Time Service client. */
    ble_srv_error_handler_t   error_handler; /**< Function to be called if an error occurs. */
    nrf_ble_gq_t            * p_gatt_queue;  /**< Pointer to the BLE GATT Queue instance. */
} ble_cts_c_init_t;


/**@brief Function for initializing the Current Time Service client.
 *
 * @details This function must be used by the application to initialize the Current Time Service client.
 *
 * @param[out] p_cts Current Time Service client structure. This structure must
 *                   be supplied by the application. It is initialized by this
 *                   function and can later be used to identify this particular client
 *                   instance.
 * @param[in]  p_cts_init Information needed to initialize the Current Time Service client.
 *
 * @retval NRF_SUCCESS If the service was initialized successfully.
 */
uint32_t ble_cts_c_init(ble_cts_c_t * p_cts, const ble_cts_c_init_t * p_cts_init);


/**@brief Function for handling events from the Database Discovery module.
 *
 * @details This function handles an event from the Database Discovery module, and determines
 *          whether it relates to the discovery of CTS at the peer. If it does, the function
 *          calls the application's event handler to indicate that CTS was
 *          discovered. The function also populates the event with service-related
 *          information before providing it to the application.
 *
 * @param[in] p_cts  Pointer to the CTS client structure.
 * @param[in] p_evt  Pointer to the event received from the Database Discovery module.
 */
 void ble_cts_c_on_db_disc_evt(ble_cts_c_t * p_cts, ble_db_discovery_evt_t * p_evt);


/**@brief Function for handling the application's BLE stack events.
 *
 * @details This function handles all events from the BLE stack that are of interest to the
 *          Current Time Service client. This is a callback function that must be dispatched
 *          from the main application context.
 *
 * @param[in] p_ble_evt     Event received from the BLE stack.
 * @param[in] p_context     Current Time Service client structure.
 */
void ble_cts_c_on_ble_evt(ble_evt_t const * p_ble_evt, void * p_context);


/**@brief Function for checking whether the peer's Current Time Service instance and the Current Time
 *        Characteristic have been discovered.
 *
 * @param[in] p_cts  Current Time Service client structure.
 */
static __INLINE bool ble_cts_c_is_cts_discovered(const ble_cts_c_t * p_cts)
{
    return (p_cts->char_handles.cts_handle != BLE_GATT_HANDLE_INVALID);
}


/**@brief Function for reading the peer's Current Time Service Current Time characteristic.
 *
 * @param[in] p_cts  Current Time Service client structure.
 *
 * @retval NRF_SUCCESS If the operation is successful.
 * @retval err_code    Otherwise, this API propagates the error code returned by function
 *                     @ref nrf_ble_gq_item_add.
 */
uint32_t ble_cts_c_current_time_read(ble_cts_c_t const * p_cts);


/**@brief Function for assigning handles to this instance of cts_c.
 *
 * @details Call this function when a link has been established with a peer to
 *          associate the link to this instance of the module. This association makes it
 *          possible to handle several links and associate each link to a particular
 *          instance of this module. The connection handle and attribute handles are
 *          provided from the discovery event @ref BLE_CTS_C_EVT_DISCOVERY_COMPLETE.
 *
 * @param[in] p_cts          Pointer to the CTS client structure instance for associating the link.
 * @param[in] conn_handle    Connection handle to associate with the given CTS instance.
 * @param[in] p_peer_handles Attribute handles for the CTS server you want this CTS client to
 *                           interact with.
 *
 * @retval NRF_SUCCESS    If the operation was successful.
 * @retval NRF_ERROR_NULL If a p_cts was a NULL pointer.
 * @retval err_code       Otherwise, this API propagates the error code returned by function
 *                        @ref nrf_ble_gq_conn_handle_register.
 */
uint32_t ble_cts_c_handles_assign(ble_cts_c_t               * p_cts,
                                  const uint16_t              conn_handle,
                                  const ble_cts_c_handles_t * p_peer_handles);


#ifdef __cplusplus
}
#endif

#endif // BLE_CTS_C_H__

/** @} */
