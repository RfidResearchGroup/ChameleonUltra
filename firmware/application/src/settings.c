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

void settings_init_config(void)
{
    config.version = SETTINGS_CURRENT_VERSION;
    config.animation_config = SettingsAnimationModeFull;
}

void settings_migrate(void)
{
    switch (config.version) {
        case 0:
            NRF_LOG_ERROR("Unexpected configuration version detected!");
            settings_init_config();
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
         */
        default:
            NRF_LOG_ERROR("Unsupported configuration migration attempted! (%d -> %d)", config.version, SETTINGS_CURRENT_VERSION);
            break;
    }
}

void settings_load_config(void)
{
    bool ret = fds_read_sync(FDS_SETTINGS_ID, FDS_SETTINGS_KEY, sizeof(config), (uint8_t *)&config);
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
        bool ret = fds_write_sync(FDS_SETTINGS_ID, FDS_SETTINGS_KEY, sizeof(config) / 4, (uint8_t *)&config);
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
