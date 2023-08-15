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

void settings_load_config(void)
{
    bool ret = fds_read_sync(FDS_SETTINGS_ID, FDS_SETTINGS_KEY, sizeof(config), (uint8_t *)&config);
    if (ret) {
        // After the reading is complete, we first save a copy of the current CRC, which can be used as a reference for comparison of changes when saving later
        calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&m_config_crc);
        NRF_LOG_INFO("Load config done.");
    } else {
        NRF_LOG_INFO("config no exists.");
    }
}

uint8_t settings_save_config(void)
{
    // We are saving the configuration, we need to calculate the crc code of the current configuration to judge whether the following data is updated
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&config, sizeof(config), (uint8_t *)&new_calc_crc);
    if (new_calc_crc != m_config_crc) {    // Before saving, make sure that the configuration has changed
        NRF_LOG_INFO("Save config start.");
        bool ret = fds_write_sync(FDS_SETTINGS_ID, FDS_SETTINGS_KEY, sizeof(config) / 4, (uint8_t *)&config);
        if (ret) {
            NRF_LOG_INFO("Save config success.");
            m_config_crc = new_calc_crc; // store new CRC so we know that we've updated the configuration
        }
        else
        {
            NRF_LOG_ERROR("Save config error.");
            return STATUS_FLASH_WRITE_FAIL;
        }
    } else {
        NRF_LOG_INFO("Config no change.");
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
