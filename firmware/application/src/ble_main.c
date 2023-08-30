#include "ble_hci.h"
#include "ble_advdata.h"
#include "ble_advertising.h"
#include "ble_conn_params.h"

#include "nrf.h"
#include "nordic_common.h"
#include "nrf_sdh.h"
#include "nrf_ble_qwr.h"
#include "nrf_sdh_soc.h"
#include "nrf_sdh_ble.h"
#include "nrf_ble_gatt.h"
#include "nrf_ble_lesc.h"
#include "nrf_drv_saadc.h"

#include "peer_manager.h"
#include "peer_manager_handler.h"

#include "app_timer.h"
#include "app_util_platform.h"

#include "syssleep.h"
#include "ble_main.h"
#include "dataframe.h"
#include "hw_connect.h"
#include "settings.h"

#define NRF_LOG_MODULE_NAME ble_main
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


#define LESC_DEBUG_MODE                 0                                           /**< Set to 1 to use the LESC debug keys. The debug mode allows you to use a sniffer to inspect traffic. */
#define LESC_MITM_NC                    1                                           /**< Use MITM (Numeric Comparison). */

#define SEC_PARAMS_BOND                 1                                           /**< Perform bonding. */
#if LESC_MITM_NC
#define SEC_PARAMS_MITM                 1                                           /**< Man In The Middle protection required. */
#define SEC_PARAMS_IO_CAPABILITIES      BLE_GAP_IO_CAPS_DISPLAY_ONLY
#else
#define SEC_PARAMS_MITM                 0                                           /**< Man In The Middle protection required. */
#define SEC_PARAMS_IO_CAPABILITIES      BLE_GAP_IO_CAPS_DISPLAY_ONLY
#endif
#define SEC_PARAMS_LESC                 1                                           /**< LE Secure Connections pairing required. */
#define SEC_PARAMS_KEYPRESS             0                                           /**< Keypress notifications not required. */
#define SEC_PARAMS_OOB                  0                                           /**< Out Of Band data not available. */
#define SEC_PARAMS_MIN_KEY_SIZE         7                                           /**< Minimum encryption key size in octets. */
#define SEC_PARAMS_MAX_KEY_SIZE         16                                          /**< Maximum encryption key size in octets. */

#define APP_BLE_CONN_CFG_TAG            1                                           /**< A tag identifying the SoftDevice BLE configuration. */
#define NUS_SERVICE_UUID_TYPE           BLE_UUID_TYPE_VENDOR_BEGIN                  /**< UUID type for the Nordic UART Service (vendor specific). */

#define APP_BLE_OBSERVER_PRIO           3                                           /**< Application's BLE observer priority. You shouldn't need to modify this value. */
#define APP_ADV_INTERVAL                64                                          /**< The advertising interval (in units of 0.625 ms. This value corresponds to 40 ms). */

#define MIN_CONN_INTERVAL               MSEC_TO_UNITS(20, UNIT_1_25_MS)             /**< Minimum acceptable connection interval (20 ms), Connection interval uses 1.25 ms units. */
#define MAX_CONN_INTERVAL               MSEC_TO_UNITS(75, UNIT_1_25_MS)             /**< Maximum acceptable connection interval (75 ms), Connection interval uses 1.25 ms units. */
#define SLAVE_LATENCY                   0                                           /**< Slave latency. */
#define CONN_SUP_TIMEOUT                MSEC_TO_UNITS(4000, UNIT_10_MS)             /**< Connection supervisory timeout (4 seconds), Supervision Timeout uses 10 ms units. */
#define FIRST_CONN_PARAMS_UPDATE_DELAY  APP_TIMER_TICKS(5000)                       /**< Time from initiating event (connect or start of notification) to first time sd_ble_gap_conn_param_update is called (5 seconds). */
#define NEXT_CONN_PARAMS_UPDATE_DELAY   APP_TIMER_TICKS(30000)                      /**< Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds). */
#define MAX_CONN_PARAMS_UPDATE_COUNT    3                                           /**< Number of attempts before giving up the connection parameter negotiation. */

// #define BATTERY_LEVEL_MEAS_INTERVAL     APP_TIMER_TICKS(1000)                       /**< Battery level measurement interval (ticks). This value corresponds to 1 seconds. */
#define BATTERY_LEVEL_MEAS_INTERVAL     APP_TIMER_TICKS(5000)                     /**< Battery level measurement interval (ticks). This value corresponds to N seconds. */

#define ADC_REF_VOLTAGE_IN_MILLIVOLTS  600  //!< Reference voltage (in milli volts) used by ADC while doing conversion.
#define ADC_RES_12BIT                  16383 //!< Maximum digital value for 14-bit ADC conversion.
#define ADC_PRE_SCALING_COMPENSATION   12    //!< The ADC is configured to use VDD with 1/3 prescaling as input. And hence the result of conversion is to be multiplied by 3 to get the actual value of the battery voltage.

/**@brief Macro to convert the result of ADC conversion in millivolts.
 *
 * @param[in]  ADC_VALUE   ADC result.
 *
 * @retval     Result converted to millivolts.
 */
#define ADC_RESULT_IN_MILLI_VOLTS(ADC_VALUE)\
        ((((ADC_VALUE) * ADC_REF_VOLTAGE_IN_MILLIVOLTS) / ADC_RES_12BIT) * ADC_PRE_SCALING_COMPENSATION)

APP_TIMER_DEF(m_battery_timer_id);                                                  /**< Battery measurement timer. */
BLE_BAS_DEF(m_bas);                                                                 /**< Battery service instance. */
BLE_NUS_DEF(m_nus, NRF_SDH_BLE_TOTAL_LINK_COUNT);                                   /**< BLE NUS service instance. */
NRF_BLE_GATT_DEF(m_gatt);                                                           /**< GATT module instance. */
NRF_BLE_QWR_DEF(m_qwr);                                                             /**< Context for the Queued Write module.*/
BLE_ADVERTISING_DEF(m_advertising);                                                 /**< Advertising module instance. */

uint16_t          batt_lvl_in_milli_volts = 0;
uint8_t           percentage_batt_lvl = 0;
static nrf_saadc_value_t adc_buf[2];
static uint16_t   m_conn_handle          = BLE_CONN_HANDLE_INVALID;                 /**< Handle of the current connection. */
static uint16_t   m_ble_nus_max_data_len = BLE_GATT_ATT_MTU_DEFAULT - 3;            /**< Maximum length of data (in bytes) that can be transmitted to the peer by the Nordic UART service module. */
static ble_uuid_t m_adv_uuids[]          =                                          /**< Universally unique service identifier. */
{
    {BLE_UUID_NUS_SERVICE, NUS_SERVICE_UUID_TYPE},
    {BLE_UUID_BATTERY_SERVICE, BLE_UUID_TYPE_BLE},
};
volatile bool g_is_ble_connected = false;
volatile bool g_is_low_battery_shutdown = false;
static ble_opt_t m_static_pin_option;


/**@brief Function for the ble connect key setup.
 *
 * @details This function will set up the ble connect passkey.
 */
void set_ble_connect_key(uint8_t* key) {
    static uint8_t passkey[BLE_CONNECT_KEY_LEN_MAX];
    memcpy(passkey, key, BLE_CONNECT_KEY_LEN_MAX);
    m_static_pin_option.gap_opt.passkey.p_passkey = passkey;
    // NRF_LOG_RAW_HEXDUMP_INFO(passkey, 6);
    APP_ERROR_CHECK(sd_ble_opt_set(BLE_GAP_OPT_PASSKEY, &m_static_pin_option));
}

/**@brief Function for the GAP initialization.
 *
 * @details This function will set up all the necessary GAP (Generic Access Profile) parameters of
 *          the device. It also sets the permissions and appearance.
 */
static void gap_params_init(void) {
    uint32_t                err_code;
    ble_gap_conn_params_t   gap_conn_params;
    ble_gap_conn_sec_mode_t sec_mode;

    BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

    err_code = sd_ble_gap_device_name_set(&sec_mode, (const uint8_t *) DEVICE_NAME_STR, strlen(DEVICE_NAME_STR));
    APP_ERROR_CHECK(err_code);

    memset(&gap_conn_params, 0, sizeof(gap_conn_params));

    gap_conn_params.min_conn_interval = MIN_CONN_INTERVAL;
    gap_conn_params.max_conn_interval = MAX_CONN_INTERVAL;
    gap_conn_params.slave_latency     = SLAVE_LATENCY;
    gap_conn_params.conn_sup_timeout  = CONN_SUP_TIMEOUT;

    err_code = sd_ble_gap_ppcp_set(&gap_conn_params);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the Battery Service events.
 *
 * @details This function will be called for all Battery Service events which are passed to the
 |          application.
 *
 * @param[in] p_bas  Battery Service structure.
 * @param[in] p_evt  Event received from the Battery Service.
 */
static void on_bas_evt(ble_bas_t *p_bas, ble_bas_evt_t *p_evt) {
    switch (p_evt->evt_type) {
        case BLE_BAS_EVT_NOTIFICATION_ENABLED:
            break; // BLE_BAS_EVT_NOTIFICATION_ENABLED

        case BLE_BAS_EVT_NOTIFICATION_DISABLED:
            break; // BLE_BAS_EVT_NOTIFICATION_DISABLED

        default:
            // No implementation needed.
            break;
    }
}

/**@brief Function for handling the data from the Nordic UART Service.
 *
 * @details This function will process the data received from the Nordic UART BLE Service
 *
 * @param[in] p_evt       Nordic UART Service event.
 */
/**@snippet [Handling the data received over BLE] */
static void nus_data_handler(ble_nus_evt_t *p_evt) {
    if (p_evt->type == BLE_NUS_EVT_RX_DATA) {
        NRF_LOG_DEBUG("Received data from BLE NUS.");
        NRF_LOG_HEXDUMP_DEBUG(p_evt->params.rx_data.p_data, p_evt->params.rx_data.length);
        data_frame_receive((uint8_t *)(p_evt->params.rx_data.p_data), p_evt->params.rx_data.length);
    }
}
/**@snippet [Handling the data received over BLE] */

void nus_data_response(uint8_t *p_data, uint16_t length) {
    NRF_LOG_INFO("BLE nus service response data length: %d", length);
    NRF_LOG_HEXDUMP_DEBUG(p_data, length);

    ret_code_t err_code;
    uint16_t remain = length;
    uint16_t count = 0;
    do {
        remain = MIN(m_ble_nus_max_data_len, remain);
        err_code = ble_nus_data_send(&m_nus, p_data + count, &remain, m_conn_handle);
        // NRF_LOG_INFO("Data send length(amount): %d", remain);
        if (err_code == NRF_SUCCESS) {
            count += remain;
            remain = length - count;
        }
        // NRF_LOG_INFO("Data send length(count): %d", count);
        if (err_code == NRF_ERROR_BUSY) {
            continue;
        }
        if ((err_code != NRF_ERROR_INVALID_STATE) &&
                (err_code != NRF_ERROR_RESOURCES) &&
                (err_code != NRF_ERROR_NOT_FOUND)) {
            APP_ERROR_CHECK(err_code);
        }

    } while (count != length && g_is_ble_connected);
}

bool is_nus_working(void) {
    return g_is_ble_connected;
}

/**@brief Function for handling Queued Write Module errors.
 *
 * @details A pointer to this function will be passed to each service which may need to inform the
 *          application about an error.
 *
 * @param[in]   nrf_error   Error code containing information about what went wrong.
 */
static void nrf_qwr_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

__INLINE uint32_t map(uint32_t x, uint32_t in_min, uint32_t in_max, uint32_t out_min, uint32_t out_max) {
    return (uint32_t)(MIN((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min, out_max));
}

//Battery voltage to percentage calculation
uint32_t BATVOL2PERCENT(uint16_t VOL) {
    //100%  4.20V   1
    //90 %  4.06V       80%-100%    white
    //80 %  3.98V   1
    //70 %  3.92V       60%-80%     white
    //60 %  3.87V   1
    //50 %  3.82V       40%-60%     white
    //40 %  3.79V   1
    //30 %  3.77V       20%-40%     white
    //20 %  3.74V   1
    //10 %  3.68V       5%-20%      red
    //5 %   3.45V   1               Turn off
    //0 %   3.00V
    //#define P100VOL   4200
    //#define P80VOL    3980
    //#define P60VOL    3870
    //#define P40VOL    3790
    //#define P20VOL    3740
    //#define P5VOL 3450

    //100%  4.20V   1
    //90 %  4.00V       80%-100%    white
    //80 %  3.89V   1
    //70 %  3.79V       60%-80%     white
    //60 %  3.70V   1
    //50 %  3.62V       40%-60%     white
    //40 %  3.57V   1
    //30 %  3.53V       20%-40%     white
    //20 %  3.51V   1
    //10 %  3.46V       5%-20%      red
    //5 %   3.43V   1               Turn off
    //0 %   3.00V
#define P100VOL 4200
#define P80VOL  3890
#define P60VOL  3700
#define P40VOL  3570
#define P20VOL  3510
#define P5VOL   3230


    if (VOL > P80VOL) {
        //80-100
        return map(VOL, P80VOL, P100VOL, 80, 100);
    } else if (VOL > P60VOL) {
        //60-80
        return map(VOL, P60VOL, P80VOL, 60, 80);
    } else if (VOL > P40VOL) {
        //40-60
        return map(VOL, P40VOL, P60VOL, 40, 60);
    } else if (VOL > P20VOL) {
        //20-60
        return map(VOL, P20VOL, P40VOL, 20, 40);
    } else if (VOL > P5VOL) {
        //5-20
        return map(VOL, P5VOL, P20VOL, 5, 20);
    } else {
        //<5
        return 0;
    }
}

/**@brief Function for initializing services that will be used by the application.
 */
static void services_init(void) {
    uint32_t           err_code;

    // -------------------------------------------------------------
    // Initialize Queued Write Module.
    nrf_ble_qwr_init_t qwr_init = {0};
    qwr_init.error_handler = nrf_qwr_error_handler;

    err_code = nrf_ble_qwr_init(&m_qwr, &qwr_init);
    APP_ERROR_CHECK(err_code);


    // -------------------------------------------------------------
    // Initialize NUS.
    ble_nus_init_t     nus_init;
    memset(&nus_init, 0, sizeof(nus_init));

    nus_init.data_handler = nus_data_handler;

    err_code = ble_nus_init(&m_nus, &nus_init);
    APP_ERROR_CHECK(err_code);

    // -------------------------------------------------------------
    // battery service

    ble_bas_init_t bas_init_obj;

    memset(&bas_init_obj, 0, sizeof(bas_init_obj));

    bas_init_obj.evt_handler          = on_bas_evt;
    bas_init_obj.support_notification = true;
    bas_init_obj.p_report_ref         = NULL;
    bas_init_obj.initial_batt_level   = 100;

    bas_init_obj.bl_rd_sec        = SEC_MITM;
    bas_init_obj.bl_cccd_wr_sec   = SEC_MITM;
    bas_init_obj.bl_report_rd_sec = SEC_MITM;

    err_code = ble_bas_init(&m_bas, &bas_init_obj);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling an event from the Connection Parameters Module.
 *
 * @details This function will be called for all events in the Connection Parameters Module
 *          which are passed to the application.
 *
 * @note All this function does is to disconnect. This could have been done by simply setting
 *       the disconnect_on_fail config parameter, but instead we use the event handler
 *       mechanism to demonstrate its use.
 *
 * @param[in] p_evt  Event received from the Connection Parameters Module.
 */
static void on_conn_params_evt(ble_conn_params_evt_t *p_evt) {
    uint32_t err_code;

    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        err_code = sd_ble_gap_disconnect(m_conn_handle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_CHECK(err_code);
    }
}

/**@brief Function for handling errors from the Connection Parameters module.
 *
 * @param[in] nrf_error  Error code containing information about what went wrong.
 */
static void conn_params_error_handler(uint32_t nrf_error) {
    APP_ERROR_HANDLER(nrf_error);
}

/**@brief Function for initializing the Connection Parameters module.
 */
static void conn_params_init(void) {
    uint32_t               err_code;
    ble_conn_params_init_t cp_init;

    memset(&cp_init, 0, sizeof(cp_init));

    cp_init.p_conn_params                  = NULL;
    cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
    cp_init.next_conn_params_update_delay  = NEXT_CONN_PARAMS_UPDATE_DELAY;
    cp_init.max_conn_params_update_count   = MAX_CONN_PARAMS_UPDATE_COUNT;
    cp_init.start_on_notify_cccd_handle    = BLE_GATT_HANDLE_INVALID;
    cp_init.disconnect_on_fail             = false;
    cp_init.evt_handler                    = on_conn_params_evt;
    cp_init.error_handler                  = conn_params_error_handler;

    err_code = ble_conn_params_init(&cp_init);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling advertising events.
 *
 * @details This function will be called for advertising events which are passed to the application.
 *
 * @param[in] ble_adv_evt  Advertising event.
 */
static void on_adv_evt(ble_adv_evt_t ble_adv_evt) {
    switch (ble_adv_evt) {
        case BLE_ADV_EVT_FAST:
            NRF_LOG_INFO("BLE_ADV_EVT_FAST");
            break;
        case BLE_ADV_EVT_IDLE:
            NRF_LOG_INFO("BLE_ADV_EVT_IDLE");
            break;
        default:
            break;
    }
}

/**@brief Function for handling BLE events.
 *
 * @param[in]   p_ble_evt   Bluetooth stack event.
 * @param[in]   p_context   Unused.
 */
static void ble_evt_handler(ble_evt_t const *p_ble_evt, void *p_context) {
    ret_code_t err_code;

    switch (p_ble_evt->header.evt_id) {
        case BLE_GAP_EVT_CONNECTED:
            sleep_timer_stop();

            NRF_LOG_INFO("Connected");
            m_conn_handle = p_ble_evt->evt.gap_evt.conn_handle;
            err_code = nrf_ble_qwr_conn_handle_assign(&m_qwr, m_conn_handle);
            APP_ERROR_CHECK(err_code);
            g_is_ble_connected = true;
            break;

        case BLE_GAP_EVT_DISCONNECTED:
            NRF_LOG_INFO("Disconnected");
            // LED indication will be changed when advertising starts.
            m_conn_handle = BLE_CONN_HANDLE_INVALID;
            g_is_ble_connected = false;
            // call sleep_timer_start *after* unsetting g_is_ble_connected
            sleep_timer_start(SLEEP_DELAY_MS_BLE_DISCONNECTED);
            break;

        case BLE_GAP_EVT_PHY_UPDATE_REQUEST: {
            NRF_LOG_DEBUG("PHY update request.");
            ble_gap_phys_t const phys = {
                .rx_phys = BLE_GAP_PHY_AUTO,
                .tx_phys = BLE_GAP_PHY_AUTO,
            };
            err_code = sd_ble_gap_phy_update(p_ble_evt->evt.gap_evt.conn_handle, &phys);
            APP_ERROR_CHECK(err_code);
        }
        break;

        case BLE_GAP_EVT_SEC_PARAMS_REQUEST:
            // Pairing not supported? No, is supported now, hahahaha...
            // err_code = sd_ble_gap_sec_params_reply(m_conn_handle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
            // APP_ERROR_CHECK(err_code);
            break;
        
        case BLE_GAP_EVT_PASSKEY_DISPLAY: {
            char passkey[BLE_GAP_PASSKEY_LEN + 1];
            memcpy(passkey, p_ble_evt->evt.gap_evt.params.passkey_display.passkey, BLE_GAP_PASSKEY_LEN);
            passkey[BLE_GAP_PASSKEY_LEN] = 0x00;
            NRF_LOG_INFO("=== PASSKEY: %s =====",   nrf_log_push(passkey));
        }
        break;

        case BLE_GATTS_EVT_SYS_ATTR_MISSING:
            // No system attributes have been stored.
            err_code = sd_ble_gatts_sys_attr_set(m_conn_handle, NULL, 0, 0);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTC_EVT_TIMEOUT:
            // Disconnect on GATT Client timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gattc_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        case BLE_GATTS_EVT_TIMEOUT:
            // Disconnect on GATT Server timeout event.
            err_code = sd_ble_gap_disconnect(p_ble_evt->evt.gatts_evt.conn_handle,
                                             BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
            APP_ERROR_CHECK(err_code);
            break;

        default:
            // No implementation needed.
            UNUSED_VARIABLE(err_code);
            break;
    }
}


/**@brief Function for the SoftDevice initialization.
 *
 * @details This function initializes the SoftDevice and the BLE event interrupt.
 */
static void ble_stack_init(void) {
    ret_code_t err_code;

    err_code = nrf_sdh_enable_request();
    APP_ERROR_CHECK(err_code);

    // Configure the BLE stack using the default settings.
    // Fetch the start address of the application RAM.
    uint32_t ram_start = 0;
    err_code = nrf_sdh_ble_default_cfg_set(APP_BLE_CONN_CFG_TAG, &ram_start);
    APP_ERROR_CHECK(err_code);

    // Enable BLE stack.
    err_code = nrf_sdh_ble_enable(&ram_start);
    APP_ERROR_CHECK(err_code);

    // Register a handler for BLE events.
    NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, ble_evt_handler, NULL);
}


/**@brief Function for handling events from the GATT library. */
void gatt_evt_handler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const *p_evt) {
    if ((m_conn_handle == p_evt->conn_handle) && (p_evt->evt_id == NRF_BLE_GATT_EVT_ATT_MTU_UPDATED)) {
        m_ble_nus_max_data_len = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
        NRF_LOG_INFO("Data len is set to 0x%X(%d)", m_ble_nus_max_data_len, m_ble_nus_max_data_len);
    }
    NRF_LOG_DEBUG("ATT MTU exchange completed. central 0x%x peripheral 0x%x",
                  p_gatt->att_mtu_desired_central,
                  p_gatt->att_mtu_desired_periph);
}


/**@brief Function for initializing the GATT library. */
void gatt_init(void) {
    ret_code_t err_code;

    err_code = nrf_ble_gatt_init(&m_gatt, gatt_evt_handler);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_ble_gatt_att_mtu_periph_set(&m_gatt, NRF_SDH_BLE_GATT_MAX_MTU_SIZE);
    APP_ERROR_CHECK(err_code);
}


/**@brief Function for initializing the Advertising functionality.
 */
static void advertising_init(void) {
    uint32_t               err_code;
    ble_advertising_init_t init;

    memset(&init, 0, sizeof(init));

    init.advdata.name_type          = BLE_ADVDATA_FULL_NAME;
    init.advdata.include_appearance = false;
    init.advdata.flags              = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;

    init.srdata.uuids_complete.uuid_cnt = sizeof(m_adv_uuids) / sizeof(m_adv_uuids[0]);
    init.srdata.uuids_complete.p_uuids  = m_adv_uuids;

    init.config.ble_adv_fast_enabled  = true;
    init.config.ble_adv_fast_interval = APP_ADV_INTERVAL;
    init.config.ble_adv_fast_timeout  = 0;
    init.evt_handler = on_adv_evt;

    err_code = ble_advertising_init(&m_advertising, &init);
    APP_ERROR_CHECK(err_code);

    ble_advertising_conn_cfg_tag_set(&m_advertising, APP_BLE_CONN_CFG_TAG);
}

/**@brief Clear bond information from persistent storage.
 */
void delete_bonds_all(void)
{
    ret_code_t err_code;

    NRF_LOG_INFO("Erase bonds!");

    err_code = pm_peers_delete();
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for setting filtered whitelist.
 *
 * @param[in] skip  Filter passed to @ref pm_peer_id_list.
 */
static void whitelist_set(pm_peer_id_list_skip_t skip)
{
    pm_peer_id_t peer_ids[BLE_GAP_WHITELIST_ADDR_MAX_COUNT];
    uint32_t     peer_id_count = BLE_GAP_WHITELIST_ADDR_MAX_COUNT;

    ret_code_t err_code = pm_peer_id_list(peer_ids, &peer_id_count, PM_PEER_ID_INVALID, skip);
    APP_ERROR_CHECK(err_code);

    NRF_LOG_INFO("Whitelist peer cnt %d, MAX_PEERS_WLIST %d", peer_id_count, BLE_GAP_WHITELIST_ADDR_MAX_COUNT);

    err_code = pm_whitelist_set(peer_ids, peer_id_count);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for starting advertising.
 */
void advertising_start(bool erase_bonds)
{
    if (erase_bonds == true)
    {
        delete_bonds_all();
        // Advertising is started by PM_EVT_PEERS_DELETE_SUCCEEDED event.
    }
    else
    {
        whitelist_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);

        ret_code_t ret = ble_advertising_start(&m_advertising, BLE_ADV_MODE_FAST);
        APP_ERROR_CHECK(ret);
    }
}

/**
 * @brief Function for stop advertising.
 */
void advertising_stop(void)
{
    sd_ble_gap_adv_stop(m_advertising.adv_handle);
}

/**@brief Function for handling Peer Manager events.
 *
 * @param[in] p_evt  Peer Manager event.
 */
static void pm_evt_handler(pm_evt_t const * p_evt)
{
    pm_handler_on_pm_evt(p_evt);
    pm_handler_disconnect_on_sec_failure(p_evt);
    pm_handler_flash_clean(p_evt);

    switch (p_evt->evt_id)
    {
        case PM_EVT_CONN_SEC_SUCCEEDED:
            // p_evt->peer_id;
            break;

        case PM_EVT_PEERS_DELETE_SUCCEEDED:
            advertising_start(false);
            break;

        case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
            if (     p_evt->params.peer_data_update_succeeded.flash_changed
                 && (p_evt->params.peer_data_update_succeeded.data_id == PM_PEER_DATA_ID_BONDING))
            {
                NRF_LOG_INFO("New Bond, add the peer to the whitelist if possible");
                // Note: You should check on what kind of white list policy your application should use.

                whitelist_set(PM_PEER_ID_LIST_SKIP_NO_ID_ADDR);
            }
            break;
        case PM_EVT_CONN_SEC_CONFIG_REQ:
            {
                pm_conn_sec_config_t cfg;
                cfg.allow_repairing = true;
                pm_conn_sec_config_reply(p_evt->conn_handle, &cfg);
            }
            break;
        default:
            break;
    }
}

/**@brief Function for the Peer Manager initialization.
 */
static void peer_manager_init(void)
{
    ble_gap_sec_params_t sec_param;
    ret_code_t           err_code;

    err_code = pm_init();
    APP_ERROR_CHECK(err_code);

    memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

    // Security parameters to be used for all security procedures.
    sec_param.bond           = SEC_PARAMS_BOND;
    sec_param.mitm           = SEC_PARAMS_MITM;
    sec_param.lesc           = SEC_PARAMS_LESC;
    sec_param.keypress       = SEC_PARAMS_KEYPRESS;
    sec_param.io_caps        = SEC_PARAMS_IO_CAPABILITIES;
    sec_param.oob            = SEC_PARAMS_OOB;
    sec_param.min_key_size   = SEC_PARAMS_MIN_KEY_SIZE;
    sec_param.max_key_size   = SEC_PARAMS_MAX_KEY_SIZE;
    sec_param.kdist_own.enc  = 1;
    sec_param.kdist_own.id   = 1;
    sec_param.kdist_peer.enc = 1;
    sec_param.kdist_peer.id  = 1;

    err_code = pm_sec_params_set(&sec_param);
    APP_ERROR_CHECK(err_code);

    err_code = pm_register(pm_evt_handler);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the ADC interrupt.
 *
 * @details  This function will fetch the conversion result from the ADC, convert the value into
 *           percentage and send it to peer.
 */
void saadc_event_handler(nrf_drv_saadc_evt_t const *p_event) {
    if (p_event->type == NRF_DRV_SAADC_EVT_DONE) {
        nrf_saadc_value_t adc_result;
        uint32_t          err_code;

        adc_result = p_event->data.done.p_buffer[0];
        // NRF_LOG_INFO("ADC sample value = %d", adc_result);

        err_code = nrf_drv_saadc_buffer_convert(p_event->data.done.p_buffer, 1);
        APP_ERROR_CHECK(err_code);

        batt_lvl_in_milli_volts = ADC_RESULT_IN_MILLI_VOLTS(adc_result) + 100;
        // NRF_LOG_INFO("batt_lvl_in_milli_volts: %d", batt_lvl_in_milli_volts);
        percentage_batt_lvl = BATVOL2PERCENT(batt_lvl_in_milli_volts);

        // if battery service is notification enable, we can send msg to device.
        err_code = ble_bas_battery_level_update(&m_bas, percentage_batt_lvl, BLE_CONN_HANDLE_ALL);
        if ((err_code != NRF_SUCCESS) &&
            (err_code != NRF_ERROR_INVALID_STATE) &&
            (err_code != NRF_ERROR_RESOURCES) &&
            (err_code != NRF_ERROR_BUSY) &&
            (err_code != NRF_ERROR_FORBIDDEN) &&
            (err_code != BLE_ERROR_GATTS_SYS_ATTR_MISSING)) {
            APP_ERROR_HANDLER(err_code);
        }

        // check low battery level, if level == 0, we can try to shutdown.
        if (percentage_batt_lvl == 0) {
            NRF_LOG_INFO("battery too low, try to shutdown...");
            g_is_low_battery_shutdown = true;
            sleep_timer_start(SLEEP_NO_BATTERY_SHUTDOWN);
        } else {
            g_is_low_battery_shutdown = false;
        }
    }
}

/**@brief Function for configuring ADC to do battery level conversion.
 */
static void adc_configure(void) {
    ret_code_t err_code = nrf_drv_saadc_init(NULL, saadc_event_handler);
    APP_ERROR_CHECK(err_code);

    nrf_saadc_channel_config_t config = NRF_DRV_SAADC_DEFAULT_CHANNEL_CONFIG_SE(BAT_SENSE);
    err_code = nrf_drv_saadc_channel_init(0, &config);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(&adc_buf[0], 1);
    APP_ERROR_CHECK(err_code);

    err_code = nrf_drv_saadc_buffer_convert(&adc_buf[1], 1);
    APP_ERROR_CHECK(err_code);
}

/**@brief Function for handling the Battery measurement timer timeout.
 *
 * @details This function will be called each time the battery level measurement timer expires.
 *          This function will start the ADC.
 *
 * @param[in] p_context   Pointer used for passing some arbitrary information (context) from the
 *                        app_start_timer() call to the timeout handler.
 */
static void battery_level_meas_timeout_handler(void *p_context) {
    UNUSED_PARAMETER(p_context);

    ret_code_t err_code;
    err_code = nrf_drv_saadc_sample();
    APP_ERROR_CHECK(err_code);
}

void create_battery_timer(void) {
    ret_code_t err_code;
    // Create battery timer.
    err_code = app_timer_create(&m_battery_timer_id, APP_TIMER_MODE_REPEATED, battery_level_meas_timeout_handler);
    APP_ERROR_CHECK(err_code);
    // Start battery timer
    err_code = app_timer_start(m_battery_timer_id, BATTERY_LEVEL_MEAS_INTERVAL, NULL);
    APP_ERROR_CHECK(err_code);
}

/**
 * @brief Function for init ble slave.
 */
void ble_slave_init(void) {
    adc_configure();                    // ADC initialization
    create_battery_timer();             // Create a battery power update timer
    ble_stack_init();                   // BLE protocol stack initialization
    gap_params_init();                  // GAP parameter initialization
    gatt_init();                        // Gatt protocol initialization
    services_init();                    // Initialization of service characteristics
    advertising_init();                 // Broadcast parameter initialization
    conn_params_init();                 // Connection parameter initialization
    peer_manager_init();                // Peer manager Initialization
}
