#include <stdbool.h>
#include "crc_utils.h"
#include "app_status.h"
#include "settings.h"
#include "fds_ids.h"
#include "fds_util.h"

#define NRF_LOG_MODULE_NAME settings
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


static settings_data_t config;
static uint16_t m_config_crc;
static bool m_ble_pairing_enable_first_load_value;

static void update_config_crc(void) {
    calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&m_config_crc);
}

static bool config_did_change(void) {
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&new_calc_crc);
    return new_calc_crc != m_config_crc;
}

void settings_update_version_for_config(void) {
    config.version = SETTINGS_CURRENT_VERSION;
}

// add on version2
void settings_init_button_press_config(void) {
    config.button_a_press = SettingsButtonCycleSlot;
    config.button_b_press = SettingsButtonCycleSlotDec;
}

// add on version3
void settings_init_button_long_press_config(void) {
    config.button_a_long_press = SettingsButtonCloneIcUid;
    config.button_b_long_press = SettingsButtonShowBattery;
}

// add on version4
void settings_init_ble_connect_key_config(void) {
    uint8_t p_key_u8[] = DEFAULT_BLE_PAIRING_KEY;
    settings_set_ble_connect_key(p_key_u8);
}

// add on version5
void settings_init_ble_pairing_enable_config(void) {
    config.ble_pairing_enable = false;
}

void settings_init_config(void) {
    settings_update_version_for_config();
    config.animation_config = SettingsAnimationModeFull; // add on version1
    settings_init_button_press_config();
    settings_init_button_long_press_config();
    settings_init_ble_connect_key_config();
    settings_init_ble_pairing_enable_config();
}

void settings_migrate(void) {
    switch (config.version) {
        case 0:
            NRF_LOG_ERROR("Unexpected configuration version detected!");
            settings_init_config();

        case 1:
            settings_init_button_press_config();

        case 2:
            settings_init_button_long_press_config();

        case 3:
            settings_init_ble_connect_key_config();

        case 4:
            settings_init_ble_pairing_enable_config();

            /*
             * Add new migration steps ABOVE THIS COMMENT
             * `settings_update_version_for_config()` and `break` statements should only be used on the last migration step, all the previous steps must fall
             * through to the next case.
             */

            settings_update_version_for_config();
            break;
        default:
            NRF_LOG_ERROR("Unsupported configuration migration attempted! (%d -> %d)", config.version, SETTINGS_CURRENT_VERSION);
            break;
    }
}

void settings_load_config(void) {
    uint16_t length = sizeof(config);
    bool ret = fds_read_sync(FDS_SETTINGS_FILE_ID, FDS_SETTINGS_RECORD_KEY, &length, (uint8_t *)&config);
    if (ret) {
        NRF_LOG_INFO("Load config done.");
        // After the reading is complete, we first save a copy of the current CRC, which can be used as a reference for comparison of changes when saving later
        update_config_crc();
    } else {
        NRF_LOG_WARNING("Config does not exist, loading default values...");
        settings_init_config();
    }
    if (config.version > SETTINGS_CURRENT_VERSION) {
        NRF_LOG_WARNING("Config version %d is greater than current firmware supports (%d). Default config will be loaded.", config.version, SETTINGS_CURRENT_VERSION);
        settings_init_config();
    }
    if (config.version < SETTINGS_CURRENT_VERSION) {
        NRF_LOG_INFO("Config version (%d) is not latest, performing migration to %d", config.version, SETTINGS_CURRENT_VERSION);
        settings_migrate();
    }
    if (config_did_change()) {
        settings_save_config();
    }

    // Assign values only after the first configuration load.
    m_ble_pairing_enable_first_load_value = config.ble_pairing_enable;
}

uint8_t settings_save_config(void) {
    // We are saving the configuration, we need to calculate the crc code of the current configuration to judge whether the following data is updated
    if (config_did_change()) {    // Before saving, make sure that the configuration has changed
        NRF_LOG_INFO("Save config start.");
        bool ret = fds_write_sync(FDS_SETTINGS_FILE_ID, FDS_SETTINGS_RECORD_KEY, sizeof(config), (uint8_t *)&config);
        if (ret) {
            NRF_LOG_INFO("Save config success.");
            update_config_crc();
        } else {
            NRF_LOG_ERROR("Save config error.");
            return STATUS_FLASH_WRITE_FAIL;
        }
    } else {
        NRF_LOG_INFO("Config did not change.");
    }

    return STATUS_SUCCESS;
}

uint8_t settings_get_animation_config() {
    return config.animation_config;
}

void settings_set_animation_config(uint8_t value) {
    config.animation_config = value;
}

/**
 * @brief check the button type is valid?
 *
 * @param type Button type, 'a' or 'b' or 'A' or 'B'
 * @return true Button type valid.
 * @return false Button type Invalid.
 */
bool is_settings_button_type_valid(char type) {
    switch (type) {
        case 'a':
        case 'b':
        case 'A':
        case 'B':
            return true;
        default:
            return false;
    }
}

/**
 * @brief Get the button press config
 *
 * @param which 'a' or 'b'
 * @return uint8_t @link{ settings_button_function_t }
 */
uint8_t settings_get_button_press_config(char which) {
    switch (which) {
        case 'a':
        case 'A':
            return config.button_a_press;

        case 'b':
        case 'B':
            return config.button_b_press;

        default:
            // can't to here.
            APP_ERROR_CHECK_BOOL(false);
            break;
    }
    // can't to here.
    return SettingsButtonDisable;
}

/**
 * @brief Get the long button press config
 *
 * @param which 'a' or 'b'
 * @return uint8_t @link{ settings_button_function_t }
 */
uint8_t settings_get_long_button_press_config(char which) {
    switch (which) {
        case 'a':
        case 'A':
            return config.button_a_long_press;

        case 'b':
        case 'B':
            return config.button_b_long_press;

        default:
            // can't to here.
            APP_ERROR_CHECK_BOOL(false);
            break;
    }
    // can't to here.
    return SettingsButtonDisable;
}

/**
 * @brief Set the button press config
 *
 * @param which 'a' or 'b'
 * @param value @link{ settings_button_function_t }
 */
void settings_set_button_press_config(char which, uint8_t value) {
    switch (which) {
        case 'a':
        case 'A':
            config.button_a_press = value;
            break;

        case 'b':
        case 'B':
            config.button_b_press = value;
            break;

        default:
            // can't to here.
            APP_ERROR_CHECK_BOOL(false);
            break;
    }
}

/**
 * @brief Set the long button press config
 *
 * @param which 'a' or 'b'
 * @param value @link{ settings_button_function_t }
 */
void settings_set_long_button_press_config(char which, uint8_t value) {
    switch (which) {
        case 'a':
        case 'A':
            config.button_a_long_press = value;
            break;

        case 'b':
        case 'B':
            config.button_b_long_press = value;
            break;

        default:
            // can't to here.
            APP_ERROR_CHECK_BOOL(false);
            break;
    }
}

uint8_t *settings_get_ble_connect_key(void) {
    return config.ble_connect_key;
}

/**
 * @brief Pointer to 6-digit ASCII string (digit 0..9 only, no NULL termination) passkey to be used during pairing.
 *
 * @param key Ble connect key for your device
 */
void settings_set_ble_connect_key(uint8_t *key) {
    memcpy(config.ble_connect_key, key, BLE_PAIRING_KEY_LEN);
}

void settings_set_ble_pairing_enable(bool enable) {
    config.ble_pairing_enable = enable;
}

bool settings_get_ble_pairing_enable(void) {
    return config.ble_pairing_enable;
}

bool settings_get_ble_pairing_enable_first_load(void) {
    return m_ble_pairing_enable_first_load_value;
}
