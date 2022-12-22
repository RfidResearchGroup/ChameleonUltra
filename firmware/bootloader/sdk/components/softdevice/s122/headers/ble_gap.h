/*
 * Copyright (c) Nordic Semiconductor ASA
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
 */

/**
  @addtogroup BLE_GAP Generic Access Profile (GAP)
  @{
  @brief Definitions and prototypes for the GAP interface.
 */

#ifndef BLE_GAP_H__
#define BLE_GAP_H__

#include <stdint.h>
#include "nrf_svc.h"
#include "nrf_error.h"
#include "ble_hci.h"
#include "ble_ranges.h"
#include "ble_types.h"
#include "ble_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**@addtogroup BLE_GAP_ENUMERATIONS Enumerations
 * @{ */

/**@brief GAP API SVC numbers.
 */
enum BLE_GAP_SVCS
{
  SD_BLE_GAP_ADDR_SET                   = BLE_GAP_SVC_BASE,       /**< Set own Bluetooth Address. */
  SD_BLE_GAP_ADDR_GET                   = BLE_GAP_SVC_BASE + 1,   /**< Get own Bluetooth Address. */
  SD_BLE_GAP_WHITELIST_SET              = BLE_GAP_SVC_BASE + 3,   /**< Set active whitelist. */
  SD_BLE_GAP_DEVICE_IDENTITIES_SET      = BLE_GAP_SVC_BASE + 4,   /**< Set device identity list. */
  SD_BLE_GAP_PRIVACY_SET                = BLE_GAP_SVC_BASE + 5,   /**< Set Privacy settings*/
  SD_BLE_GAP_PRIVACY_GET                = BLE_GAP_SVC_BASE + 6,   /**< Get Privacy settings*/
  SD_BLE_GAP_CONN_PARAM_UPDATE          = BLE_GAP_SVC_BASE + 10,  /**< Connection Parameter Update. */
  SD_BLE_GAP_DISCONNECT                 = BLE_GAP_SVC_BASE + 11,  /**< Disconnect. */
  SD_BLE_GAP_TX_POWER_SET               = BLE_GAP_SVC_BASE + 12,  /**< Set TX Power. */
  SD_BLE_GAP_APPEARANCE_SET             = BLE_GAP_SVC_BASE + 13,  /**< Set Appearance. */
  SD_BLE_GAP_APPEARANCE_GET             = BLE_GAP_SVC_BASE + 14,  /**< Get Appearance. */
  SD_BLE_GAP_DEVICE_NAME_SET            = BLE_GAP_SVC_BASE + 17,  /**< Set Device Name. */
  SD_BLE_GAP_DEVICE_NAME_GET            = BLE_GAP_SVC_BASE + 18,  /**< Get Device Name. */
  SD_BLE_GAP_AUTHENTICATE               = BLE_GAP_SVC_BASE + 19,  /**< Initiate Pairing/Bonding. */
  SD_BLE_GAP_SEC_PARAMS_REPLY           = BLE_GAP_SVC_BASE + 20,  /**< Reply with Security Parameters. */
  SD_BLE_GAP_AUTH_KEY_REPLY             = BLE_GAP_SVC_BASE + 21,  /**< Reply with an authentication key. */
  SD_BLE_GAP_LESC_DHKEY_REPLY           = BLE_GAP_SVC_BASE + 22,  /**< Reply with an LE Secure Connections DHKey. */
  SD_BLE_GAP_KEYPRESS_NOTIFY            = BLE_GAP_SVC_BASE + 23,  /**< Notify of a keypress during an authentication procedure. */
  SD_BLE_GAP_LESC_OOB_DATA_GET          = BLE_GAP_SVC_BASE + 24,  /**< Get the local LE Secure Connections OOB data. */
  SD_BLE_GAP_LESC_OOB_DATA_SET          = BLE_GAP_SVC_BASE + 25,  /**< Set the remote LE Secure Connections OOB data. */
  SD_BLE_GAP_ENCRYPT                    = BLE_GAP_SVC_BASE + 26,  /**< Initiate encryption procedure. */
  SD_BLE_GAP_CONN_SEC_GET               = BLE_GAP_SVC_BASE + 28,  /**< Obtain connection security level. */
  SD_BLE_GAP_SCAN_START                 = BLE_GAP_SVC_BASE + 29,  /**< Start Scanning. */
  SD_BLE_GAP_SCAN_STOP                  = BLE_GAP_SVC_BASE + 30,  /**< Stop Scanning. */
  SD_BLE_GAP_CONNECT                    = BLE_GAP_SVC_BASE + 31,  /**< Connect. */
  SD_BLE_GAP_CONNECT_CANCEL             = BLE_GAP_SVC_BASE + 32,  /**< Cancel ongoing connection procedure. */
  SD_BLE_GAP_PHY_UPDATE                 = BLE_GAP_SVC_BASE + 33,  /**< Initiate or respond to a PHY Update Procedure. */
  SD_BLE_GAP_RSSI_GET                   = BLE_GAP_SVC_BASE + 35,  /**< Get the last RSSI sample. */
  SD_BLE_GAP_QOS_START                  = BLE_GAP_SVC_BASE + 36,  /**< Start Quality of Service (QoS) reporting. */
  SD_BLE_GAP_QOS_STOP                   = BLE_GAP_SVC_BASE + 37,  /**< Stop Quality of Service (QoS) reporting. */
  SD_BLE_GAP_NEXT_CONN_EVT_COUNTER_GET  = BLE_GAP_SVC_BASE + 39,  /**< Get the next connection event counter. */
  SD_BLE_GAP_EVT_TRIGGER_START          = BLE_GAP_SVC_BASE + 40,  /**< Start triggering a given task on the start of a Bluetooth Link Layer event. */
  SD_BLE_GAP_EVT_TRIGGER_STOP           = BLE_GAP_SVC_BASE + 41,  /**< Stop triggering the task configured using @ref sd_ble_gap_evt_trigger_start. */
};

/**@brief GAP Event IDs.
 * IDs that uniquely identify an event coming from the stack to the application.
 */
enum BLE_GAP_EVTS
{
  BLE_GAP_EVT_CONNECTED                   = BLE_GAP_EVT_BASE,       /**< Connected to peer.                              \n See @ref ble_gap_evt_connected_t             */
  BLE_GAP_EVT_DISCONNECTED                = BLE_GAP_EVT_BASE + 1,   /**< Disconnected from peer.                         \n See @ref ble_gap_evt_disconnected_t.         */
  BLE_GAP_EVT_CONN_PARAM_UPDATE           = BLE_GAP_EVT_BASE + 2,   /**< Connection Parameters updated.                  \n See @ref ble_gap_evt_conn_param_update_t.    */
  BLE_GAP_EVT_SEC_PARAMS_REQUEST          = BLE_GAP_EVT_BASE + 3,   /**< Request to provide security parameters.         \n Reply with @ref sd_ble_gap_sec_params_reply.  \n See @ref ble_gap_evt_sec_params_request_t. */
  BLE_GAP_EVT_PASSKEY_DISPLAY             = BLE_GAP_EVT_BASE + 5,   /**< Request to display a passkey to the user.       \n In LESC Numeric Comparison, reply with @ref sd_ble_gap_auth_key_reply. \n See @ref ble_gap_evt_passkey_display_t. */
  BLE_GAP_EVT_KEY_PRESSED                 = BLE_GAP_EVT_BASE + 6,   /**< Notification of a keypress on the remote device.\n See @ref ble_gap_evt_key_pressed_t           */
  BLE_GAP_EVT_AUTH_KEY_REQUEST            = BLE_GAP_EVT_BASE + 7,   /**< Request to provide an authentication key.       \n Reply with @ref sd_ble_gap_auth_key_reply.    \n See @ref ble_gap_evt_auth_key_request_t.   */
  BLE_GAP_EVT_LESC_DHKEY_REQUEST          = BLE_GAP_EVT_BASE + 8,   /**< Request to calculate an LE Secure Connections DHKey. \n Reply with @ref sd_ble_gap_lesc_dhkey_reply.  \n See @ref ble_gap_evt_lesc_dhkey_request_t */
  BLE_GAP_EVT_AUTH_STATUS                 = BLE_GAP_EVT_BASE + 9,   /**< Authentication procedure completed with status. \n See @ref ble_gap_evt_auth_status_t.          */
  BLE_GAP_EVT_CONN_SEC_UPDATE             = BLE_GAP_EVT_BASE + 10,  /**< Connection security updated.                    \n See @ref ble_gap_evt_conn_sec_update_t.      */
  BLE_GAP_EVT_TIMEOUT                     = BLE_GAP_EVT_BASE + 11,  /**< Timeout expired.                                \n See @ref ble_gap_evt_timeout_t.              */
  BLE_GAP_EVT_RSSI_CHANGED                = BLE_GAP_EVT_BASE + 12,  /**< RSSI report.                                    \n See @ref ble_gap_evt_rssi_changed_t.         */
  BLE_GAP_EVT_ADV_REPORT                  = BLE_GAP_EVT_BASE + 13,  /**< Advertising report.                             \n See @ref ble_gap_evt_adv_report_t.           */
  BLE_GAP_EVT_SEC_REQUEST                 = BLE_GAP_EVT_BASE + 14,  /**< Security Request.                               \n Reply with @ref sd_ble_gap_authenticate \n or with @ref sd_ble_gap_encrypt if required security information is available.    \n See @ref ble_gap_evt_sec_request_t.          */
  BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST   = BLE_GAP_EVT_BASE + 15,  /**< Connection Parameter Update Request.            \n Reply with @ref sd_ble_gap_conn_param_update. \n See @ref ble_gap_evt_conn_param_update_request_t. */
  BLE_GAP_EVT_PHY_UPDATE_REQUEST          = BLE_GAP_EVT_BASE + 17,  /**< PHY Update Request.                             \n Reply with @ref sd_ble_gap_phy_update. \n See @ref ble_gap_evt_phy_update_request_t. */
  BLE_GAP_EVT_PHY_UPDATE                  = BLE_GAP_EVT_BASE + 18,  /**< PHY Update Procedure is complete.               \n See @ref ble_gap_evt_phy_update_t.           */
  BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT  = BLE_GAP_EVT_BASE + 21,   /**< Channel survey report.                          \n See @ref ble_gap_evt_qos_channel_survey_report_t. */
  BLE_GAP_EVT_QOS_CONN_EVENT_REPORT      = BLE_GAP_EVT_BASE + 23,   /**< Connection event QoS report.                    \n See @ref ble_gap_evt_qos_conn_event_report_t. */
};

/**@brief GAP Option IDs.
 * IDs that uniquely identify a GAP option.
 */
enum BLE_GAP_OPTS
{
  BLE_GAP_OPT_CH_MAP                  = BLE_GAP_OPT_BASE,       /**< Channel Map. @ref ble_gap_opt_ch_map_t  */
  BLE_GAP_OPT_PASSKEY                 = BLE_GAP_OPT_BASE + 2,   /**< Set passkey. @ref ble_gap_opt_passkey_t */
  BLE_GAP_OPT_COMPAT_MODE_1           = BLE_GAP_OPT_BASE + 3,   /**< Compatibility mode. @ref ble_gap_opt_compat_mode_1_t */
  BLE_GAP_OPT_AUTH_PAYLOAD_TIMEOUT    = BLE_GAP_OPT_BASE + 4,   /**< Set Authenticated payload timeout. @ref ble_gap_opt_auth_payload_timeout_t */
  BLE_GAP_OPT_ENABLE_EVT_LEN_CHECK    = BLE_GAP_OPT_BASE + 7,   /**< Allow disabling event length checks. @ref ble_gap_opt_enable_evt_len_check_t */
};

/**@brief Quality of Service (QoS) IDs.
 * IDs that uniquely identify a QoS.
 */
enum BLE_GAP_QOS_IDS
{
  BLE_GAP_QOS_CHANNEL_SURVEY         = 0,                      /**< Channel survey. @ref ble_gap_qos_channel_survey_t */
  BLE_GAP_QOS_RSSI                   = 1,                      /**< Reporting changes in RSSI. @ref ble_gap_qos_rssi_t */
  BLE_GAP_QOS_CONN_EVENT             = 2,                      /**< Connection event QoS.
                                                                    It does not have any additional parameters in @ref ble_gap_qos_params_t.
                                                                    When started, @ref BLE_GAP_EVT_QOS_CONN_EVENT_REPORT will be generated on every connection event. */
};

/**@brief GAP Configuration IDs.
 *
 * IDs that uniquely identify a GAP configuration.
 */
enum BLE_GAP_CFGS
{
  BLE_GAP_CFG_ROLE_COUNT       = BLE_GAP_CFG_BASE,     /**< Role count configuration.  */
  BLE_GAP_CFG_DEVICE_NAME      = BLE_GAP_CFG_BASE + 1, /**< Device name configuration. */
  BLE_GAP_CFG_PPCP_INCL_CONFIG = BLE_GAP_CFG_BASE + 2, /**< Peripheral Preferred Connection Parameters characteristic
                                                            inclusion configuration. */
  BLE_GAP_CFG_CAR_INCL_CONFIG  = BLE_GAP_CFG_BASE + 3, /**< Central Address Resolution characteristic
                                                            inclusion configuration. */
};

/**@brief GAP TX Power roles.
 */
enum BLE_GAP_TX_POWER_ROLES
{
  BLE_GAP_TX_POWER_ROLE_SCAN_INIT = 2,           /**< Scanner and initiator role. */
  BLE_GAP_TX_POWER_ROLE_CONN      = 3,           /**< Connection role. */
};

/**@brief GAP LL roles.
 */
enum BLE_GAP_LL_ROLES
{
  BLE_GAP_LL_ROLE_SCAN  = 1,                     /**< Scanner role. */
  BLE_GAP_LL_ROLE_INIT  = 2,                     /**< Initiator role. */
  BLE_GAP_LL_ROLE_CONN  = 3,                     /**< Connected role. */
};

/** @} */

/**@addtogroup BLE_GAP_DEFINES Defines
 * @{ */

/**@defgroup BLE_ERRORS_GAP SVC return values specific to GAP
 * @{ */
#define BLE_ERROR_GAP_UUID_LIST_MISMATCH            (NRF_GAP_ERR_BASE + 0x000)  /**< UUID list does not contain an integral number of UUIDs. */
#define BLE_ERROR_GAP_INVALID_BLE_ADDR              (NRF_GAP_ERR_BASE + 0x002)  /**< The upper two bits of the address do not correspond to the specified address type. */
#define BLE_ERROR_GAP_WHITELIST_IN_USE              (NRF_GAP_ERR_BASE + 0x003)  /**< Attempt to modify the whitelist while already in use by another operation. */
#define BLE_ERROR_GAP_DEVICE_IDENTITIES_IN_USE      (NRF_GAP_ERR_BASE + 0x004)  /**< Attempt to modify the device identity list while already in use by another operation. */
#define BLE_ERROR_GAP_DEVICE_IDENTITIES_DUPLICATE   (NRF_GAP_ERR_BASE + 0x005)  /**< The device identity list contains entries with duplicate identity addresses. */
/**@} */


/**@defgroup BLE_GAP_ROLES GAP Roles
 * @{ */
#define BLE_GAP_ROLE_INVALID     0x0            /**< Invalid Role. */
#define BLE_GAP_ROLE_CENTRAL     0x2            /**< Central Role. */
/**@} */


/**@defgroup BLE_GAP_TIMEOUT_SOURCES GAP Timeout sources
 * @{ */
#define BLE_GAP_TIMEOUT_SRC_SCAN                       0x01 /**< Scanning timeout. */
#define BLE_GAP_TIMEOUT_SRC_CONN                       0x02 /**< Connection timeout. */
#define BLE_GAP_TIMEOUT_SRC_AUTH_PAYLOAD               0x03 /**< Authenticated payload timeout. */
/**@} */


/**@defgroup BLE_GAP_ADDR_TYPES GAP Address types
 * @{ */
#define BLE_GAP_ADDR_TYPE_PUBLIC                        0x00 /**< Public (identity) address.*/
#define BLE_GAP_ADDR_TYPE_RANDOM_STATIC                 0x01 /**< Random static (identity) address. */
#define BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE     0x02 /**< Random private resolvable address. */
#define BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE 0x03 /**< Random private non-resolvable address. */
/**@} */


/**@brief The default interval in seconds at which a private address is refreshed.  */
#define BLE_GAP_DEFAULT_PRIVATE_ADDR_CYCLE_INTERVAL_S (900) /* 15 minutes. */
/**@brief The maximum interval in seconds at which a private address can be refreshed.  */
#define BLE_GAP_MAX_PRIVATE_ADDR_CYCLE_INTERVAL_S     (41400) /* 11 hours 30 minutes. */


/** @brief BLE address length. */
#define BLE_GAP_ADDR_LEN (6)

/**@defgroup BLE_GAP_PRIVACY_MODES Privacy modes
 * @{ */
#define BLE_GAP_PRIVACY_MODE_OFF                       0x00 /**< Device will send and accept its identity address for its own address. */
#define BLE_GAP_PRIVACY_MODE_DEVICE_PRIVACY            0x01 /**< Device will send and accept only private addresses for its own address. */
#define BLE_GAP_PRIVACY_MODE_NETWORK_PRIVACY           0x02 /**< Device will send and accept only private addresses for its own address,
                                                                 and will not accept a peer using identity address as sender address when
                                                                 the peer IRK is exchanged, non-zero and added to the identity list. */
/**@} */

/** @brief Invalid power level. */
#define BLE_GAP_POWER_LEVEL_INVALID     127

/**@defgroup BLE_GAP_ADV_SET_DATA_SIZES Advertising data sizes.
 * @{ */
#define BLE_GAP_ADV_SET_DATA_SIZE_MAX                    (31)   /**< Maximum data length for an advertising set.
                                                                     If more advertising data is required, use extended advertising instead. */
/**@}. */

/** @brief Set ID not available in advertising report. */
#define BLE_GAP_ADV_REPORT_SET_ID_NOT_AVAILABLE                    0xFF

/**@defgroup BLE_GAP_AD_TYPE_DEFINITIONS GAP Advertising and Scan Response Data format
 * @note Found at https://www.bluetooth.org/Technical/AssignedNumbers/generic_access_profile.htm
 * @{ */
#define BLE_GAP_AD_TYPE_FLAGS                               0x01 /**< Flags for discoverability. */
#define BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_MORE_AVAILABLE   0x02 /**< Partial list of 16 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_16BIT_SERVICE_UUID_COMPLETE         0x03 /**< Complete list of 16 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_32BIT_SERVICE_UUID_MORE_AVAILABLE   0x04 /**< Partial list of 32 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_32BIT_SERVICE_UUID_COMPLETE         0x05 /**< Complete list of 32 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_MORE_AVAILABLE  0x06 /**< Partial list of 128 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_128BIT_SERVICE_UUID_COMPLETE        0x07 /**< Complete list of 128 bit service UUIDs. */
#define BLE_GAP_AD_TYPE_SHORT_LOCAL_NAME                    0x08 /**< Short local device name. */
#define BLE_GAP_AD_TYPE_COMPLETE_LOCAL_NAME                 0x09 /**< Complete local device name. */
#define BLE_GAP_AD_TYPE_TX_POWER_LEVEL                      0x0A /**< Transmit power level. */
#define BLE_GAP_AD_TYPE_CLASS_OF_DEVICE                     0x0D /**< Class of device. */
#define BLE_GAP_AD_TYPE_SIMPLE_PAIRING_HASH_C               0x0E /**< Simple Pairing Hash C. */
#define BLE_GAP_AD_TYPE_SIMPLE_PAIRING_RANDOMIZER_R         0x0F /**< Simple Pairing Randomizer R. */
#define BLE_GAP_AD_TYPE_SECURITY_MANAGER_TK_VALUE           0x10 /**< Security Manager TK Value. */
#define BLE_GAP_AD_TYPE_SECURITY_MANAGER_OOB_FLAGS          0x11 /**< Security Manager Out Of Band Flags. */
#define BLE_GAP_AD_TYPE_SLAVE_CONNECTION_INTERVAL_RANGE     0x12 /**< Slave Connection Interval Range. */
#define BLE_GAP_AD_TYPE_SOLICITED_SERVICE_UUIDS_16BIT       0x14 /**< List of 16-bit Service Solicitation UUIDs. */
#define BLE_GAP_AD_TYPE_SOLICITED_SERVICE_UUIDS_128BIT      0x15 /**< List of 128-bit Service Solicitation UUIDs. */
#define BLE_GAP_AD_TYPE_SERVICE_DATA                        0x16 /**< Service Data - 16-bit UUID. */
#define BLE_GAP_AD_TYPE_PUBLIC_TARGET_ADDRESS               0x17 /**< Public Target Address. */
#define BLE_GAP_AD_TYPE_RANDOM_TARGET_ADDRESS               0x18 /**< Random Target Address. */
#define BLE_GAP_AD_TYPE_APPEARANCE                          0x19 /**< Appearance. */
#define BLE_GAP_AD_TYPE_ADVERTISING_INTERVAL                0x1A /**< Advertising Interval. */
#define BLE_GAP_AD_TYPE_LE_BLUETOOTH_DEVICE_ADDRESS         0x1B /**< LE Bluetooth Device Address. */
#define BLE_GAP_AD_TYPE_LE_ROLE                             0x1C /**< LE Role. */
#define BLE_GAP_AD_TYPE_SIMPLE_PAIRING_HASH_C256            0x1D /**< Simple Pairing Hash C-256. */
#define BLE_GAP_AD_TYPE_SIMPLE_PAIRING_RANDOMIZER_R256      0x1E /**< Simple Pairing Randomizer R-256. */
#define BLE_GAP_AD_TYPE_SERVICE_DATA_32BIT_UUID             0x20 /**< Service Data - 32-bit UUID. */
#define BLE_GAP_AD_TYPE_SERVICE_DATA_128BIT_UUID            0x21 /**< Service Data - 128-bit UUID. */
#define BLE_GAP_AD_TYPE_LESC_CONFIRMATION_VALUE             0x22 /**< LE Secure Connections Confirmation Value */
#define BLE_GAP_AD_TYPE_LESC_RANDOM_VALUE                   0x23 /**< LE Secure Connections Random Value */
#define BLE_GAP_AD_TYPE_URI                                 0x24 /**< URI */
#define BLE_GAP_AD_TYPE_3D_INFORMATION_DATA                 0x3D /**< 3D Information Data. */
#define BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA          0xFF /**< Manufacturer Specific Data. */
/**@} */


/**@defgroup BLE_GAP_ADV_FLAGS GAP Advertisement Flags
 * @{ */
#define BLE_GAP_ADV_FLAG_LE_LIMITED_DISC_MODE         (0x01)   /**< LE Limited Discoverable Mode. */
#define BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE         (0x02)   /**< LE General Discoverable Mode. */
#define BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED         (0x04)   /**< BR/EDR not supported. */
#define BLE_GAP_ADV_FLAG_LE_BR_EDR_CONTROLLER         (0x08)   /**< Simultaneous LE and BR/EDR, Controller. */
#define BLE_GAP_ADV_FLAG_LE_BR_EDR_HOST               (0x10)   /**< Simultaneous LE and BR/EDR, Host. */
#define BLE_GAP_ADV_FLAGS_LE_ONLY_LIMITED_DISC_MODE   (BLE_GAP_ADV_FLAG_LE_LIMITED_DISC_MODE | BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED)   /**< LE Limited Discoverable Mode, BR/EDR not supported. */
#define BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE   (BLE_GAP_ADV_FLAG_LE_GENERAL_DISC_MODE | BLE_GAP_ADV_FLAG_BR_EDR_NOT_SUPPORTED)   /**< LE General Discoverable Mode, BR/EDR not supported. */
/**@} */



/**@defgroup BLE_GAP_SCAN_INTERVALS GAP Scan interval max and min
 * @{ */
#define BLE_GAP_SCAN_INTERVAL_US_MIN            2500UL /**< Minimum Scan interval in us. */
#define BLE_GAP_SCAN_INTERVAL_US_MAX        40959375UL /**< Maximum Scan interval in us, i.e. 40.96 s. */
 /** @}  */


/**@defgroup BLE_GAP_SCAN_WINDOW GAP Scan window max and min
 * @{ */
#define BLE_GAP_SCAN_WINDOW_US_MIN            2500UL /**< Minimum Scan window in us. */
#define BLE_GAP_SCAN_WINDOW_1MBPS_US_MIN      1875UL /**< Minimum Scan window in us on 1 Mbps PHY. */
#define BLE_GAP_SCAN_WINDOW_US_MAX        40959375UL /**< Maximum Scan window in us, i.e. 40.96 s. */
 /** @}  */


/**@defgroup BLE_GAP_SCAN_TIMEOUT GAP Scan timeout max and min
 * @{ */
#define BLE_GAP_SCAN_TIMEOUT_MIN        0x0001 /**< Minimum Scan timeout in 10 ms units, i.e 10 ms. */
#define BLE_GAP_SCAN_TIMEOUT_UNLIMITED  0x0000 /**< Continue to scan forever. */
 /** @}  */

/**@defgroup BLE_GAP_SCAN_BUFFER_SIZE GAP Minimum scanner buffer size
 *
 * Scan buffers are used for storing advertising data received from an advertiser.
 * @{ */
#define BLE_GAP_SCAN_BUFFER_MIN                    (31)                             /**< Minimum data length for an
                                                                                         advertising set. */
#define BLE_GAP_SCAN_BUFFER_MAX                    (31)                             /**< Maximum data length for an
                                                                                         advertising set. */
/** @}  */


/**@defgroup BLE_GAP_ADV_DATA_STATUS GAP Advertising data status
 * @{ */
#define BLE_GAP_ADV_DATA_STATUS_COMPLETE             0x00 /**< All data in the advertising event have been received. */
/**@} */

/**@defgroup BLE_GAP_SCAN_FILTER_POLICIES GAP Scanner filter policies
 * @{ */
#define BLE_GAP_SCAN_FP_ACCEPT_ALL                      0x00  /**< Accept all advertising packets except directed advertising packets
                                                                   not addressed to this device. */
#define BLE_GAP_SCAN_FP_WHITELIST                       0x01  /**< Accept advertising packets from devices in the whitelist except directed
                                                                   packets not addressed to this device. */
#define BLE_GAP_SCAN_FP_ALL_NOT_RESOLVED_DIRECTED       0x02  /**< Accept all advertising packets specified in @ref BLE_GAP_SCAN_FP_ACCEPT_ALL.
                                                                   In addition, accept directed advertising packets, where the address
                                                                   of the scanner or initiator is a resolvable private address that cannot
                                                                   be resolved. */
#define BLE_GAP_SCAN_FP_WHITELIST_NOT_RESOLVED_DIRECTED 0x03  /**< Accept all advertising packets specified in @ref BLE_GAP_SCAN_FP_WHITELIST.
                                                                   In addition, accept directed advertising packets, where the address of
                                                                   the initiator or scanner is a resolvable private address that cannot
                                                                   be resolved. */
/**@} */

/**@defgroup BLE_GAP_IO_CAPS GAP IO Capabilities
 * @{ */
#define BLE_GAP_IO_CAPS_DISPLAY_ONLY      0x00   /**< Display Only. */
#define BLE_GAP_IO_CAPS_DISPLAY_YESNO     0x01   /**< Display and Yes/No entry. */
#define BLE_GAP_IO_CAPS_KEYBOARD_ONLY     0x02   /**< Keyboard Only. */
#define BLE_GAP_IO_CAPS_NONE              0x03   /**< No I/O capabilities. */
#define BLE_GAP_IO_CAPS_KEYBOARD_DISPLAY  0x04   /**< Keyboard and Display. */
/**@} */


/**@defgroup BLE_GAP_AUTH_KEY_TYPES GAP Authentication Key Types
 * @{ */
#define BLE_GAP_AUTH_KEY_TYPE_NONE        0x00   /**< No key (may be used to reject). */
#define BLE_GAP_AUTH_KEY_TYPE_PASSKEY     0x01   /**< 6-digit Passkey. */
#define BLE_GAP_AUTH_KEY_TYPE_OOB         0x02   /**< Out Of Band data. */
/**@} */


/**@defgroup BLE_GAP_KP_NOT_TYPES GAP Keypress Notification Types
 * @{ */
#define BLE_GAP_KP_NOT_TYPE_PASSKEY_START       0x00   /**< Passkey entry started. */
#define BLE_GAP_KP_NOT_TYPE_PASSKEY_DIGIT_IN    0x01   /**< Passkey digit entered. */
#define BLE_GAP_KP_NOT_TYPE_PASSKEY_DIGIT_OUT   0x02   /**< Passkey digit erased. */
#define BLE_GAP_KP_NOT_TYPE_PASSKEY_CLEAR       0x03   /**< Passkey cleared. */
#define BLE_GAP_KP_NOT_TYPE_PASSKEY_END         0x04   /**< Passkey entry completed. */
/**@} */


/**@defgroup BLE_GAP_SEC_STATUS GAP Security status
 * @{ */
#define BLE_GAP_SEC_STATUS_SUCCESS                0x00  /**< Procedure completed with success. */
#define BLE_GAP_SEC_STATUS_TIMEOUT                0x01  /**< Procedure timed out. */
#define BLE_GAP_SEC_STATUS_PDU_INVALID            0x02  /**< Invalid PDU received. */
#define BLE_GAP_SEC_STATUS_RFU_RANGE1_BEGIN       0x03  /**< Reserved for Future Use range #1 begin. */
#define BLE_GAP_SEC_STATUS_RFU_RANGE1_END         0x80  /**< Reserved for Future Use range #1 end. */
#define BLE_GAP_SEC_STATUS_PASSKEY_ENTRY_FAILED   0x81  /**< Passkey entry failed (user canceled or other). */
#define BLE_GAP_SEC_STATUS_OOB_NOT_AVAILABLE      0x82  /**< Out of Band Key not available. */
#define BLE_GAP_SEC_STATUS_AUTH_REQ               0x83  /**< Authentication requirements not met. */
#define BLE_GAP_SEC_STATUS_CONFIRM_VALUE          0x84  /**< Confirm value failed. */
#define BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP       0x85  /**< Pairing not supported.  */
#define BLE_GAP_SEC_STATUS_ENC_KEY_SIZE           0x86  /**< Encryption key size. */
#define BLE_GAP_SEC_STATUS_SMP_CMD_UNSUPPORTED    0x87  /**< Unsupported SMP command. */
#define BLE_GAP_SEC_STATUS_UNSPECIFIED            0x88  /**< Unspecified reason. */
#define BLE_GAP_SEC_STATUS_REPEATED_ATTEMPTS      0x89  /**< Too little time elapsed since last attempt. */
#define BLE_GAP_SEC_STATUS_INVALID_PARAMS         0x8A  /**< Invalid parameters. */
#define BLE_GAP_SEC_STATUS_DHKEY_FAILURE          0x8B  /**< DHKey check failure. */
#define BLE_GAP_SEC_STATUS_NUM_COMP_FAILURE       0x8C  /**< Numeric Comparison failure. */
#define BLE_GAP_SEC_STATUS_BR_EDR_IN_PROG         0x8D  /**< BR/EDR pairing in progress. */
#define BLE_GAP_SEC_STATUS_X_TRANS_KEY_DISALLOWED 0x8E  /**< BR/EDR Link Key cannot be used for LE keys. */
#define BLE_GAP_SEC_STATUS_RFU_RANGE2_BEGIN       0x8F  /**< Reserved for Future Use range #2 begin. */
#define BLE_GAP_SEC_STATUS_RFU_RANGE2_END         0xFF  /**< Reserved for Future Use range #2 end. */
/**@} */


/**@defgroup BLE_GAP_SEC_STATUS_SOURCES GAP Security status sources
 * @{ */
#define BLE_GAP_SEC_STATUS_SOURCE_LOCAL           0x00  /**< Local failure. */
#define BLE_GAP_SEC_STATUS_SOURCE_REMOTE          0x01  /**< Remote failure. */
/**@} */


/**@defgroup BLE_GAP_CONN_INTVL_UNITS GAP Connection Interval units conversion
   Macros for converting connection interval in milliseconds to units acceptable by @ref ble_gap_conn_params_t.
 * @{ */
#define BLE_GAP_CONN_INTVL_MS_TO_UNITS(CI_MS)       ((uint16_t)((CI_MS) / 1.25))  /**< Parameter can be in range of 7.5 ms to 4000 ms in 1.25 ms steps */

/**@} */


/**@defgroup BLE_GAP_CP_LIMITS GAP Connection Parameters Limits
 * @{
 */
#define BLE_GAP_CP_MIN_CONN_INTVL_NONE           0xFFFF  /**< No new minimum connection interval specified in connect parameters. */
#define BLE_GAP_CP_MIN_CONN_INTVL_MIN            0x0006  /**< Lowest minimum connection interval permitted, in units of 1.25 ms, i.e. 7.5 ms. */
#define BLE_GAP_CP_MIN_CONN_INTVL_MAX            0x0C80  /**< Highest minimum connection interval permitted, in units of 1.25 ms, i.e. 4 s. */
#define BLE_GAP_CP_MAX_CONN_INTVL_NONE           0xFFFF  /**< No new maximum connection interval specified in connect parameters. */
#define BLE_GAP_CP_MAX_CONN_INTVL_MIN            0x0006  /**< Lowest maximum connection interval permitted, in units of 1.25 ms, i.e. 7.5 ms. */
#define BLE_GAP_CP_MAX_CONN_INTVL_MAX            0x0C80  /**< Highest maximum connection interval permitted, in units of 1.25 ms, i.e. 4 s. */
#define BLE_GAP_CP_SLAVE_LATENCY_MAX             0x01F3  /**< Highest slave latency permitted, in connection events. */
#define BLE_GAP_CP_CONN_SUP_TIMEOUT_NONE         0xFFFF  /**< No new supervision timeout specified in connect parameters. */
#define BLE_GAP_CP_CONN_SUP_TIMEOUT_MIN          0x000A  /**< Lowest supervision timeout permitted, in units of 10 ms, i.e. 100 ms. */
#define BLE_GAP_CP_CONN_SUP_TIMEOUT_MAX          0x0C80  /**< Highest supervision timeout permitted, in units of 10 ms, i.e. 32 s. */
/**@} */


/**@defgroup BLE_GAP_DEVNAME GAP device name defines.
 * @{ */
#define BLE_GAP_DEVNAME_DEFAULT                  "nRF5x" /**< Default device name value. */
#define BLE_GAP_DEVNAME_DEFAULT_LEN              31      /**< Default number of octets in device name. */
#define BLE_GAP_DEVNAME_MAX_LEN                  248     /**< Maximum number of octets in device name. */
/**@} */


/**@brief Disable RSSI events for connections */
#define BLE_GAP_RSSI_THRESHOLD_INVALID 0xFF

/**@defgroup BLE_GAP_PHYS GAP PHYs
 * @{ */
#define BLE_GAP_PHY_AUTO                         0x00    /**< Automatic PHY selection. Refer @ref sd_ble_gap_phy_update for more information.*/
#define BLE_GAP_PHY_1MBPS                        0x01    /**< 1 Mbps PHY. */
#define BLE_GAP_PHY_2MBPS                        0x02    /**< 2 Mbps PHY. */
#define BLE_GAP_PHY_CODED                        0x04    /**< Coded PHY. */
#define BLE_GAP_PHY_NOT_SET                      0xFF    /**< PHY is not configured. */

/**@brief Supported PHYs in connections, for scanning, and for advertising. */
#define BLE_GAP_PHYS_SUPPORTED  (BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_2MBPS) /**< All PHYs except @ref BLE_GAP_PHY_CODED are supported. */

/**@} */

/**@defgroup BLE_GAP_CONN_SEC_MODE_SET_MACROS GAP attribute security requirement setters
 *
 * See @ref ble_gap_conn_sec_mode_t.
 * @{ */
/**@brief Set sec_mode pointed to by ptr to have no access rights.*/
#define BLE_GAP_CONN_SEC_MODE_SET_NO_ACCESS(ptr)          do {(ptr)->sm = 0; (ptr)->lv = 0;} while(0)
/**@brief Set sec_mode pointed to by ptr to require no protection, open link.*/
#define BLE_GAP_CONN_SEC_MODE_SET_OPEN(ptr)               do {(ptr)->sm = 1; (ptr)->lv = 1;} while(0)
/**@brief Set sec_mode pointed to by ptr to require encryption, but no MITM protection.*/
#define BLE_GAP_CONN_SEC_MODE_SET_ENC_NO_MITM(ptr)        do {(ptr)->sm = 1; (ptr)->lv = 2;} while(0)
/**@brief Set sec_mode pointed to by ptr to require encryption and MITM protection.*/
#define BLE_GAP_CONN_SEC_MODE_SET_ENC_WITH_MITM(ptr)      do {(ptr)->sm = 1; (ptr)->lv = 3;} while(0)
/**@brief Set sec_mode pointed to by ptr to require LESC encryption and MITM protection.*/
#define BLE_GAP_CONN_SEC_MODE_SET_LESC_ENC_WITH_MITM(ptr) do {(ptr)->sm = 1; (ptr)->lv = 4;} while(0)
/**@brief Set sec_mode pointed to by ptr to require signing or encryption, no MITM protection needed.*/
#define BLE_GAP_CONN_SEC_MODE_SET_SIGNED_NO_MITM(ptr)     do {(ptr)->sm = 2; (ptr)->lv = 1;} while(0)
/**@brief Set sec_mode pointed to by ptr to require signing or encryption with MITM protection.*/
#define BLE_GAP_CONN_SEC_MODE_SET_SIGNED_WITH_MITM(ptr)   do {(ptr)->sm = 2; (ptr)->lv = 2;} while(0)
/**@} */


/**@brief GAP Security Random Number Length. */
#define BLE_GAP_SEC_RAND_LEN 8


/**@brief GAP Security Key Length. */
#define BLE_GAP_SEC_KEY_LEN 16


/**@brief GAP LE Secure Connections Elliptic Curve Diffie-Hellman P-256 Public Key Length. */
#define BLE_GAP_LESC_P256_PK_LEN 64


/**@brief GAP LE Secure Connections Elliptic Curve Diffie-Hellman DHKey Length. */
#define BLE_GAP_LESC_DHKEY_LEN   32


/**@brief GAP Passkey Length. */
#define BLE_GAP_PASSKEY_LEN 6


/**@brief Maximum amount of addresses in the whitelist. */
#define BLE_GAP_WHITELIST_ADDR_MAX_COUNT (8)


/**@brief Maximum amount of identities in the device identities list. */
#define BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT (8)


/**@brief Default connection count for a configuration. */
#define BLE_GAP_CONN_COUNT_DEFAULT (1)


/**@defgroup BLE_GAP_EVENT_LENGTH GAP event length defines.
 *
 * If the link is configured with an event length above the minimum,
 * the SoftDevice will guarantee the link will not be allocated more time than specified.
 * If the configured event length is too short for the current PHY, more time will be allocated to be able to transmit at least one packet pair.
 * Notice that this may lead to scheduling conflicts. See the SoftDevice Specification for more details.
 * @{ */
#define BLE_GAP_EVENT_LENGTH_MIN            (2)  /**< Minimum event length, in 1.25 ms units. */
#define BLE_GAP_EVENT_LENGTH_1MBPS_PHY_MIN  (2)  /**< Minimum event length on 1 Mbps PHY, in 1.25 ms units. */
#define BLE_GAP_EVENT_LENGTH_2MBPS_PHY_MIN  (1)  /**< Minimum event length on 2 Mbps PHY, in 1.25 ms units. */
#define BLE_GAP_EVENT_LENGTH_DEFAULT        (3)  /**< Default event length, in 1.25 ms units. */
/**@} */


/**@defgroup BLE_GAP_ROLE_COUNT GAP concurrent connection count defines.
 * @{ */
#define BLE_GAP_ROLE_COUNT_CENTRAL_DEFAULT     (3)   /**< Default maximum number of connections concurrently acting as centrals. */
#define BLE_GAP_ROLE_COUNT_CENTRAL_SEC_DEFAULT (1)   /**< Default number of SMP instances shared between all connections acting as centrals. */
#define BLE_GAP_ROLE_COUNT_COMBINED_MAX        (20)  /**< Maximum supported number of concurrent connections for all connected roles combined. */
/**@} */


/**@defgroup BLE_GAP_AUTH_PAYLOAD_TIMEOUT Authenticated payload timeout defines.
 * @{ */
#define BLE_GAP_AUTH_PAYLOAD_TIMEOUT_MAX (48000) /**< Maximum authenticated payload timeout in 10 ms units, i.e. 8 minutes. */
#define BLE_GAP_AUTH_PAYLOAD_TIMEOUT_MIN (1)     /**< Minimum authenticated payload timeout in 10 ms units, i.e. 10 ms. */
/**@} */

/**@defgroup GAP_SEC_MODES GAP Security Modes
 * @{ */
#define BLE_GAP_SEC_MODE 0x00 /**< No key (may be used to reject). */
/**@} */

/**@brief The total number of channels in Bluetooth Low Energy. */
#define BLE_GAP_CHANNEL_COUNT      (40)

/**@defgroup BLE_GAP_QOS_CHANNEL_SURVEY_INTERVALS Quality of Service (QoS) Channel survey interval defines
 * @{ */
#define BLE_GAP_QOS_CHANNEL_SURVEY_INTERVAL_CONTINUOUS  (0)       /**< Continuous channel survey. */
#define BLE_GAP_QOS_CHANNEL_SURVEY_INTERVAL_MIN_US      (7500)    /**< Minimum channel survey interval in microseconds (7.5 ms). */
#define BLE_GAP_QOS_CHANNEL_SURVEY_INTERVAL_MAX_US      (4000000) /**< Maximum channel survey interval in microseconds (4 s). */
 /**@}  */

/** @} */

/** @defgroup BLE_GAP_CHAR_INCL_CONFIG GAP Characteristic inclusion configurations
 * @{
 */
#define BLE_GAP_CHAR_INCL_CONFIG_INCLUDE               (0) /**< Include the characteristic in the Attribute Table */
#define BLE_GAP_CHAR_INCL_CONFIG_EXCLUDE_WITH_SPACE    (1) /**< Do not include the characteristic in the Attribute table.
                                                                The SoftDevice will reserve the attribute handles
                                                                which are otherwise used for this characteristic.
                                                                By reserving the attribute handles it will be possible
                                                                to upgrade the SoftDevice without changing handle of the
                                                                Service Changed characteristic. */
#define BLE_GAP_CHAR_INCL_CONFIG_EXCLUDE_WITHOUT_SPACE (2) /**< Do not include the characteristic in the Attribute table.
                                                                The SoftDevice will not reserve the attribute handles
                                                                which are otherwise used for this characteristic. */
/**@} */


/** @defgroup BLE_GAP_CHAR_INCL_CONFIG_DEFAULTS Characteristic inclusion default values
 * @{ */
#define BLE_GAP_PPCP_INCL_CONFIG_DEFAULT     (BLE_GAP_CHAR_INCL_CONFIG_EXCLUDE_WITH_SPACE) /**< Excluded with space for this SoftDevice. */
#define BLE_GAP_CAR_INCL_CONFIG_DEFAULT      (BLE_GAP_CHAR_INCL_CONFIG_INCLUDE)            /**< Included by default. */
/**@} */

/**@addtogroup BLE_GAP_STRUCTURES Structures
 * @{ */

/**@brief Advertising report type. */
typedef struct
{
  uint16_t connectable   : 1; /**< Connectable advertising event type. */
  uint16_t scannable     : 1; /**< Scannable advertising event type. */
  uint16_t directed      : 1; /**< Directed advertising event type. */
  uint16_t scan_response : 1; /**< Received a scan response. */
  uint16_t extended_pdu  : 1; /**< Received an extended advertising set. */
  uint16_t status        : 2; /**< Data status. See @ref BLE_GAP_ADV_DATA_STATUS. */
  uint16_t reserved      : 9; /**< Reserved for future use. */
} ble_gap_adv_report_type_t;

/**@brief Advertising Auxiliary Pointer. */
typedef struct
{
  uint16_t  aux_offset;   /**< Time offset from the beginning of advertising packet to the auxiliary packet in 100 us units. */
  uint8_t   aux_phy;      /**< Indicates the PHY on which the auxiliary advertising packet is sent. See @ref BLE_GAP_PHYS. */
} ble_gap_aux_pointer_t;

/**@brief Bluetooth Low Energy address. */
typedef struct
{
  uint8_t addr_id_peer : 1;       /**< Only valid for peer addresses.
                                       This bit is set by the SoftDevice to indicate whether the address has been resolved from
                                       a Resolvable Private Address (when the peer is using privacy).
                                       If set to 1, @ref addr and @ref addr_type refer to the identity address of the resolved address.

                                       This bit is ignored when a variable of type @ref ble_gap_addr_t is used as input to API functions. */
  uint8_t addr_type    : 7;       /**< See @ref BLE_GAP_ADDR_TYPES. */
  uint8_t addr[BLE_GAP_ADDR_LEN]; /**< 48-bit address, LSB format. */
} ble_gap_addr_t;


/**@brief GAP connection parameters.
 *
 * @note  When ble_conn_params_t is received in an event, both min_conn_interval and
 *        max_conn_interval will be equal to the connection interval set by the central.
 *
 * @note  If both @ref conn_sup_timeout and @ref max_conn_interval
 *        are specified, then the following constraint applies:
 *        The @ref conn_sup_timeout converted to milliseconds shall be larger than
 *        (1 + @ref slave_latency) * max_conn_interval_ms * 2
 *        where max_conn_interval_ms is @ref max_conn_interval converted to milliseconds.
 */
typedef struct
{
  uint16_t min_conn_interval;         /**< Minimum Connection Interval, see @ref BLE_GAP_CONN_INTVL_UNITS.*/
  uint16_t max_conn_interval;         /**< Maximum Connection Interval, see @ref BLE_GAP_CONN_INTVL_UNITS.*/
  uint16_t slave_latency;             /**< Slave Latency in number of connection events, see @ref BLE_GAP_CP_LIMITS.*/
  uint16_t conn_sup_timeout;          /**< Connection Supervision Timeout, see @ref BLE_GAP_CP_LIMITS for range and unit size.*/
} ble_gap_conn_params_t;


/**@brief GAP connection security modes.
 *
 * Security Mode 0 Level 0: No access permissions at all (this level is not defined by the Bluetooth Core specification).\n
 * Security Mode 1 Level 1: No security is needed (aka open link).\n
 * Security Mode 1 Level 2: Encrypted link required, MITM protection not necessary.\n
 * Security Mode 1 Level 3: MITM protected encrypted link required.\n
 * Security Mode 1 Level 4: LESC MITM protected encrypted link using a 128-bit strength encryption key required.\n
 * Security Mode 2 Level 1: Signing or encryption required, MITM protection not necessary.\n
 * Security Mode 2 Level 2: MITM protected signing required, unless link is MITM protected encrypted.\n
 */
typedef struct
{
  uint8_t sm : 4;                     /**< Security Mode (1 or 2), 0 for no permissions at all. */
  uint8_t lv : 4;                     /**< Level (1, 2, 3 or 4), 0 for no permissions at all. */

} ble_gap_conn_sec_mode_t;


/**@brief GAP connection security status.*/
typedef struct
{
  ble_gap_conn_sec_mode_t sec_mode;           /**< Currently active security mode for this connection.*/
  uint8_t                 encr_key_size;      /**< Length of currently active encryption key, 7 to 16 octets (only applicable for bonding procedures). */
} ble_gap_conn_sec_t;

/**@brief Identity Resolving Key. */
typedef struct
{
  uint8_t irk[BLE_GAP_SEC_KEY_LEN];   /**< Array containing IRK. */
} ble_gap_irk_t;


/**@brief Channel mask (40 bits).
 * Every channel is represented with a bit positioned as per channel index defined in Bluetooth Core Specification v5.0,
 * Vol 6, Part B, Section 1.4.1. The LSB contained in array element 0 represents channel index 0, and bit 39 represents
 * channel index 39. If a bit is set to 1, the channel is not used.
 */
typedef uint8_t ble_gap_ch_mask_t[5];



/**@brief GAP scanning parameters. */
typedef struct
{
  uint8_t               extended               : 1; /**< This feature is not supported by this SoftDevice. */
  uint8_t               report_incomplete_evts : 1; /**< This feature is not supported by this SoftDevice. */
  uint8_t               active                 : 1; /**< If 1, perform active scanning by sending scan requests.
                                                         This parameter is ignored when used with @ref sd_ble_gap_connect. */
  uint8_t               filter_policy          : 2; /**< Scanning filter policy. @sa BLE_GAP_SCAN_FILTER_POLICIES.
                                                         @note Only @ref BLE_GAP_SCAN_FP_ACCEPT_ALL and
                                                               @ref BLE_GAP_SCAN_FP_WHITELIST are valid when used with
                                                               @ref sd_ble_gap_connect */
  uint8_t               scan_phys;                  /**< Bitfield of PHYs to scan on. If set to @ref BLE_GAP_PHY_AUTO,
                                                         scan_phys will default to @ref BLE_GAP_PHY_1MBPS.
                                                         Only @ref BLE_GAP_PHY_1MBPS is supported for this SoftDevice.
                                                        */

  uint32_t              interval_us;                /**< Scan interval in us. @sa BLE_GAP_SCAN_INTERVALS. */
  uint32_t              window_us;                  /**< Scan window in us. @sa BLE_GAP_SCAN_WINDOW.
                                                         If scan_phys contains multiple phys, then interval shall be
                                                         larger than or equal to the sum of scan window sizes.
                                                         This parameter represents the time set aside for each
                                                         scan window. That is, the actual listening time is slightly less.
                                                         See the SoftDevice Specification for details. */
  uint16_t              timeout;                    /**< Scan timeout in 10 ms units. @sa BLE_GAP_SCAN_TIMEOUT. */
  ble_gap_ch_mask_t     channel_mask;               /**< Channel mask for primary and secondary advertising channels.
                                                         At least one of the primary channels, that is channel index 37-39, must be
                                                         set to 0.
                                                         Masking away secondary channels is not supported. */
} ble_gap_scan_params_t;


/**@brief Privacy.
 *
 *        The privacy feature provides a way for the device to avoid being tracked over a period of time.
 *        The privacy feature, when enabled, hides the local device identity and replaces it with a private address
 *        that is automatically refreshed at a specified interval.
 *
 *        If a device still wants to be recognized by other peers, it needs to share it's Identity Resolving Key (IRK).
 *        With this key, a device can generate a random private address that can only be recognized by peers in possession of that key,
 *        and devices can establish connections without revealing their real identities.
 *
 *        Both network privacy (@ref BLE_GAP_PRIVACY_MODE_NETWORK_PRIVACY) and device privacy (@ref BLE_GAP_PRIVACY_MODE_DEVICE_PRIVACY)
 *        are supported.
 *
 * @note  If the device IRK is updated, the new IRK becomes the one to be distributed in all
 *        bonding procedures performed after @ref sd_ble_gap_privacy_set returns.
 *        The IRK distributed during bonding procedure is the device IRK that is active when @ref sd_ble_gap_sec_params_reply is called.
 */
typedef struct
{
  uint8_t        privacy_mode;         /**< Privacy mode, see @ref BLE_GAP_PRIVACY_MODES. Default is @ref BLE_GAP_PRIVACY_MODE_OFF. */
  uint8_t        private_addr_type;    /**< The private address type must be either @ref BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE or @ref BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_NON_RESOLVABLE. */
  uint16_t       private_addr_cycle_s; /**< Private address cycle interval in seconds. Providing an address cycle value of 0 will use the default value defined by @ref BLE_GAP_DEFAULT_PRIVATE_ADDR_CYCLE_INTERVAL_S. */
  ble_gap_irk_t *p_device_irk;         /**< When used as input, pointer to IRK structure that will be used as the default IRK. If NULL, the device default IRK will be used.
                                            When used as output, pointer to IRK structure where the current default IRK will be written to. If NULL, this argument is ignored.
                                            By default, the default IRK is used to generate random private resolvable addresses for the local device unless instructed otherwise. */
} ble_gap_privacy_params_t;


/**@brief PHY preferences for TX and RX
 * @note  tx_phys and rx_phys are bit fields. Multiple bits can be set in them to indicate multiple preferred PHYs for each direction.
 * @code
 * p_gap_phys->tx_phys = BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_2MBPS;
 * p_gap_phys->rx_phys = BLE_GAP_PHY_1MBPS | BLE_GAP_PHY_2MBPS;
 * @endcode
 *
 */
typedef struct
{
  uint8_t tx_phys;     /**< Preferred transmit PHYs, see @ref BLE_GAP_PHYS. */
  uint8_t rx_phys;     /**< Preferred receive PHYs, see @ref BLE_GAP_PHYS. */
} ble_gap_phys_t;

/** @brief Keys that can be exchanged during a bonding procedure. */
typedef struct
{
  uint8_t enc     : 1;                        /**< Long Term Key and Master Identification. */
  uint8_t id      : 1;                        /**< Identity Resolving Key and Identity Address Information. */
  uint8_t sign    : 1;                        /**< Connection Signature Resolving Key. */
  uint8_t link    : 1;                        /**< Derive the Link Key from the LTK. */
} ble_gap_sec_kdist_t;


/**@brief GAP security parameters. */
typedef struct
{
  uint8_t               bond      : 1;             /**< Perform bonding. */
  uint8_t               mitm      : 1;             /**< Enable Man In The Middle protection. */
  uint8_t               lesc      : 1;             /**< Enable LE Secure Connection pairing. */
  uint8_t               keypress  : 1;             /**< Enable generation of keypress notifications. */
  uint8_t               io_caps   : 3;             /**< IO capabilities, see @ref BLE_GAP_IO_CAPS. */
  uint8_t               oob       : 1;             /**< The OOB data flag.
                                                        - In LE legacy pairing, this flag is set if a device has out of band authentication data.
                                                          The OOB method is used if both of the devices have out of band authentication data.
                                                        - In LE Secure Connections pairing, this flag is set if a device has the peer device's out of band authentication data.
                                                          The OOB method is used if at least one device has the peer device's OOB data available. */
  uint8_t               min_key_size;              /**< Minimum encryption key size in octets between 7 and 16. If 0 then not applicable in this instance. */
  uint8_t               max_key_size;              /**< Maximum encryption key size in octets between min_key_size and 16. */
  ble_gap_sec_kdist_t   kdist_own;                 /**< Key distribution bitmap: keys that the local device will distribute. */
  ble_gap_sec_kdist_t   kdist_peer;                /**< Key distribution bitmap: keys that the remote device will distribute. */
} ble_gap_sec_params_t;


/**@brief GAP Encryption Information. */
typedef struct
{
  uint8_t   ltk[BLE_GAP_SEC_KEY_LEN];   /**< Long Term Key. */
  uint8_t   lesc : 1;                   /**< Key generated using LE Secure Connections. */
  uint8_t   auth : 1;                   /**< Authenticated Key. */
  uint8_t   ltk_len : 6;                /**< LTK length in octets. */
} ble_gap_enc_info_t;


/**@brief GAP Master Identification. */
typedef struct
{
  uint16_t  ediv;                       /**< Encrypted Diversifier. */
  uint8_t   rand[BLE_GAP_SEC_RAND_LEN]; /**< Random Number. */
} ble_gap_master_id_t;


/**@brief GAP Signing Information. */
typedef struct
{
  uint8_t   csrk[BLE_GAP_SEC_KEY_LEN];        /**< Connection Signature Resolving Key. */
} ble_gap_sign_info_t;


/**@brief GAP LE Secure Connections P-256 Public Key. */
typedef struct
{
  uint8_t   pk[BLE_GAP_LESC_P256_PK_LEN];        /**< LE Secure Connections Elliptic Curve Diffie-Hellman P-256 Public Key. Stored in the standard SMP protocol format: {X,Y} both in little-endian. */
} ble_gap_lesc_p256_pk_t;


/**@brief GAP LE Secure Connections DHKey. */
typedef struct
{
  uint8_t   key[BLE_GAP_LESC_DHKEY_LEN];        /**< LE Secure Connections Elliptic Curve Diffie-Hellman Key. Stored in little-endian. */
} ble_gap_lesc_dhkey_t;


/**@brief GAP LE Secure Connections OOB data. */
typedef struct
{
  ble_gap_addr_t  addr;                          /**< Bluetooth address of the device. */
  uint8_t         r[BLE_GAP_SEC_KEY_LEN];        /**< Random Number. */
  uint8_t         c[BLE_GAP_SEC_KEY_LEN];        /**< Confirm Value. */
} ble_gap_lesc_oob_data_t;


/**@brief Event structure for @ref BLE_GAP_EVT_CONNECTED. */
typedef struct
{
  ble_gap_addr_t        peer_addr;              /**< Bluetooth address of the peer device. If the peer_addr resolved: @ref ble_gap_addr_t::addr_id_peer is set to 1
                                                     and the address is the device's identity address. */
  uint8_t               role;                   /**< BLE role for this connection, see @ref BLE_GAP_ROLES */
  ble_gap_conn_params_t conn_params;            /**< GAP Connection Parameters. */
} ble_gap_evt_connected_t;


/**@brief Event structure for @ref BLE_GAP_EVT_DISCONNECTED. */
typedef struct
{
  uint8_t reason;                               /**< HCI error code, see @ref BLE_HCI_STATUS_CODES. */
} ble_gap_evt_disconnected_t;


/**@brief Event structure for @ref BLE_GAP_EVT_CONN_PARAM_UPDATE. */
typedef struct
{
  ble_gap_conn_params_t conn_params;            /**<  GAP Connection Parameters. */
} ble_gap_evt_conn_param_update_t;

/**@brief Event structure for @ref BLE_GAP_EVT_PHY_UPDATE_REQUEST. */
typedef struct
{
  ble_gap_phys_t peer_preferred_phys;            /**< The PHYs the peer prefers to use. */
} ble_gap_evt_phy_update_request_t;

/**@brief Event Structure for @ref BLE_GAP_EVT_PHY_UPDATE. */
typedef struct
{
  uint8_t status;                               /**< Status of the procedure, see @ref BLE_HCI_STATUS_CODES.*/
  uint8_t tx_phy;                               /**< TX PHY for this connection, see @ref BLE_GAP_PHYS. */
  uint8_t rx_phy;                               /**< RX PHY for this connection, see @ref BLE_GAP_PHYS. */
} ble_gap_evt_phy_update_t;

/**@brief Event structure for @ref BLE_GAP_EVT_SEC_PARAMS_REQUEST. */
typedef struct
{
  ble_gap_sec_params_t peer_params;             /**< Initiator Security Parameters. */
} ble_gap_evt_sec_params_request_t;


/**@brief Event structure for @ref BLE_GAP_EVT_PASSKEY_DISPLAY. */
typedef struct
{
  uint8_t passkey[BLE_GAP_PASSKEY_LEN];         /**< 6-digit passkey in ASCII ('0'-'9' digits only). */
  uint8_t match_request : 1;                    /**< If 1 requires the application to report the match using @ref sd_ble_gap_auth_key_reply
                                                     with either @ref BLE_GAP_AUTH_KEY_TYPE_NONE if there is no match or
                                                     @ref BLE_GAP_AUTH_KEY_TYPE_PASSKEY if there is a match. */
} ble_gap_evt_passkey_display_t;

/**@brief Event structure for @ref BLE_GAP_EVT_KEY_PRESSED. */
typedef struct
{
  uint8_t kp_not;         /**< Keypress notification type, see @ref BLE_GAP_KP_NOT_TYPES. */
} ble_gap_evt_key_pressed_t;


/**@brief Event structure for @ref BLE_GAP_EVT_AUTH_KEY_REQUEST. */
typedef struct
{
  uint8_t key_type;                             /**< See @ref BLE_GAP_AUTH_KEY_TYPES. */
} ble_gap_evt_auth_key_request_t;

/**@brief Event structure for @ref BLE_GAP_EVT_LESC_DHKEY_REQUEST. */
typedef struct
{
  ble_gap_lesc_p256_pk_t *p_pk_peer;  /**< LE Secure Connections remote P-256 Public Key. This will point to the application-supplied memory
                                           inside the keyset during the call to @ref sd_ble_gap_sec_params_reply. */
  uint8_t oobd_req       :1;          /**< LESC OOB data required. A call to @ref sd_ble_gap_lesc_oob_data_set is required to complete the procedure. */
} ble_gap_evt_lesc_dhkey_request_t;


/**@brief Security levels supported.
 * @note  See Bluetooth Specification Version 4.2 Volume 3, Part C, Chapter 10, Section 10.2.1.
*/
typedef struct
{
  uint8_t lv1 : 1;                              /**< If 1: Level 1 is supported. */
  uint8_t lv2 : 1;                              /**< If 1: Level 2 is supported. */
  uint8_t lv3 : 1;                              /**< If 1: Level 3 is supported. */
  uint8_t lv4 : 1;                              /**< If 1: Level 4 is supported. */
} ble_gap_sec_levels_t;


/**@brief Encryption Key. */
typedef struct
{
  ble_gap_enc_info_t    enc_info;             /**< Encryption Information. */
  ble_gap_master_id_t   master_id;            /**< Master Identification. */
} ble_gap_enc_key_t;


/**@brief Identity Key. */
typedef struct
{
  ble_gap_irk_t         id_info;              /**< Identity Resolving Key. */
  ble_gap_addr_t        id_addr_info;         /**< Identity Address. */
} ble_gap_id_key_t;


/**@brief Security Keys. */
typedef struct
{
  ble_gap_enc_key_t      *p_enc_key;           /**< Encryption Key, or NULL. */
  ble_gap_id_key_t       *p_id_key;            /**< Identity Key, or NULL. */
  ble_gap_sign_info_t    *p_sign_key;          /**< Signing Key, or NULL. */
  ble_gap_lesc_p256_pk_t *p_pk;                /**< LE Secure Connections P-256 Public Key. When in debug mode the application must use the value defined
                                                    in the Core Bluetooth Specification v4.2 Vol.3, Part H, Section 2.3.5.6.1 */
} ble_gap_sec_keys_t;


/**@brief Security key set for both local and peer keys. */
typedef struct
{
  ble_gap_sec_keys_t            keys_own;     /**< Keys distributed by the local device. For LE Secure Connections the encryption key will be generated locally and will always be stored if bonding. */
  ble_gap_sec_keys_t            keys_peer;    /**< Keys distributed by the remote device. For LE Secure Connections, p_enc_key must always be NULL. */
} ble_gap_sec_keyset_t;


/**@brief Event structure for @ref BLE_GAP_EVT_AUTH_STATUS. */
typedef struct
{
  uint8_t               auth_status;            /**< Authentication status, see @ref BLE_GAP_SEC_STATUS. */
  uint8_t               error_src : 2;          /**< On error, source that caused the failure, see @ref BLE_GAP_SEC_STATUS_SOURCES. */
  uint8_t               bonded : 1;             /**< Procedure resulted in a bond. */
  uint8_t               lesc : 1;               /**< Procedure resulted in a LE Secure Connection. */
  ble_gap_sec_levels_t  sm1_levels;             /**< Levels supported in Security Mode 1. */
  ble_gap_sec_levels_t  sm2_levels;             /**< Levels supported in Security Mode 2. */
  ble_gap_sec_kdist_t   kdist_own;              /**< Bitmap stating which keys were exchanged (distributed) by the local device. If bonding with LE Secure Connections, the enc bit will be always set. */
  ble_gap_sec_kdist_t   kdist_peer;             /**< Bitmap stating which keys were exchanged (distributed) by the remote device. If bonding with LE Secure Connections, the enc bit will never be set. */
} ble_gap_evt_auth_status_t;


/**@brief Event structure for @ref BLE_GAP_EVT_CONN_SEC_UPDATE. */
typedef struct
{
  ble_gap_conn_sec_t conn_sec;                  /**< Connection security level. */
} ble_gap_evt_conn_sec_update_t;


/**@brief Event structure for @ref BLE_GAP_EVT_TIMEOUT. */
typedef struct
{
  uint8_t src;                                  /**< Source of timeout event, see @ref BLE_GAP_TIMEOUT_SOURCES. */
  union
  {
    ble_data_t adv_report_buffer;               /**< If source is set to @ref BLE_GAP_TIMEOUT_SRC_SCAN, the released
                                                     scan buffer is contained in this field. */
  } params;                                     /**< Event Parameters. */
} ble_gap_evt_timeout_t;


/**@brief Event structure for @ref BLE_GAP_EVT_RSSI_CHANGED. */
typedef struct
{
  int8_t  rssi;                                 /**< Received Signal Strength Indication in dBm.
                                                     @note ERRATA-153 and ERRATA-225 require the rssi sample to be compensated based on a temperature measurement. */
  uint8_t ch_index;                             /**< Data Channel Index on which the Signal Strength is measured (0-36). */
} ble_gap_evt_rssi_changed_t;

/**@brief Event structure for @ref BLE_GAP_EVT_ADV_REPORT.
 */
typedef struct
{
  ble_gap_adv_report_type_t type;                  /**< Advertising report type. See @ref ble_gap_adv_report_type_t. */
  ble_gap_addr_t            peer_addr;             /**< Bluetooth address of the peer device. If the peer_addr is resolved:
                                                        @ref ble_gap_addr_t::addr_id_peer is set to 1 and the address is the
                                                        peer's identity address. */
  ble_gap_addr_t            direct_addr;           /**< Contains the target address of the advertising event if
                                                        @ref ble_gap_adv_report_type_t::directed is set to 1. If the
                                                        SoftDevice was able to resolve the address,
                                                        @ref ble_gap_addr_t::addr_id_peer is set to 1 and the direct_addr
                                                        contains the local identity address. If the target address of the
                                                        advertising event is @ref BLE_GAP_ADDR_TYPE_RANDOM_PRIVATE_RESOLVABLE,
                                                        and the SoftDevice was unable to resolve it, the application may try
                                                        to resolve this address to find out if the advertising event was
                                                        directed to us. */
  uint8_t                   primary_phy;           /**< Indicates the PHY on which the primary advertising packet was received.
                                                        See @ref BLE_GAP_PHYS. */
  uint8_t                   secondary_phy;         /**< Indicates the PHY on which the secondary advertising packet was received.
                                                        See @ref BLE_GAP_PHYS. This field is set to @ref BLE_GAP_PHY_NOT_SET if no packets
                                                        were received on a secondary advertising channel. */
  int8_t                    tx_power;              /**< This field is only used with extended advertising packets, not supported
                                                        by this SoftDevice. */
  int8_t                    rssi;                  /**< Received Signal Strength Indication in dBm of the last packet received.
                                                        @note ERRATA-153 and ERRATA-225 require the rssi sample to be compensated based on a temperature measurement. */
  uint8_t                   ch_index;              /**< Channel Index on which the last advertising packet is received (0-39). */
  uint8_t                   set_id;                /**< Set ID of the received advertising data. Set ID is not present
                                                        if set to @ref BLE_GAP_ADV_REPORT_SET_ID_NOT_AVAILABLE. */
  uint16_t                  data_id:12;            /**< The advertising data ID of the received advertising data. Data ID
                                                        is not present if @ref ble_gap_evt_adv_report_t::set_id is set to
                                                        @ref BLE_GAP_ADV_REPORT_SET_ID_NOT_AVAILABLE. */
  ble_data_t                data;                  /**< Received advertising or scan response data,
                                                        the data buffer provided in @ref sd_ble_gap_scan_start is now released. */
  ble_gap_aux_pointer_t     aux_pointer;           /**< This field is only used with extended advertisement packets, not supported
                                                        by this SoftDevice. */
} ble_gap_evt_adv_report_t;


/**@brief Event structure for @ref BLE_GAP_EVT_SEC_REQUEST. */
typedef struct
{
  uint8_t    bond       : 1;                       /**< Perform bonding. */
  uint8_t    mitm       : 1;                       /**< Man In The Middle protection requested. */
  uint8_t    lesc       : 1;                       /**< LE Secure Connections requested. */
  uint8_t    keypress   : 1;                       /**< Generation of keypress notifications requested. */
} ble_gap_evt_sec_request_t;


/**@brief Event structure for @ref BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST. */
typedef struct
{
  ble_gap_conn_params_t conn_params;            /**<  GAP Connection Parameters. */
} ble_gap_evt_conn_param_update_request_t;



/**@brief Event structure for @ref BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT. */
typedef struct
{
  int8_t channel_energy[BLE_GAP_CHANNEL_COUNT]; /**< The measured energy on the Bluetooth Low Energy
                                                     channels, in dBm, indexed by Channel Index.
                                                     If no measurement is available for the given channel, channel_energy is set to
                                                     @ref BLE_GAP_POWER_LEVEL_INVALID. */
} ble_gap_evt_qos_channel_survey_report_t;

/**@brief Event structure for @ref BLE_GAP_EVT_QOS_CONN_EVENT_REPORT. */
typedef struct
{
  uint16_t event_counter;                       /**< Connection event counter corresponding to the connection event report. */
  uint8_t ch_index;                             /**< Data Channel Index used during the connection event (0-36). */
  uint8_t crc_ok_count;                         /**< Number of packets received with good CRC during the connection event. */
  uint8_t crc_error_count;                      /**< Number of packets received with bad CRC during the connection event. */
  uint8_t rx_timeout : 1;                       /**< Indicates that the connection event was closed because a packet was not received. */
} ble_gap_evt_qos_conn_event_report_t;

/**@brief GAP event structure. */
typedef struct
{
  uint16_t conn_handle;                                     /**< Connection Handle on which event occurred. */
  union                                                     /**< union alternative identified by evt_id in enclosing struct. */
  {
    ble_gap_evt_connected_t                   connected;                    /**< Connected Event Parameters. */
    ble_gap_evt_disconnected_t                disconnected;                 /**< Disconnected Event Parameters. */
    ble_gap_evt_conn_param_update_t           conn_param_update;            /**< Connection Parameter Update Parameters. */
    ble_gap_evt_sec_params_request_t          sec_params_request;           /**< Security Parameters Request Event Parameters. */
    ble_gap_evt_passkey_display_t             passkey_display;              /**< Passkey Display Event Parameters. */
    ble_gap_evt_key_pressed_t                 key_pressed;                  /**< Key Pressed Event Parameters. */
    ble_gap_evt_auth_key_request_t            auth_key_request;             /**< Authentication Key Request Event Parameters. */
    ble_gap_evt_lesc_dhkey_request_t          lesc_dhkey_request;           /**< LE Secure Connections DHKey calculation request. */
    ble_gap_evt_auth_status_t                 auth_status;                  /**< Authentication Status Event Parameters. */
    ble_gap_evt_conn_sec_update_t             conn_sec_update;              /**< Connection Security Update Event Parameters. */
    ble_gap_evt_timeout_t                     timeout;                      /**< Timeout Event Parameters. */
    ble_gap_evt_rssi_changed_t                rssi_changed;                 /**< RSSI Event Parameters. */
    ble_gap_evt_adv_report_t                  adv_report;                   /**< Advertising Report Event Parameters. */
    ble_gap_evt_sec_request_t                 sec_request;                  /**< Security Request Event Parameters. */
    ble_gap_evt_conn_param_update_request_t   conn_param_update_request;    /**< Connection Parameter Update Parameters. */
    ble_gap_evt_phy_update_request_t          phy_update_request;           /**< PHY Update Request Event Parameters. */
    ble_gap_evt_phy_update_t                  phy_update;                   /**< PHY Update Parameters. */
    ble_gap_evt_qos_channel_survey_report_t   qos_channel_survey_report;    /**< Quality of Service (QoS) Channel Survey Report Parameters. */
    ble_gap_evt_qos_conn_event_report_t       qos_conn_event_report;        /**< Connection Event Quality of Service (QoS) Parameters. */
  } params;                                                                 /**< Event Parameters. */
} ble_gap_evt_t;


/**
 * @brief BLE GAP connection configuration parameters, set with @ref sd_ble_cfg_set.
 *
 * @retval ::NRF_ERROR_CONN_COUNT     The connection count for the connection configurations is zero.
 * @retval ::NRF_ERROR_INVALID_PARAM  One or more of the following is true:
 *                                    - The sum of conn_count for all connection configurations combined exceeds UINT8_MAX.
 *                                    - The event length is smaller than @ref BLE_GAP_EVENT_LENGTH_MIN.
 */
typedef struct
{
  uint8_t  conn_count;     /**< The number of concurrent connections the application can create with this configuration.
                                The default and minimum value is @ref BLE_GAP_CONN_COUNT_DEFAULT. */
  uint16_t event_length;   /**< The time set aside for this connection on every connection interval in 1.25 ms units.
                                The default value is @ref BLE_GAP_EVENT_LENGTH_DEFAULT, the minimum value is @ref BLE_GAP_EVENT_LENGTH_MIN.
                                The event length and the connection interval are the primary parameters
                                for setting the throughput of a connection.
                                See the SoftDevice Specification for details on throughput.
                           */
} ble_gap_conn_cfg_t;


/**
 * @brief Configuration of maximum concurrent connections in the different connected roles, set with
 * @ref sd_ble_cfg_set.
 *
 * @retval ::NRF_ERROR_CONN_COUNT     The central_role_count is too large.
 *                                    The maximum supported number of concurrent connections is
 *                                    @ref BLE_GAP_ROLE_COUNT_COMBINED_MAX for all connected roles
 *                                    combined.
 * @retval ::NRF_ERROR_INVALID_PARAM  central_sec_count is larger than central_role_count.
 */
typedef struct
{
  uint8_t central_role_count; /**< Maximum number of connections concurrently acting as a central. Default value is @ref BLE_GAP_ROLE_COUNT_CENTRAL_DEFAULT. */
  uint8_t central_sec_count;  /**< Number of SMP instances shared between all connections acting as a central. Default value is @ref BLE_GAP_ROLE_COUNT_CENTRAL_SEC_DEFAULT. */
  uint8_t qos_channel_survey_role_available:1; /**< If set, the Quality of Service (QoS) channel survey module is available to the
                                                    application using @ref sd_ble_gap_qos_start. */
} ble_gap_cfg_role_count_t;


/**
 * @brief Device name and its properties, set with @ref sd_ble_cfg_set.
 *
 * @note  If the device name is not configured, the default device name will be
 *        @ref BLE_GAP_DEVNAME_DEFAULT, the maximum device name length will be
 *        @ref BLE_GAP_DEVNAME_DEFAULT_LEN, vloc will be set to @ref BLE_GATTS_VLOC_STACK and the device name
 *        will have no write access.
 *
 * @note  If @ref max_len is more than @ref BLE_GAP_DEVNAME_DEFAULT_LEN and vloc is set to @ref BLE_GATTS_VLOC_STACK,
 *        the attribute table size must be increased to have room for the longer device name (see
 *        @ref sd_ble_cfg_set and @ref ble_gatts_cfg_attr_tab_size_t).
 *
 * @note  If vloc is @ref BLE_GATTS_VLOC_STACK :
 *        - p_value must point to non-volatile memory (flash) or be NULL.
 *        - If p_value is NULL, the device name will initially be empty.
 *
 * @note  If vloc is @ref BLE_GATTS_VLOC_USER :
 *        - p_value cannot be NULL.
 *        - If the device name is writable, p_value must point to volatile memory (RAM).
 *
 * @retval ::NRF_ERROR_INVALID_PARAM  One or more of the following is true:
 *                                    - Invalid device name location (vloc).
 *                                    - Invalid device name security mode.
 * @retval ::NRF_ERROR_INVALID_LENGTH One or more of the following is true:
 *                                    - The device name length is invalid (must be between 0 and @ref BLE_GAP_DEVNAME_MAX_LEN).
 *                                    - The device name length is too long for the given Attribute Table.
 * @retval ::NRF_ERROR_NOT_SUPPORTED  Device name security mode is not supported.
 */
typedef struct
{
  ble_gap_conn_sec_mode_t  write_perm;   /**< Write permissions. */
  uint8_t                  vloc:2;       /**< Value location, see @ref BLE_GATTS_VLOCS.*/
  uint8_t                 *p_value;      /**< Pointer to where the value (device name) is stored or will be stored. */
  uint16_t                 current_len;  /**< Current length in bytes of the memory pointed to by p_value.*/
  uint16_t                 max_len;      /**< Maximum length in bytes of the memory pointed to by p_value.*/
} ble_gap_cfg_device_name_t;


/**@brief Peripheral Preferred Connection Parameters include configuration parameters, set with @ref sd_ble_cfg_set. */
typedef struct
{
  uint8_t include_cfg;     /**< Inclusion configuration of the Peripheral Preferred Connection Parameters characteristic.
                                See @ref BLE_GAP_CHAR_INCL_CONFIG. Default is @ref BLE_GAP_PPCP_INCL_CONFIG_DEFAULT.
                                The Peripheral Preferred Connection Parameters characteristic configuration cannot be
                                changed to @ref BLE_GAP_CHAR_INCL_CONFIG_INCLUDE in this softdevice.
                            */
} ble_gap_cfg_ppcp_incl_cfg_t;


/**@brief Central Address Resolution include configuration parameters, set with @ref sd_ble_cfg_set. */
typedef struct
{
  uint8_t include_cfg;     /**< Inclusion configuration of the Central Address Resolution characteristic.
                                See @ref BLE_GAP_CHAR_INCL_CONFIG. Default is @ref BLE_GAP_CAR_INCL_CONFIG_DEFAULT. */
} ble_gap_cfg_car_incl_cfg_t;


/**@brief Configuration structure for GAP configurations. */
typedef union
{
  ble_gap_cfg_role_count_t      role_count_cfg;   /**< Role count configuration, cfg_id is @ref BLE_GAP_CFG_ROLE_COUNT. */
  ble_gap_cfg_device_name_t     device_name_cfg;  /**< Device name configuration, cfg_id is @ref BLE_GAP_CFG_DEVICE_NAME. */
  ble_gap_cfg_ppcp_incl_cfg_t   ppcp_include_cfg; /**< Peripheral Preferred Connection Parameters characteristic include
                                                       configuration, cfg_id is @ref BLE_GAP_CFG_PPCP_INCL_CONFIG. */
  ble_gap_cfg_car_incl_cfg_t    car_include_cfg;  /**< Central Address Resolution characteristic include configuration,
                                                       cfg_id is @ref BLE_GAP_CFG_CAR_INCL_CONFIG. */
} ble_gap_cfg_t;


/**@brief Channel Map option.
 *
 * @details Used with @ref sd_ble_opt_get to get the current channel map
 *          or @ref sd_ble_opt_set to set a new channel map. When setting the
 *          channel map, it applies to all current and future connections. When getting the
 *          current channel map, it applies to a single connection and the connection handle
 *          must be supplied.
 *
 * @note Setting the channel map may take some time, depending on connection parameters.
 *       The time taken may be different for each connection and the get operation will
 *       return the previous channel map until the new one has taken effect.
 *
 * @note After setting the channel map, by spec it can not be set again until at least 1 s has passed.
 *       See Bluetooth Specification Version 4.1 Volume 2, Part E, Section 7.3.46.
 *
 * @retval ::NRF_SUCCESS Get or set successful.
 * @retval ::NRF_ERROR_INVALID_PARAM One or more of the following is true:
 *                                   - Less then two bits in @ref ch_map are set.
 *                                   - Bits for primary advertising channels (37-39) are set.
 * @retval ::NRF_ERROR_BUSY Channel map was set again before enough time had passed.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied for get.
 *
 */
typedef struct
{
  uint16_t conn_handle;                   /**< Connection Handle (only applicable for get) */
  uint8_t ch_map[5];                      /**< Channel Map (37-bit). */
} ble_gap_opt_ch_map_t;



/**@brief Passkey Option.
 *
 *
 * @details Structure containing the passkey to be used during pairing. This can be used with @ref
 *          sd_ble_opt_set to make the SoftDevice use a preprogrammed passkey for authentication
 *          instead of generating a random one.
 *
 * @note Repeated pairing attempts using the same preprogrammed passkey makes pairing vulnerable to MITM attacks.
 *
 * @note @ref sd_ble_opt_get is not supported for this option.
 *
 */
typedef struct
{
  uint8_t const * p_passkey;                    /**< Pointer to 6-digit ASCII string (digit 0..9 only, no NULL termination) passkey to be used during pairing. If this is NULL, the SoftDevice will generate a random passkey if required.*/
} ble_gap_opt_passkey_t;


/**@brief Compatibility mode 1 option.
 *
 * @details This can be used with @ref sd_ble_opt_set to enable and disable
 *          compatibility mode 1. Compatibility mode 1 is disabled by default.
 *
 * @note Compatibility mode 1 enables interoperability with devices that do not support a value of
 *       0 for the WinOffset parameter in the Link Layer CONNECT_IND packet. This applies to a
 *       limited set of legacy peripheral devices from another vendor. Enabling this compatibility
 *       mode will only have an effect if the local device will act as a central device and
 *       initiate a connection to a peripheral device. In that case it may lead to the connection
 *       creation taking up to one connection interval longer to complete for all connections.
 *
 *  @retval ::NRF_SUCCESS Set successfully.
 *  @retval ::NRF_ERROR_INVALID_STATE When connection creation is ongoing while mode 1 is set.
 */
typedef struct
{
   uint8_t enable : 1;                           /**< Enable compatibility mode 1.*/
} ble_gap_opt_compat_mode_1_t;


/**@brief Authenticated payload timeout option.
 *
 * @details This can be used with @ref sd_ble_opt_set to change the Authenticated payload timeout to a value other
 *          than the default of @ref BLE_GAP_AUTH_PAYLOAD_TIMEOUT_MAX.
 *
 * @note The authenticated payload timeout event ::BLE_GAP_TIMEOUT_SRC_AUTH_PAYLOAD will be generated
 *       if auth_payload_timeout time has elapsed without receiving a packet with a valid MIC on an encrypted
 *       link.
 *
 * @note The LE ping procedure will be initiated before the timer expires to give the peer a chance
 *       to reset the timer. In addition the stack will try to prioritize running of LE ping over other
 *       activities to increase chances of finishing LE ping before timer expires. To avoid side-effects
 *       on other activities, it is recommended to use high timeout values.
 *       Recommended timeout > 2*(connInterval * (6 + connSlaveLatency)).
 *
 * @retval ::NRF_SUCCESS Set successfully.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied. auth_payload_timeout was outside of allowed range.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle parameter.
 */
typedef struct
{
  uint16_t   conn_handle;                       /**< Connection Handle */
  uint16_t   auth_payload_timeout;              /**< Requested timeout in 10 ms unit, see @ref BLE_GAP_AUTH_PAYLOAD_TIMEOUT. */
} ble_gap_opt_auth_payload_timeout_t;

/**@brief Enable event length checking.
 *
 * When this option is enabled, the SoftDevice will prevent the application from
 * switching to a state where the link can utilize more than the configured
 * event length. See @ref ble_gap_conn_cfg_t::event_length. This validation is
 * performed when the application calls the following APIs:
 *
 *       - @ref sd_ble_gap_connect
 *       - @ref sd_ble_gap_phy_update
 *
 * @note Event length checking is enabled by default.
 *
 * @note For a link with an event length of
 *       @ref BLE_GAP_EVENT_LENGTH_2MBPS_PHY_MIN, the application cannot connect
 *       directly on 1Mbps unless @ref BLE_GAP_OPT_ENABLE_EVT_LEN_CHECK is
 *       disabled. The application is allowed to enable this option after the
 *       link has switched to 2Mbps.
 *
 * @retval ::NRF_SUCCESS Set successfully.
 */
typedef struct
{
  uint8_t enable : 1;   /**< Set to 1 to enable (or 0 to disable) event length checks. */
} ble_gap_opt_enable_evt_len_check_t;

/**@brief Option structure for GAP options. */
typedef union
{
  ble_gap_opt_ch_map_t                  ch_map;                    /**< Parameters for the Channel Map option. */
  ble_gap_opt_passkey_t                 passkey;                   /**< Parameters for the Passkey option.*/
  ble_gap_opt_compat_mode_1_t           compat_mode_1;             /**< Parameters for the compatibility mode 1 option.*/
  ble_gap_opt_auth_payload_timeout_t    auth_payload_timeout;      /**< Parameters for the authenticated payload timeout option.*/
  ble_gap_opt_enable_evt_len_check_t    enable_evt_len_check;      /**< Parameters for enabling the event length checks. */
} ble_gap_opt_t;

/**@brief  Bluetooth Link Layer event triggering parameters. */
typedef struct
{
  uint8_t  ppi_ch_id;                /**< PPI channel to use. This channel should be regarded as reserved until
                                          Link Layer event PPI task triggering is stopped.
                                          The PPI channel ID can not be one of the PPI channels reserved by
                                          the SoftDevice. See @ref NRF_SOC_SD_PPI_CHANNELS_SD_ENABLED_MSK. */
  uint32_t task_endpoint;            /**< Task Endpoint to trigger. */
  uint16_t conn_evt_counter_start;   /**< The connection event on which the task triggering should start.
                                          This field is only used for connections and shall be 0 for all
                                          other roles. */
  uint16_t period_in_events;         /**< Trigger period. Valid range is [1, 32767].
                                          For connections, the application can configure the SoftDevice to
                                          trigger every N connection events. If the device is in slave role
                                          and slave latency is enabled, this parameter should be set to a
                                          multiple of (slave latency + 1) to ensure low power operation.
                                          This field is only used for connections and shall be 0 for all
                                          other roles. */
} ble_gap_event_trigger_t;

/**@brief   Quality of Service (QoS) channel survey parameters.
 *
 * @details This can be used with @ref sd_ble_gap_qos_start to start the QoS channel survey module.
 *          It is ignored with @ref sd_ble_gap_qos_stop.
 *
 * @details The channel survey module provides measurements of the energy levels on
 *          the Bluetooth Low Energy channels. When the module is enabled, @ref BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT
 *          events will periodically report the measured energy levels for each channel.
 *
 * @note    The measurements are scheduled with lower priority than other Bluetooth Low Energy roles,
 *          Radio Timeslot API events and Flash API events.
 *
 * @note    The channel survey module will attempt to do measurements so that the average interval
 *          between measurements will be interval_us. However due to the channel survey module
 *          having the lowest priority of all roles and modules, this may not be possible. In that
 *          case fewer than expected channel survey reports may be given.
 *
 * @note    In order to use the channel survey module, @ref ble_gap_cfg_role_count_t::qos_channel_survey_role_available
 *          must be set. This is done using @ref sd_ble_cfg_set.
 *
 * @note    The SoftDevice may generate one @ref BLE_GAP_EVT_QOS_CHANNEL_SURVEY_REPORT event after
 *          @ref sd_ble_gap_qos_stop is called.
 *
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter supplied. interval_us is out of the
 *                                   allowed range.
 * @retval ::NRF_ERROR_RESOURCES     The channel survey module is not available to the application.
 *                                   Set @ref ble_gap_cfg_role_count_t::qos_channel_survey_role_available using
 *                                   @ref sd_ble_cfg_set.
 */
typedef struct
{
  uint32_t interval_us;              /**< Requested average interval for the measurements and reports. See
                                          @ref BLE_GAP_QOS_CHANNEL_SURVEY_INTERVALS for valid ranges. If set
                                          to @ref BLE_GAP_QOS_CHANNEL_SURVEY_INTERVAL_CONTINUOUS, the channel
                                          survey role will be scheduled at every available opportunity. */
} ble_gap_qos_channel_survey_t;

/**@brief   Quality of Service (QoS) reporting changes in RSSI.
 *
 * @details This can be used with @ref sd_ble_gap_qos_start and @ref sd_ble_gap_qos_stop to start and stop
 *          reporting of changes in RSSI.
 *
 * @note    An RSSI change detected before the call but not yet received by the application
 *          may be reported after @ref sd_ble_gap_qos_stop has been called.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_RSSI_CHANGED, New RSSI data available. How often the event is generated is
 *                                       dependent on the settings of @ref ble_gap_qos_rssi_t::threshold_dbm
 *                                       and @ref ble_gap_qos_rssi_t::skip_count input parameters.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_RSSI_READ_MSC}
 * @mmsc{@ref BLE_GAP_RSSI_FILT_MSC}
 * @endmscs
 *
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
typedef struct
{
  uint16_t conn_handle;              /**< Connection handle. */
  uint8_t threshold_dbm;             /**< Minimum change in dBm before triggering the @ref BLE_GAP_EVT_RSSI_CHANGED event.
                                          Events are disabled if threshold_dbm equals @ref BLE_GAP_RSSI_THRESHOLD_INVALID.
                                          Ignored when used with @ref sd_ble_gap_qos_stop. */
  uint8_t skip_count;                /**< Number of RSSI samples with a change of threshold_dbm or more before sending
                                          a new @ref BLE_GAP_EVT_RSSI_CHANGED event.
                                          Ignored when used with @ref sd_ble_gap_qos_stop. */
} ble_gap_qos_rssi_t;

/**@brief Quality of Service (QoS) parameters. */
typedef union
{
  ble_gap_qos_channel_survey_t          channel_survey;            /**< Parameters for the channel survey. */
  ble_gap_qos_rssi_t                    rssi;                      /**< Parameters for the reporting changes in RSSI. */
} ble_gap_qos_params_t;
/**@} */

/**@addtogroup BLE_GAP_FUNCTIONS Functions
 * @{ */

/**@brief Set the local Bluetooth identity address.
 *
 *        The local Bluetooth identity address is the address that identifies this device to other peers.
 *        The address type must be either @ref BLE_GAP_ADDR_TYPE_PUBLIC or @ref BLE_GAP_ADDR_TYPE_RANDOM_STATIC.
 *
 * @note  The identity address cannot be changed while advertising, scanning or creating a connection.
 *
 * @note  This address will be distributed to the peer during bonding.
 *        If the address changes, the address stored in the peer device will not be valid and the ability to
 *        reconnect using the old address will be lost.
 *
 * @note  By default the SoftDevice will set an address of type @ref BLE_GAP_ADDR_TYPE_RANDOM_STATIC upon being
 *        enabled. The address is a random number populated during the IC manufacturing process and remains unchanged
 *        for the lifetime of each IC.
 *
 *
 * @param[in] p_addr Pointer to address structure.
 *
 * @retval ::NRF_SUCCESS Address successfully set.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::BLE_ERROR_GAP_INVALID_BLE_ADDR Invalid address.
 * @retval ::NRF_ERROR_BUSY The stack is busy, process pending events and retry.
 * @retval ::NRF_ERROR_INVALID_STATE The identity address cannot be changed while advertising,
 *                                   scanning or creating a connection.
 */
SVCALL(SD_BLE_GAP_ADDR_SET, uint32_t, sd_ble_gap_addr_set(ble_gap_addr_t const *p_addr));


/**@brief Get local Bluetooth identity address.
 *
 * @note  This will always return the identity address irrespective of the privacy settings,
 *        i.e. the address type will always be either @ref BLE_GAP_ADDR_TYPE_PUBLIC or @ref BLE_GAP_ADDR_TYPE_RANDOM_STATIC.
 *
 * @param[out] p_addr Pointer to address structure to be filled in.
 *
 * @retval ::NRF_SUCCESS Address successfully retrieved.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid or NULL pointer supplied.
 */
SVCALL(SD_BLE_GAP_ADDR_GET, uint32_t, sd_ble_gap_addr_get(ble_gap_addr_t *p_addr));


/**@brief Set the active whitelist in the SoftDevice.
 *
 * @note  Only one whitelist can be used at a time and the whitelist is shared between the BLE roles.
 *        The whitelist cannot be set if a BLE role is using the whitelist.
 *
 * @note  If an address is resolved using the information in the device identity list, then the whitelist
 *        filter policy applies to the peer identity address and not the resolvable address sent on air.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_PRIVACY_SCAN_PRIVATE_SCAN_MSC}
 * @endmscs
 *
 * @param[in] pp_wl_addrs Pointer to a whitelist of peer addresses, if NULL the whitelist will be cleared.
 * @param[in] len         Length of the whitelist, maximum @ref BLE_GAP_WHITELIST_ADDR_MAX_COUNT.
 *
 * @retval ::NRF_SUCCESS The whitelist is successfully set/cleared.
 * @retval ::NRF_ERROR_INVALID_ADDR The whitelist (or one of its entries) provided is invalid.
 * @retval ::BLE_ERROR_GAP_WHITELIST_IN_USE The whitelist is in use by a BLE role and cannot be set or cleared.
 * @retval ::BLE_ERROR_GAP_INVALID_BLE_ADDR Invalid address type is supplied.
 * @retval ::NRF_ERROR_DATA_SIZE The given whitelist size is invalid (zero or too large); this can only return when
 *                               pp_wl_addrs is not NULL.
 */
SVCALL(SD_BLE_GAP_WHITELIST_SET, uint32_t, sd_ble_gap_whitelist_set(ble_gap_addr_t const * const * pp_wl_addrs, uint8_t len));


/**@brief Set device identity list.
 *
 * @note  Only one device identity list can be used at a time and the list is shared between the BLE roles.
 *        The device identity list cannot be set if a BLE role is using the list.
 *
 * @param[in] pp_id_keys     Pointer to an array of peer identity addresses and peer IRKs, if NULL the device identity list will be cleared.
 * @param[in] pp_local_irks  Pointer to an array of local IRKs. Each entry in the array maps to the entry in pp_id_keys at the same index.
 *                           To fill in the list with the currently set device IRK for all peers, set to NULL.
 * @param[in] len            Length of the device identity list, maximum @ref BLE_GAP_DEVICE_IDENTITIES_MAX_COUNT.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_PRIVACY_SCAN_MSC}
 * @mmsc{@ref BLE_GAP_PRIVACY_SCAN_PRIVATE_SCAN_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_CONN_PRIV_MSC}
 * @endmscs
 *
 * @retval ::NRF_SUCCESS The device identity list successfully set/cleared.
 * @retval ::NRF_ERROR_INVALID_ADDR The device identity list (or one of its entries) provided is invalid.
 *                                  This code may be returned if the local IRK list also has an invalid entry.
 * @retval ::BLE_ERROR_GAP_DEVICE_IDENTITIES_IN_USE The device identity list is in use and cannot be set or cleared.
 * @retval ::BLE_ERROR_GAP_DEVICE_IDENTITIES_DUPLICATE The device identity list contains multiple entries with the same identity address.
 * @retval ::BLE_ERROR_GAP_INVALID_BLE_ADDR Invalid address type is supplied.
 * @retval ::NRF_ERROR_DATA_SIZE The given device identity list size invalid (zero or too large); this can
 *                               only return when pp_id_keys is not NULL.
 */
SVCALL(SD_BLE_GAP_DEVICE_IDENTITIES_SET, uint32_t, sd_ble_gap_device_identities_set(ble_gap_id_key_t const * const * pp_id_keys, ble_gap_irk_t const * const * pp_local_irks, uint8_t len));


/**@brief Set privacy settings.
 *
 * @note  Privacy settings cannot be changed while advertising, scanning or creating a connection.
 *
 * @param[in] p_privacy_params Privacy settings.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_PRIVACY_SCAN_MSC}
 * @endmscs
 *
 * @retval ::NRF_SUCCESS Set successfully.
 * @retval ::NRF_ERROR_BUSY The stack is busy, process pending events and retry.
 * @retval ::BLE_ERROR_GAP_INVALID_BLE_ADDR Invalid address type is supplied.
 * @retval ::NRF_ERROR_INVALID_ADDR The pointer to privacy settings is NULL or invalid.
 *                                  Otherwise, the p_device_irk pointer in privacy parameter is an invalid pointer.
 * @retval ::NRF_ERROR_INVALID_PARAM Out of range parameters are provided.
 * @retval ::NRF_ERROR_NOT_SUPPORTED The SoftDevice does not support privacy if the Central Address Resolution
                                     characteristic is not configured to be included and the SoftDevice is configured
                                     to support central roles.
                                     See @ref ble_gap_cfg_car_incl_cfg_t and @ref ble_gap_cfg_role_count_t.
 * @retval ::NRF_ERROR_INVALID_STATE Privacy settings cannot be changed while advertising, scanning
 *                                   or creating a connection.
 */
SVCALL(SD_BLE_GAP_PRIVACY_SET, uint32_t, sd_ble_gap_privacy_set(ble_gap_privacy_params_t const *p_privacy_params));


/**@brief Get privacy settings.
 *
 * @note ::ble_gap_privacy_params_t::p_device_irk must be initialized to NULL or a valid address before this function is called.
 *       If it is initialized to a valid address, the address pointed to will contain the current device IRK on return.
 *
 * @param[in,out] p_privacy_params Privacy settings.
 *
 * @retval ::NRF_SUCCESS            Privacy settings read.
 * @retval ::NRF_ERROR_INVALID_ADDR The pointer given for returning the privacy settings may be NULL or invalid.
 *                                  Otherwise, the p_device_irk pointer in privacy parameter is an invalid pointer.
 */
SVCALL(SD_BLE_GAP_PRIVACY_GET, uint32_t, sd_ble_gap_privacy_get(ble_gap_privacy_params_t *p_privacy_params));


/**@brief Update connection parameters.
 *
 * @details In the central role this will initiate a Link Layer connection parameter update procedure.
 *          This function can be used as a central both to reply to a @ref BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST or to start the procedure unrequested.
 *          Regardless of success or failure, the application
 *          will be informed of the result with a @ref BLE_GAP_EVT_CONN_PARAM_UPDATE event.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_CONN_PARAM_UPDATE, Result of the connection parameter update procedure.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_CPU_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_ENC_AUTH_MUTEX_MSC}
 * @mmsc{@ref BLE_GAP_MULTILINK_CPU_MSC}
 * @mmsc{@ref BLE_GAP_MULTILINK_CTRL_PROC_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] p_conn_params  Pointer to desired connection parameters.
 *                           If NULL is provided on a central role and in response to a @ref BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST, the peripheral request will be rejected.
 *
 * @retval ::NRF_SUCCESS The Connection Update procedure has been started successfully.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied, check parameter limits and constraints.
 * @retval ::NRF_ERROR_INVALID_STATE Either:
 *                                   - Disconnection in progress
 *                                   - Link has not been established.
 * @retval ::NRF_ERROR_BUSY Procedure already in progress, wait for pending procedures to complete and retry.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_NO_MEM Not enough memory to complete operation.
 */
SVCALL(SD_BLE_GAP_CONN_PARAM_UPDATE, uint32_t, sd_ble_gap_conn_param_update(uint16_t conn_handle, ble_gap_conn_params_t const *p_conn_params));


/**@brief Disconnect (GAP Link Termination).
 *
 * @details This call initiates the disconnection procedure, and its completion will be communicated to the application
 *          with a @ref BLE_GAP_EVT_DISCONNECTED event.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_DISCONNECTED, Generated when disconnection procedure is complete.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_CONN_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] hci_status_code HCI status code, see @ref BLE_HCI_STATUS_CODES (accepted values are @ref BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION and @ref BLE_HCI_CONN_INTERVAL_UNACCEPTABLE).
 *
 * @retval ::NRF_SUCCESS The disconnection procedure has been started successfully.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Disconnection in progress or link has not been established.
 */
SVCALL(SD_BLE_GAP_DISCONNECT, uint32_t, sd_ble_gap_disconnect(uint16_t conn_handle, uint8_t hci_status_code));


/**@brief Set the radio's transmit power.
 *
 * @param[in] role The role to set the transmit power for, see @ref BLE_GAP_TX_POWER_ROLES for
 *                 possible roles.
 * @param[in] handle   The handle parameter is interpreted depending on role:
 *                     - If role is @ref BLE_GAP_TX_POWER_ROLE_CONN, this value is the specific connection handle.
 *                     - For all other roles, handle is ignored.
 * @param[in] tx_power Radio transmit power in dBm (see note for accepted values).
 *
 * @note Supported tx_power values: -40dBm, -20dBm, -16dBm, -12dBm, -8dBm, -4dBm, 0dBm, +3dBm and +4dBm.
 * @note The initiator will have the same transmit power as the scanner.
 * @note When a connection is created it will inherit the transmit power from the initiator or
 *       advertiser leading to the connection.
 *
 * @retval ::NRF_SUCCESS Successfully changed the transmit power.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::BLE_ERROR_INVALID_ADV_HANDLE Advertising handle not found.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_TX_POWER_SET, uint32_t, sd_ble_gap_tx_power_set(uint8_t role, uint16_t handle, int8_t tx_power));


/**@brief Set GAP Appearance value.
 *
 * @param[in] appearance Appearance (16-bit), see @ref BLE_APPEARANCES.
 *
 * @retval ::NRF_SUCCESS  Appearance value set successfully.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 */
SVCALL(SD_BLE_GAP_APPEARANCE_SET, uint32_t, sd_ble_gap_appearance_set(uint16_t appearance));


/**@brief Get GAP Appearance value.
 *
 * @param[out] p_appearance Pointer to appearance (16-bit) to be filled in, see @ref BLE_APPEARANCES.
 *
 * @retval ::NRF_SUCCESS Appearance value retrieved successfully.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 */
SVCALL(SD_BLE_GAP_APPEARANCE_GET, uint32_t, sd_ble_gap_appearance_get(uint16_t *p_appearance));


/**@brief Set GAP device name.
 *
 * @note  If the device name is located in application flash memory (see @ref ble_gap_cfg_device_name_t),
 *        it cannot be changed. Then @ref NRF_ERROR_FORBIDDEN will be returned.
 *
 * @param[in] p_write_perm Write permissions for the Device Name characteristic, see @ref ble_gap_conn_sec_mode_t.
 * @param[in] p_dev_name Pointer to a UTF-8 encoded, <b>non NULL-terminated</b> string.
 * @param[in] len Length of the UTF-8, <b>non NULL-terminated</b> string pointed to by p_dev_name in octets (must be smaller or equal than @ref BLE_GAP_DEVNAME_MAX_LEN).
 *
 * @retval ::NRF_SUCCESS GAP device name and permissions set successfully.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_DATA_SIZE Invalid data size(s) supplied.
 * @retval ::NRF_ERROR_FORBIDDEN Device name is not writable.
 */
SVCALL(SD_BLE_GAP_DEVICE_NAME_SET, uint32_t, sd_ble_gap_device_name_set(ble_gap_conn_sec_mode_t const *p_write_perm, uint8_t const *p_dev_name, uint16_t len));


/**@brief Get GAP device name.
 *
 * @note  If the device name is longer than the size of the supplied buffer,
 *        p_len will return the complete device name length,
 *        and not the number of bytes actually returned in p_dev_name.
 *        The application may use this information to allocate a suitable buffer size.
 *
 * @param[out]    p_dev_name Pointer to an empty buffer where the UTF-8 <b>non NULL-terminated</b> string will be placed. Set to NULL to obtain the complete device name length.
 * @param[in,out] p_len      Length of the buffer pointed by p_dev_name, complete device name length on output.
 *
 * @retval ::NRF_SUCCESS GAP device name retrieved successfully.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_DATA_SIZE Invalid data size(s) supplied.
 */
SVCALL(SD_BLE_GAP_DEVICE_NAME_GET, uint32_t, sd_ble_gap_device_name_get(uint8_t *p_dev_name, uint16_t *p_len));


/**@brief Initiate the GAP Authentication procedure.
 *
 * @details In the central role, this function will send an SMP Pairing Request (or an SMP Pairing Failed if rejected).
 *
 * @events
 * @event{Depending on the security parameters set and the packet exchanges with the peer\, the following events may be generated:}
 * @event{@ref BLE_GAP_EVT_SEC_PARAMS_REQUEST}
 * @event{@ref BLE_GAP_EVT_PASSKEY_DISPLAY}
 * @event{@ref BLE_GAP_EVT_KEY_PRESSED}
 * @event{@ref BLE_GAP_EVT_AUTH_KEY_REQUEST}
 * @event{@ref BLE_GAP_EVT_LESC_DHKEY_REQUEST}
 * @event{@ref BLE_GAP_EVT_CONN_SEC_UPDATE}
 * @event{@ref BLE_GAP_EVT_AUTH_STATUS}
 * @event{@ref BLE_GAP_EVT_TIMEOUT}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_SEC_REQ_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_ENC_AUTH_MUTEX_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_PAIRING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_PK_PERIPH_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_PK_PERIPH_OOB_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_PAIRING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_NC_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_PD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_CD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_OOB_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] p_sec_params Pointer to the @ref ble_gap_sec_params_t structure with the security parameters to be used during the pairing or bonding procedure.
 *                         In the central role, this pointer may be NULL to reject a Security Request.
 *
 * @retval ::NRF_SUCCESS Successfully initiated authentication procedure.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Invalid state to perform operation. Either:
 *                                   - No link has been established.
 *                                   - An encryption is already executing or queued.
 * @retval ::NRF_ERROR_NO_MEM The maximum number of authentication procedures that can run in parallel for the given role is reached.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_NOT_SUPPORTED Setting of sign or link fields in @ref ble_gap_sec_kdist_t not supported.
 *                                   Distribution of own Identity Information is only supported if the Central
 *                                   Address Resolution characteristic is configured to be included or
 *                                   the Softdevice is configured to support peripheral roles only.
 *                                   See @ref ble_gap_cfg_car_incl_cfg_t and @ref ble_gap_cfg_role_count_t.
 * @retval ::NRF_ERROR_TIMEOUT A SMP timeout has occurred, and further SMP operations on this link is prohibited.
 */
SVCALL(SD_BLE_GAP_AUTHENTICATE, uint32_t, sd_ble_gap_authenticate(uint16_t conn_handle, ble_gap_sec_params_t const *p_sec_params));


/**@brief Reply with GAP security parameters.
 *
 * @details This function is only used to reply to a @ref BLE_GAP_EVT_SEC_PARAMS_REQUEST, calling it at other times will result in an @ref NRF_ERROR_INVALID_STATE.
 * @note    If the call returns an error code, the request is still pending, and the reply call may be repeated with corrected parameters.
 *
 * @events
 * @event{This function is used during authentication procedures, see the list of events in the documentation of @ref sd_ble_gap_authenticate.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_PAIRING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_PK_PERIPH_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_PK_PERIPH_OOB_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_PAIRING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_NC_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_PD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_CD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_OOB_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] sec_status Security status, see @ref BLE_GAP_SEC_STATUS.
 * @param[in] p_sec_params Pointer to a @ref ble_gap_sec_params_t security parameters structure. In the central role this must be set to NULL, as the parameters have
 *                         already been provided during a previous call to @ref sd_ble_gap_authenticate.
 * @param[in,out] p_sec_keyset Pointer to a @ref ble_gap_sec_keyset_t security keyset structure. Any keys generated and/or distributed as a result of the ongoing security procedure
 *                         will be stored into the memory referenced by the pointers inside this structure. The keys will be stored and available to the application
 *                         upon reception of a @ref BLE_GAP_EVT_AUTH_STATUS event.
 *                         Note that the SoftDevice expects the application to provide memory for storing the
 *                         peer's keys. So it must be ensured that the relevant pointers inside this structure are not NULL. The pointers to the local key
 *                         can, however, be NULL, in which case, the local key data will not be available to the application upon reception of the
 *                         @ref BLE_GAP_EVT_AUTH_STATUS event.
 *
 * @retval ::NRF_SUCCESS Successfully accepted security parameter from the application.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_BUSY The stack is busy, process pending events and retry.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Security parameters has not been requested.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_NOT_SUPPORTED Setting of sign or link fields in @ref ble_gap_sec_kdist_t not supported.
 *                                   Distribution of own Identity Information is only supported if the Central
 *                                   Address Resolution characteristic is configured to be included or
 *                                   the Softdevice is configured to support peripheral roles only.
 *                                   See @ref ble_gap_cfg_car_incl_cfg_t and @ref ble_gap_cfg_role_count_t.
 */
SVCALL(SD_BLE_GAP_SEC_PARAMS_REPLY, uint32_t, sd_ble_gap_sec_params_reply(uint16_t conn_handle, uint8_t sec_status, ble_gap_sec_params_t const *p_sec_params, ble_gap_sec_keyset_t const *p_sec_keyset));


/**@brief Reply with an authentication key.
 *
 * @details This function is only used to reply to a @ref BLE_GAP_EVT_AUTH_KEY_REQUEST or a @ref BLE_GAP_EVT_PASSKEY_DISPLAY, calling it at other times will result in an @ref NRF_ERROR_INVALID_STATE.
 * @note    If the call returns an error code, the request is still pending, and the reply call may be repeated with corrected parameters.
 *
 * @events
 * @event{This function is used during authentication procedures\, see the list of events in the documentation of @ref sd_ble_gap_authenticate.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_BONDING_PK_PERIPH_OOB_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_NC_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_CD_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] key_type See @ref BLE_GAP_AUTH_KEY_TYPES.
 * @param[in] p_key If key type is @ref BLE_GAP_AUTH_KEY_TYPE_NONE, then NULL.
 *                  If key type is @ref BLE_GAP_AUTH_KEY_TYPE_PASSKEY, then a 6-byte ASCII string (digit 0..9 only, no NULL termination)
 *                     or NULL when confirming LE Secure Connections Numeric Comparison.
 *                  If key type is @ref BLE_GAP_AUTH_KEY_TYPE_OOB, then a 16-byte OOB key value in little-endian format.
 *
 * @retval ::NRF_SUCCESS Authentication key successfully set.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Authentication key has not been requested.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_AUTH_KEY_REPLY, uint32_t, sd_ble_gap_auth_key_reply(uint16_t conn_handle, uint8_t key_type, uint8_t const *p_key));


/**@brief Reply with an LE Secure connections DHKey.
 *
 * @details This function is only used to reply to a @ref BLE_GAP_EVT_LESC_DHKEY_REQUEST, calling it at other times will result in an @ref NRF_ERROR_INVALID_STATE.
 * @note    If the call returns an error code, the request is still pending, and the reply call may be repeated with corrected parameters.
 *
 * @events
 * @event{This function is used during authentication procedures\, see the list of events in the documentation of @ref sd_ble_gap_authenticate.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_PAIRING_JW_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_NC_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_PD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_CD_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_OOB_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] p_dhkey LE Secure Connections DHKey.
 *
 * @retval ::NRF_SUCCESS DHKey successfully set.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Invalid state to perform operation. Either:
 *                                   - The peer is not authenticated.
 *                                   - The application has not pulled a @ref BLE_GAP_EVT_LESC_DHKEY_REQUEST event.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_LESC_DHKEY_REPLY, uint32_t, sd_ble_gap_lesc_dhkey_reply(uint16_t conn_handle, ble_gap_lesc_dhkey_t const *p_dhkey));


/**@brief Notify the peer of a local keypress.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_PKE_CD_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] kp_not See @ref BLE_GAP_KP_NOT_TYPES.
 *
 * @retval ::NRF_SUCCESS Keypress notification successfully queued for transmission.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Invalid state to perform operation. Either:
 *                                   - Authentication key not requested.
 *                                   - Passkey has not been entered.
 *                                   - Keypresses have not been enabled by both peers.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_BUSY The BLE stack is busy. Retry at later time.
 */
SVCALL(SD_BLE_GAP_KEYPRESS_NOTIFY, uint32_t, sd_ble_gap_keypress_notify(uint16_t conn_handle, uint8_t kp_not));


/**@brief Generate a set of OOB data to send to a peer out of band.
 *
 * @note  The @ref ble_gap_addr_t included in the OOB data returned will be the currently active one (or, if a connection has already been established,
 *        the one used during connection setup). The application may manually overwrite it with an updated value.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_OOB_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle. Can be @ref BLE_CONN_HANDLE_INVALID if a BLE connection has not been established yet.
 * @param[in] p_pk_own LE Secure Connections local P-256 Public Key.
 * @param[out] p_oobd_own The OOB data to be sent out of band to a peer.
 *
 * @retval ::NRF_SUCCESS OOB data successfully generated.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_LESC_OOB_DATA_GET, uint32_t, sd_ble_gap_lesc_oob_data_get(uint16_t conn_handle, ble_gap_lesc_p256_pk_t const *p_pk_own, ble_gap_lesc_oob_data_t *p_oobd_own));

/**@brief Provide the OOB data sent/received out of band.
 *
 * @note  An authentication procedure with OOB selected as an algorithm must be in progress when calling this function.
 * @note  A @ref BLE_GAP_EVT_LESC_DHKEY_REQUEST event with the oobd_req set to 1 must have been received prior to calling this function.
 *
 * @events
 * @event{This function is used during authentication procedures\, see the list of events in the documentation of @ref sd_ble_gap_authenticate.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_LESC_BONDING_OOB_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] p_oobd_own The OOB data sent out of band to a peer or NULL if the peer has not received OOB data.
 *                       Must correspond to @ref ble_gap_sec_params_t::oob flag in @ref BLE_GAP_EVT_SEC_PARAMS_REQUEST.
 * @param[in] p_oobd_peer The OOB data received out of band from a peer or NULL if none received.
 *                        Must correspond to @ref ble_gap_sec_params_t::oob flag
 *                        in @ref sd_ble_gap_authenticate in the central role or
 *                        in @ref sd_ble_gap_sec_params_reply in the peripheral role.
 *
 * @retval ::NRF_SUCCESS OOB data accepted.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Invalid state to perform operation. Either:
 *                                   - Authentication key not requested
 *                                   - Not expecting LESC OOB data
 *                                   - Have not actually exchanged passkeys.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_LESC_OOB_DATA_SET, uint32_t, sd_ble_gap_lesc_oob_data_set(uint16_t conn_handle, ble_gap_lesc_oob_data_t const *p_oobd_own, ble_gap_lesc_oob_data_t const *p_oobd_peer));


/**@brief Initiate GAP Encryption procedure.
 *
 * @details In the central role, this function will initiate the encryption procedure using the encryption information provided.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_CONN_SEC_UPDATE, The connection security has been updated.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_ENC_AUTH_MUTEX_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_ENC_MSC}
 * @mmsc{@ref BLE_GAP_MULTILINK_CTRL_PROC_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_SEC_REQ_MSC}
 * @endmscs
 *
 * @param[in] conn_handle Connection handle.
 * @param[in] p_master_id Pointer to a @ref ble_gap_master_id_t master identification structure.
 * @param[in] p_enc_info  Pointer to a @ref ble_gap_enc_info_t encryption information structure.
 *
 * @retval ::NRF_SUCCESS Successfully initiated authentication procedure.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_STATE No link has been established.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::BLE_ERROR_INVALID_ROLE Operation is not supported in the Peripheral role.
 * @retval ::NRF_ERROR_BUSY Procedure already in progress or not allowed at this time, wait for pending procedures to complete and retry.
 * @retval ::BLE_ERROR_UNSUPPORTED_REMOTE_FEATURE Peer has indicated that it does not support Encryption procedure.
 */
SVCALL(SD_BLE_GAP_ENCRYPT, uint32_t, sd_ble_gap_encrypt(uint16_t conn_handle, ble_gap_master_id_t const *p_master_id, ble_gap_enc_info_t const *p_enc_info));


/**@brief Get the current connection security.
 *
 * @param[in]  conn_handle Connection handle.
 * @param[out] p_conn_sec  Pointer to a @ref ble_gap_conn_sec_t structure to be filled in.
 *
 * @retval ::NRF_SUCCESS Current connection security successfully retrieved.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 */
SVCALL(SD_BLE_GAP_CONN_SEC_GET, uint32_t, sd_ble_gap_conn_sec_get(uint16_t conn_handle, ble_gap_conn_sec_t *p_conn_sec));


/**@brief Get the received signal strength for the last connection event.
 *
 *        @ref sd_ble_gap_qos_start with @ref BLE_GAP_QOS_RSSI must be called to start reporting RSSI before using this function. @ref NRF_ERROR_NOT_FOUND
 *        will be returned until RSSI was sampled for the first time after calling @ref sd_ble_gap_qos_start.
 * @note ERRATA-153 and ERRATA-225 require the rssi sample to be compensated based on a temperature measurement.
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_RSSI_READ_MSC}
 * @endmscs
 *
 * @param[in]  conn_handle Connection handle.
 * @param[out] p_rssi      Pointer to the location where the RSSI measurement shall be stored.
 * @param[out] p_ch_index  Pointer to the location where Channel Index for the RSSI measurement shall be stored.
 *
 * @retval ::NRF_SUCCESS                   Successfully read the RSSI.
 * @retval ::NRF_ERROR_NOT_FOUND           No sample is available.
 * @retval ::NRF_ERROR_INVALID_ADDR        Invalid pointer supplied.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_INVALID_STATE       RSSI reporting is not ongoing.
 */
SVCALL(SD_BLE_GAP_RSSI_GET, uint32_t, sd_ble_gap_rssi_get(uint16_t conn_handle, int8_t *p_rssi, uint8_t *p_ch_index));


/**@brief Start or continue scanning (GAP Discovery procedure, Observer Procedure).
 *
 * @note    A call to this function will require the application to keep the memory pointed by
 *          p_adv_report_buffer alive until the buffer is released. The buffer is released when the scanner is stopped
 *          or when this function is called with another buffer.
 *
 * @note    The scanner will automatically stop in the following cases:
 *           - @ref sd_ble_gap_scan_stop is called.
 *           - @ref sd_ble_gap_connect is called.
 *           - A @ref BLE_GAP_EVT_TIMEOUT with source set to @ref BLE_GAP_TIMEOUT_SRC_SCAN is received.
 *           - When a @ref BLE_GAP_EVT_ADV_REPORT event is received.
 *             In this case scanning is only paused to let the application access received data.
 *             The application must call this function to continue scanning, or call
 *             @ref sd_ble_gap_scan_stop to stop scanning.
 *
 * @note    When the scanner is paused, the application should continue scanning
 *          as soon as possible after fetching the @ref BLE_GAP_EVT_ADV_REPORT event.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_ADV_REPORT, An advertising or scan response packet has been received.}
 * @event{@ref BLE_GAP_EVT_TIMEOUT, Scanner has timed out.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_SCAN_MSC}
 * @endmscs
 *
 * @param[in] p_scan_params       Pointer to scan parameters structure. When this function is used to continue
 *                                scanning, this parameter must be NULL.
 * @param[in] p_adv_report_buffer Pointer to buffer used to store incoming advertising data.
 *                                The memory pointed to should be kept alive until the scanning is stopped.
 *                                See @ref BLE_GAP_SCAN_BUFFER_SIZE for minimum and maximum buffer size.
 *
 * @retval ::NRF_SUCCESS Successfully initiated scanning procedure.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Invalid state to perform operation. Either:
 *                                   - Scanning is already ongoing and p_scan_params was not NULL
 *                                   - Scanning is not running and p_scan_params was NULL.
 *                                   - The scanner has timed out when this function is called to continue scanning.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied. See @ref ble_gap_scan_params_t.
 * @retval ::NRF_ERROR_NOT_SUPPORTED Unsupported parameters supplied. See @ref ble_gap_scan_params_t.
 * @retval ::NRF_ERROR_INVALID_LENGTH The provided buffer length is invalid. See @ref BLE_GAP_SCAN_BUFFER_MIN.
 * @retval ::NRF_ERROR_RESOURCES Not enough BLE role slots available.
 *                               Stop one or more currently active roles (Central, Peripheral or Broadcaster) and try again
 * @retval ::NRF_ERROR_NOT_SUPPORTED Unsupported PHYs supplied to the call.
 */
SVCALL(SD_BLE_GAP_SCAN_START, uint32_t, sd_ble_gap_scan_start(ble_gap_scan_params_t const *p_scan_params, ble_data_t const * p_adv_report_buffer));


/**@brief Stop scanning (GAP Discovery procedure, Observer Procedure).
 *
 * @note The buffer provided in @ref sd_ble_gap_scan_start is released.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_SCAN_MSC}
 * @endmscs
 *
 * @retval ::NRF_SUCCESS Successfully stopped scanning procedure.
 * @retval ::NRF_ERROR_INVALID_STATE Not in the scanning state.
 */
SVCALL(SD_BLE_GAP_SCAN_STOP, uint32_t, sd_ble_gap_scan_stop(void));


/**@brief Create a connection (GAP Link Establishment).
 *
 * @note If a scanning procedure is currently in progress it will be automatically stopped when calling this function.
 *       The scanning procedure will be stopped even if the function returns an error.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_CONNECTED, A connection was established.}
 * @event{@ref BLE_GAP_EVT_TIMEOUT, Failed to establish a connection.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_CONN_PRIV_MSC}
 * @mmsc{@ref BLE_GAP_CENTRAL_CONN_MSC}
 * @endmscs
 *
 * @param[in] p_peer_addr   Pointer to peer identity address. If @ref ble_gap_scan_params_t::filter_policy is set to use
 *                          whitelist, then p_peer_addr is ignored.
 * @param[in] p_scan_params Pointer to scan parameters structure.
 * @param[in] p_conn_params Pointer to desired connection parameters.
 * @param[in] conn_cfg_tag  Tag identifying a configuration set by @ref sd_ble_cfg_set or
 *                          @ref BLE_CONN_CFG_TAG_DEFAULT to use the default connection configuration.
 *
 * @retval ::NRF_SUCCESS Successfully initiated connection procedure.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid parameter(s) pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 *                                   - Invalid parameter(s) in p_scan_params or p_conn_params.
 *                                   - Use of whitelist requested but whitelist has not been set, see @ref sd_ble_gap_whitelist_set.
 *                                   - Peer address was not present in the device identity list, see @ref sd_ble_gap_device_identities_set.
 * @retval ::NRF_ERROR_NOT_FOUND conn_cfg_tag not found.
 * @retval ::NRF_ERROR_INVALID_STATE The SoftDevice is in an invalid state to perform this operation.
 *                                   - A locally initiated connect procedure must complete before initiating again.
 * @retval ::BLE_ERROR_GAP_INVALID_BLE_ADDR Invalid Peer address.
 * @retval ::NRF_ERROR_CONN_COUNT The limit of available connections for this connection configuration tag has been reached.
 *                                To increase the number of available connections,
 *                                use @ref sd_ble_cfg_set with @ref BLE_GAP_CFG_ROLE_COUNT or @ref BLE_CONN_CFG_GAP.
 * @retval ::NRF_ERROR_RESOURCES Either:
 *                                 - Not enough BLE role slots available.
 *                                   Stop one or more currently active roles (Central, Peripheral or Observer) and try again.
 *                                 - The event_length parameter associated with conn_cfg_tag is too small to be able to
 *                                   establish a connection on the selected @ref ble_gap_scan_params_t::scan_phys.
 *                                   Use @ref sd_ble_cfg_set to increase the event length.
 * @retval ::NRF_ERROR_NOT_SUPPORTED Unsupported PHYs supplied to the call.
 */
SVCALL(SD_BLE_GAP_CONNECT, uint32_t, sd_ble_gap_connect(ble_gap_addr_t const *p_peer_addr, ble_gap_scan_params_t const *p_scan_params, ble_gap_conn_params_t const *p_conn_params, uint8_t conn_cfg_tag));


/**@brief Cancel a connection establishment.
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_CONN_MSC}
 * @endmscs
 *
 * @retval ::NRF_SUCCESS Successfully canceled an ongoing connection procedure.
 * @retval ::NRF_ERROR_INVALID_STATE No locally initiated connect procedure started or connection
 *                                   completed occurred.
 */
SVCALL(SD_BLE_GAP_CONNECT_CANCEL, uint32_t, sd_ble_gap_connect_cancel(void));


/**@brief Initiate or respond to a PHY Update Procedure
 *
 * @details   This function is used to initiate or respond to a PHY Update Procedure. It will always
 *            generate a @ref BLE_GAP_EVT_PHY_UPDATE event if successfully executed.
 *            If this function is used to initiate a PHY Update procedure and the only option
 *            provided in @ref ble_gap_phys_t::tx_phys and @ref ble_gap_phys_t::rx_phys is the
 *            currently active PHYs in the respective directions, the SoftDevice will generate a
 *            @ref BLE_GAP_EVT_PHY_UPDATE with the current PHYs set and will not initiate the
 *            procedure in the Link Layer.
 *
 *            If @ref ble_gap_phys_t::tx_phys or @ref ble_gap_phys_t::rx_phys is @ref BLE_GAP_PHY_AUTO,
 *            then the stack will select PHYs based on the peer's PHY preferences and the local link
 *            configuration. The PHY Update procedure will for this case result in a PHY combination
 *            that respects the time constraints configured with @ref sd_ble_cfg_set and the current
 *            link layer data length.
 *
 *            When acting as a central, the SoftDevice will select the fastest common PHY in each direction.
 *
 *            If the peer responds the PHY Update Procedure with unknown response, then the resulting
 *            @ref BLE_GAP_EVT_PHY_UPDATE event will have a status set to
 *            @ref BLE_HCI_UNSUPPORTED_REMOTE_FEATURE.
 *
 *            If the PHY Update procedure was rejected by the peer due to a procedure collision, the status
 *            will be @ref BLE_HCI_STATUS_CODE_LMP_ERROR_TRANSACTION_COLLISION or
 *            @ref BLE_HCI_DIFFERENT_TRANSACTION_COLLISION.
 *            If the peer responds to the PHY Update procedure with invalid parameters, the status
 *            will be @ref BLE_HCI_STATUS_CODE_INVALID_LMP_PARAMETERS.
 *            If the PHY Update procedure was rejected by the peer for a different reason, the status will
 *            contain the reason as specified by the peer.
 *
 * @events
 * @event{@ref BLE_GAP_EVT_PHY_UPDATE, Result of the PHY Update Procedure.}
 * @endevents
 *
 * @mscs
 * @mmsc{@ref BLE_GAP_CENTRAL_PHY_UPDATE}
 * @endmscs
 *
 * @param[in] conn_handle   Connection handle to indicate the connection for which the PHY Update is requested.
 * @param[in] p_gap_phys    Pointer to PHY structure.
 *
 * @retval ::NRF_SUCCESS Successfully requested a PHY Update.
 * @retval ::NRF_ERROR_INVALID_ADDR Invalid pointer supplied.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter(s) supplied.
 * @retval ::BLE_ERROR_UNSUPPORTED_REMOTE_FEATURE Peer has indicated before that it does not support PHY Update procedure.
 *                                                Note that this only applies if the PHY Update procedure is self initiated.
 * @retval ::NRF_ERROR_NOT_SUPPORTED Unsupported PHYs supplied to the call.
 * @retval ::NRF_ERROR_INVALID_STATE No link has been established.
 * @retval ::NRF_ERROR_BUSY Procedure is already in progress or not allowed at this time. Process pending events and wait for the pending procedure to complete and retry.
 *
 */
SVCALL(SD_BLE_GAP_PHY_UPDATE, uint32_t, sd_ble_gap_phy_update(uint16_t conn_handle, ble_gap_phys_t const *p_gap_phys));



/**@brief   Start Quality of Service (QoS) reporting.
 *
 * @param[in] qos_id QoS ID, see @ref BLE_GAP_QOS_IDS.
 * @param[in] p_qos_params Pointer to a @ref ble_gap_qos_params_t structure containing the QoS parameters.
 *
 * @retval ::NRF_SUCCESS             The QoS reporting is successfully started.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter supplied. qos_id is out of the
 *                                   allowed range.
 * @retval ::NRF_ERROR_INVALID_ADDR  Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Trying to start the reporting when already started.
 *
 * @note For a complete set of return values see @ref ble_gap_qos_params_t.
 */
SVCALL(SD_BLE_GAP_QOS_START, uint32_t, sd_ble_gap_qos_start(uint32_t qos_id, ble_gap_qos_params_t const *p_qos_params));

/**@brief   Stop Quality of Service (QoS) reporting.
 *
 * @param[in] qos_id QoS ID, see @ref BLE_GAP_QOS_IDS.
 * @param[in] p_qos_params Pointer to a @ref ble_gap_qos_params_t structure containing the QoS parameters.
 *
 * @retval ::NRF_SUCCESS             The QoS reporting is successfully stopped.
 * @retval ::NRF_ERROR_INVALID_PARAM Invalid parameter supplied. qos_id is out of the
 *                                   allowed range.
 * @retval ::NRF_ERROR_INVALID_ADDR  Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_STATE Trying to stop the reporting when it is not started.
 *
 * @note For a complete set of return values see @ref ble_gap_qos_params_t.
 */
SVCALL(SD_BLE_GAP_QOS_STOP, uint32_t, sd_ble_gap_qos_stop(uint32_t qos_id, ble_gap_qos_params_t const *p_qos_params));


/**@brief   Obtain the next connection event counter value.
 *
 * @details The connection event counter is initialized to zero on the first connection event. The value is incremented
 *          by one for each connection event. For more information see Bluetooth Core Specification v5.0, Vol 6, Part B,
 *          Section 4.5.1.
 *
 * @note    The connection event counter obtained through this API will be outdated if this API is called
 *          at the same time as the connection event counter is incremented.
 *
 * @note    This API will always return the last connection event counter + 1.
 *          The actual connection event may be multiple connection events later if:
 *           - Slave latency is enabled and there is no data to transmit or receive.
 *           - Another role is scheduled with a higher priority at the same time as the next connection event.
 *
 * @param[in]   conn_handle       Connection handle.
 * @param[out]  p_counter         Pointer to the variable where the next connection event counter will be written.
 *
 * @retval ::NRF_SUCCESS                   The connection event counter was successfully retrieved.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle parameter supplied.
 * @retval ::NRF_ERROR_INVALID_ADDR        Invalid pointer supplied.
 */
SVCALL(SD_BLE_GAP_NEXT_CONN_EVT_COUNTER_GET, uint32_t, sd_ble_gap_next_conn_evt_counter_get(uint16_t conn_handle, uint16_t * p_counter));


/**@brief   Start triggering a given task on the start of a Bluetooth Link Layer event.
 *
 * @details When enabled, this feature will trigger a PPI task at the start of Bluetooth Link Layer events.
 *
 * @param[in]      role          The role to start the event trigger for.
 *                               The following roles are supported on this SoftDevice:
 *                               - @ref BLE_GAP_LL_ROLE_CONN
 *                               - @ref BLE_GAP_LL_ROLE_SCAN
 *                               - @ref BLE_GAP_LL_ROLE_INIT
 * @param[in]      handle        The handle parameter is interpreted depending on role:
 *                               - If role is @ref BLE_GAP_LL_ROLE_CONN, this value is the specific connection handle.
 *                               - For all other roles, handle is ignored.
 * @param[in]      p_params      Event trigger parameters.
 *
 * @retval ::NRF_SUCCESS                   Success.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_INVALID_ADDR        Invalid pointer supplied.
 * @retval ::NRF_ERROR_INVALID_PARAM       Invalid parameter supplied. See @ref ble_gap_event_trigger_t.
 * @retval ::NRF_ERROR_INVALID_STATE       Either:
 *                                         - Trying to start event triggering when it is already ongoing.
 *                                         - @ref ble_gap_event_trigger_t::conn_evt_counter_start is in the past.
 *                                           Use @ref sd_ble_gap_next_conn_evt_counter_get to find a new value
 *                                           to be used as ble_gap_event_trigger_t::conn_evt_counter_start.
 *                                         - Trying to start the event triggering for @ref BLE_GAP_LL_ROLE_SCAN or
 *                                           @ref BLE_GAP_LL_ROLE_INIT when the corresponding LL role is not active.
 */
SVCALL(SD_BLE_GAP_EVT_TRIGGER_START, uint32_t, sd_ble_gap_evt_trigger_start(uint8_t role, uint16_t handle, ble_gap_event_trigger_t const * p_params));


/**@brief   Stop triggering the task configured using @ref sd_ble_gap_evt_trigger_start.
 *
 * @param[in]   role     The role to stop the event trigger for.
 *                       The following roles are supported on this SoftDevice:
 *                       - @ref BLE_GAP_LL_ROLE_CONN
 *                       - @ref BLE_GAP_LL_ROLE_SCAN
 *                       - @ref BLE_GAP_LL_ROLE_INIT
 * @param[in]   handle   The handle parameter is interpreted depending on role:
 *                       - If role is @ref BLE_GAP_LL_ROLE_CONN, this value is the specific connection handle.
 *                       - For all other roles, handle is ignored.
 *
 * @retval ::NRF_SUCCESS                   Success.
 * @retval ::BLE_ERROR_INVALID_CONN_HANDLE Invalid connection handle supplied.
 * @retval ::NRF_ERROR_INVALID_STATE       Trying to stop event triggering when it is not started.
 */
SVCALL(SD_BLE_GAP_EVT_TRIGGER_STOP, uint32_t, sd_ble_gap_evt_trigger_stop(uint8_t role, uint16_t handle));


/** @} */

#ifdef __cplusplus
}
#endif
#endif // BLE_GAP_H__

/**
  @}
*/
