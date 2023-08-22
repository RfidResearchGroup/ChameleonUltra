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

static void update_config_crc(void)
{
    calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&m_config_crc);
}

static bool config_did_change(void)
{
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&new_calc_crc);
    return new_calc_crc != m_config_crc;
}

void settings_update_version_for_config(void) {
    config.version = SETTINGS_CURRENT_VERSION;
}

void settings_init_button_press_config(void) {
    config.button_a_press = SettingsButtonCycleSlot;
    config.button_b_press = SettingsButtonCycleSlotDec;
}

void settings_init_config(void)
{
    settings_update_version_for_config();
    config.animation_config = SettingsAnimationModeFull;
    settings_init_button_press_config();
}

void settings_migrate(void)
{
    switch (config.version) {
        case 0:
            NRF_LOG_ERROR("Unexpected configuration version detected!");
            settings_init_config();

        case 1:
            settings_init_button_press_config();
            settings_update_version_for_config();
            break;

        /*
         * When needed migrations can be implemented like this:
         *
         * case 1:
         *   config->new_field = some_default_value;
         * case 2:
         *   config->another_new_field = some_default_value;
         * case 3:
         *   config->another_new_field = some_default_value;
         *   break;
         *
         * Note that the `break` statement should only be used on the last migration step, all the previous steps must fall
         * through to the next case.
         *
         * Note that the `settings_update_version_for_config` function should only be used on the last migration step.
         */
        default:
            NRF_LOG_ERROR("Unsupported configuration migration attempted! (%d -> %d)", config.version, SETTINGS_CURRENT_VERSION);
            break;
    }
}

void settings_load_config(void)
{
    bool ret = fds_read_sync(FDS_SETTINGS_FILE_ID, FDS_SETTINGS_RECORD_KEY, sizeof(config), (uint8_t *)&config);
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
}

uint8_t settings_save_config(void)
{
    // We are saving the configuration, we need to calculate the crc code of the current configuration to judge whether the following data is updated
    if (config_did_change()) {    // Before saving, make sure that the configuration has changed
        NRF_LOG_INFO("Save config start.");
        bool ret = fds_write_sync(FDS_SETTINGS_FILE_ID, FDS_SETTINGS_RECORD_KEY, sizeof(config) / 4, (uint8_t *)&config);
        if (ret) {
            NRF_LOG_INFO("Save config success.");
            update_config_crc();
        }
        else
        {
            NRF_LOG_ERROR("Save config error.");
            return STATUS_FLASH_WRITE_FAIL;
        }
    } else {
        NRF_LOG_INFO("Config did not change.");
    }

    return STATUS_DEVICE_SUCCESS;
}

uint8_t settings_get_animation_config()
{
    return config.animation_config;
}

void settings_set_animation_config(uint8_t value)
{
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
    switch (type)
    {
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
uint8_t settings_get_button_press_config(char which)
{
    switch (which)
    {
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
 * @brief Set the button press config
 *
 * @param which 'a' or 'b'
 * @param value @link{ settings_button_function_t }
 */
void settings_set_button_press_config(char which, uint8_t value) {
    switch (which)
    {
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
