#include "fds_util.h"
#include "bsp_time.h"
#include "bsp_delay.h"
#include "usb_main.h"
#include "rfid_main.h"
#include "ble_main.h"
#include "syssleep.h"
#include "tag_emulation.h"
#include "hex_utils.h"
#include "data_cmd.h"
#include "app_cmd.h"
#include "app_status.h"
#include "tag_persistence.h"
#include "nrf_pwr_mgmt.h"
#include "settings.h"
#include "delayed_reset.h"
#include "lwip_def.h"


#define NRF_LOG_MODULE_NAME app_cmd
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();



data_frame_tx_t *cmd_processor_get_version(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint16_t version = FW_VER_NUM;
    return data_frame_make(cmd, status, 2, (uint8_t *)&version);
}


data_frame_tx_t *cmd_processor_get_git_version(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    return data_frame_make(cmd, status, strlen(GIT_VERSION), (uint8_t *)GIT_VERSION);
}


data_frame_tx_t *cmd_processor_get_device(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    uint8_t device = 1;
#else
    uint8_t device = 0;
#endif
    return data_frame_make(cmd, status, 1, &device);
}


data_frame_tx_t *cmd_processor_change_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1) {
        if (data[0] == 1) {
#if defined(PROJECT_CHAMELEON_ULTRA)
            reader_mode_enter();
            return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 0, NULL);
#else
            return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
#endif
        } else {
#if defined(PROJECT_CHAMELEON_ULTRA)
            tag_mode_enter();
#endif
            return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 0, NULL);
        }
    } else {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
}

data_frame_tx_t *cmd_processor_get_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode == DEVICE_MODE_READER) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_enter_bootloader(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // restart to boot
#define BOOTLOADER_DFU_GPREGRET_MASK            (0xB0)
#define BOOTLOADER_DFU_START_BIT_MASK           (0x01)
#define BOOTLOADER_DFU_START    (BOOTLOADER_DFU_GPREGRET_MASK |         BOOTLOADER_DFU_START_BIT_MASK)
    APP_ERROR_CHECK(sd_power_gpregret_clr(0, 0xffffffff));
    APP_ERROR_CHECK(sd_power_gpregret_set(0, BOOTLOADER_DFU_START));
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_DFU);
    // Never into here...
    while (1) __NOP();
}

data_frame_tx_t *cmd_processor_get_device_chip_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t chip_id[2];
    chip_id[0] = NRF_FICR->DEVICEID[0];
    chip_id[1] = NRF_FICR->DEVICEID[1];
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 8, (uint8_t *)(&chip_id[0]));
}

data_frame_tx_t *cmd_processor_get_device_address(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t device_address[2];
    // The FICR value is a just a random number, with no knowledge
    // of the Bluetooth Specification requirements for random addresses.
    // So we need to set a Bluetooth LE random address as a static address.
    // See: https://github.com/zephyrproject-rtos/zephyr/blob/7b6b1328a0cb96fe313a5e2bfc57047471df236e/subsys/bluetooth/controller/hci/nordic/hci_vendor.c#L29
    device_address[0] = NRF_FICR->DEVICEADDR[0];
    device_address[1] = NRF_FICR->DEVICEADDR[1] | 0xC000;
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 6, (uint8_t *)(&device_address[0]));
}

data_frame_tx_t *cmd_processor_save_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = settings_save_config();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_reset_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    settings_init_config();
    status = settings_save_config();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t settings[7 + BLE_CONNECT_KEY_LEN_MAX] = {};
    settings[0] = SETTINGS_CURRENT_VERSION; // current version
    settings[1] = settings_get_animation_config(); // animation mode
    settings[2] = settings_get_button_press_config('A'); // short A button press mode
    settings[3] = settings_get_button_press_config('B'); // short B button press mode
    settings[4] = settings_get_long_button_press_config('A'); // long A button press mode
    settings[5] = settings_get_long_button_press_config('B'); // long B button press mode
    settings[6] = settings_get_ble_pairing_enable(); // is device require pairing
    memcpy(settings + 7, settings_get_ble_connect_key(), BLE_CONNECT_KEY_LEN_MAX);
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 7 + BLE_CONNECT_KEY_LEN_MAX, settings);
}

data_frame_tx_t *cmd_processor_set_animation_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1) {
        status = STATUS_DEVICE_SUCCESS;
        settings_set_animation_config(data[0]);
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_animation_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t animation_mode = settings_get_animation_config();
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)(&animation_mode));
}

data_frame_tx_t *cmd_processor_get_battery_info(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t resp[3] = { 0x00 };
    // set voltage
    num_to_bytes(batt_lvl_in_milli_volts, 2, resp);
    // set percentage
    resp[2] = percentage_batt_lvl;
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, sizeof(resp), resp);
}

data_frame_tx_t *cmd_processor_get_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t button_press_config;
    if (length == 1 && is_settings_button_type_valid(data[0])) {
        button_press_config = settings_get_button_press_config(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, length, (uint8_t *)(&button_press_config));
}

data_frame_tx_t *cmd_processor_set_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2 && is_settings_button_type_valid(data[0])) {
        settings_set_button_press_config(data[0], data[1]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_long_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t button_press_config;
    if (length == 1 && is_settings_button_type_valid(data[0])) {
        button_press_config = settings_get_long_button_press_config(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, length, (uint8_t *)(&button_press_config));
}

data_frame_tx_t *cmd_processor_set_long_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2 && is_settings_button_type_valid(data[0])) {
        settings_set_long_button_press_config(data[0], data[1]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_ble_pairing_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t is_enable = settings_get_ble_pairing_enable();
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)(&is_enable));
}

data_frame_tx_t *cmd_processor_set_ble_pairing_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == true || data[0] == false)) {
        settings_set_ble_pairing_enable(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

#if defined(PROJECT_CHAMELEON_ULTRA)

data_frame_tx_t *cmd_processor_14a_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    picc_14a_tag_t taginfo;
    status = pcd_14a_reader_scan_auto(&taginfo);
    if (status == HF_TAG_OK) {
        length = sizeof(picc_14a_tag_t);
        data = (uint8_t *)&taginfo;
    } else {
        length = 0;
        data = NULL;
    }
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t *cmd_processor_detect_mf1_support(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = check_std_mifare_nt_support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_detect_mf1_nt_level(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = check_weak_nested_support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_detect_mf1_darkside(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = check_darkside_support();
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_mf1_darkside_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    DarksideCore dc;
    if (length == 4) {
        status = darkside_recover_key(data[1], data[0], data[2], data[3], &dc);
        if (status == HF_TAG_OK) {
            length = sizeof(DarksideCore);
            data = (uint8_t *)(&dc);
        } else {
            length = 0;
        }
    } else {
        status = STATUS_PAR_ERR;
        length = 0;
    }
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t *cmd_processor_mf1_nt_distance(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    NestedDist nd;
    if (length == 8) {
        status = nested_distance_detect(data[1], data[0], &data[2], &nd);
        if (status == HF_TAG_OK) {
            length = sizeof(NestedDist);
            data = (uint8_t *)(&nd);
        } else {
            length = 0;
        }
    } else {
        status = STATUS_PAR_ERR;
        length = 0;
    }
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t *cmd_processor_mf1_nested_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    NestedCore ncs[SETS_NR];
    if (length == 10) {
        status = nested_recover_key(bytes_to_num(&data[2], 6), data[1], data[0], data[9], data[8], ncs);
        if (status == HF_TAG_OK) {
            length = sizeof(ncs);
            data = (uint8_t *)(&ncs);
        } else {
            length = 0;
        }
    } else {
        status = STATUS_PAR_ERR;
        length = 0;
    }
    return data_frame_make(cmd, status, length, data);
}

data_frame_tx_t *cmd_processor_mf1_auth_one_key_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 8) {
        status = auth_key_use_522_hw(data[1], data[0], &data[2]);
        pcd_14a_reader_mf1_unauth();
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_mf1_read_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t block[16] = { 0x00 };
    if (length == 8) {
        status = auth_key_use_522_hw(data[1], data[0], &data[2]);
        if (status == HF_TAG_OK) {
            status = pcd_14a_reader_mf1_read(data[1], block);
            if (status == HF_TAG_OK) {
                length = 16;
            } else {
                length = 0;
            }
        } else {
            length = 0;
        }
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, length, block);
}

data_frame_tx_t *cmd_processor_mf1_write_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 24) {
        status = auth_key_use_522_hw(data[1], data[0], &data[2]);
        if (status == HF_TAG_OK) {
            status = pcd_14a_reader_mf1_write(data[1], &data[8]);
        } else {
            length = 0;
        }
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_em410x_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t id_buffer[5] = { 0x00 };
    status = PcdScanEM410X(id_buffer);
    return data_frame_make(cmd, status, sizeof(id_buffer), id_buffer);
}

data_frame_tx_t *cmd_processor_write_em410x_2_t57(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length >= 13 && (length - 9) % 4 == 0) {
        status = PcdWriteT55XX(data, data + 5, data + 9, (length - 9) / 4);
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

#endif


static void change_slot_auto(uint8_t slot) {
    device_mode_t mode = get_device_mode();
    tag_emulation_change_slot(slot, mode != DEVICE_MODE_READER);
    light_up_by_slot();
    set_slot_light_color(0);
}

data_frame_tx_t *cmd_processor_set_slot_activated(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && data[0] < TAG_MAX_SLOT_NUM) {
        change_slot_auto(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_slot_tag_type(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && data[1] != TAG_TYPE_UNKNOWN) {
        uint8_t num_slot = data[0];
        uint8_t tag_type = data[1];
        tag_emulation_change_type(num_slot, (tag_specific_type_t)tag_type);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_delete_slot_sense_type(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = STATUS_PAR_ERR;
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && (data[1] == TAG_SENSE_HF || data[1] == TAG_SENSE_LF)) {
        uint8_t slot_num = data[0];
        uint8_t sense_type = data[1];

        tag_emulation_delete_data(slot_num, sense_type);
        status = STATUS_DEVICE_SUCCESS;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_slot_data_default(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && data[1] != TAG_TYPE_UNKNOWN) {
        uint8_t num_slot = data[0];
        uint8_t tag_type = data[1];
        status = tag_emulation_factory_data(num_slot, (tag_specific_type_t)tag_type) ? STATUS_DEVICE_SUCCESS : STATUS_NOT_IMPLEMENTED;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_slot_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2 && data[0] < TAG_MAX_SLOT_NUM && (data[1] == 0 || data[1] == 1)) {
        uint8_t slot_now = data[0];
        bool enable = data[1];
        tag_emulation_slot_set_enable(slot_now, enable);
        if (!enable) {
            uint8_t slot_prev = tag_emulation_slot_find_next(slot_now);
            NRF_LOG_INFO("slot_now = %d, slot_prev = %d", slot_now, slot_prev);
            if (slot_prev == slot_now) {
                set_slot_light_color(3);
            } else {
                change_slot_auto(slot_prev);
            }
        }
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_slot_data_config_save(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_emulation_save();
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_activated_slot(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t slot = tag_emulation_get_slot();
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, &slot);
}

data_frame_tx_t *cmd_processor_get_slot_info(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t slot_info[16] = {};
    tag_specific_type_t tag_type[2];
    for (uint8_t slot = 0; slot < 8; slot++) {
        tag_emulation_get_specific_type_by_slot(slot, tag_type);
        slot_info[slot * 2] = tag_type[0];
        slot_info[slot * 2 + 1] = tag_type[1];
    }

    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 16, slot_info);
}

data_frame_tx_t *cmd_processor_wipe_fds(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    bool success = fds_wipe();
    status = success ? STATUS_DEVICE_SUCCESS : STATUS_FLASH_WRITE_FAIL;
    delayed_reset(50);
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_em410x_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == LF_EM410X_TAG_ID_SIZE) {
        tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_EM410X);
        memcpy(buffer->buffer, data, LF_EM410X_TAG_ID_SIZE);
        tag_emulation_load_by_buffer(TAG_TYPE_EM410X, false);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_em410x_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_specific_type_t tag_type[2];
    tag_emulation_get_specific_type_by_slot(tag_emulation_get_slot(), tag_type);
    if (tag_type[1] == TAG_TYPE_UNKNOWN) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data); // no data in slot, don't send garbage
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_EM410X);
    uint8_t responseData[LF_EM410X_TAG_ID_SIZE];
    memcpy(responseData, buffer->buffer, LF_EM410X_TAG_ID_SIZE);
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, LF_EM410X_TAG_ID_SIZE, responseData);
}

data_frame_tx_t *cmd_processor_get_mf1_anti_coll_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_specific_type_t tag_type[2];
    tag_emulation_get_specific_type_by_slot(tag_emulation_get_slot(), tag_type);
    if (tag_type[0] == TAG_TYPE_UNKNOWN) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data); // no data in slot, don't send garbage
    }
    picc_14a_tag_t taginfo;
    nfc_tag_14a_coll_res_reference_t *info = get_saved_mifare_coll_res();
    taginfo.uid_len = *info->size;
    taginfo.cascade = (taginfo.uid_len + 2) / 4; //it's weird but it works for 4->1, 7->2, 10->3
    taginfo.sak = *info->sak;
    taginfo.ats_len = info->ats->length;
    memcpy(taginfo.uid, info->uid, taginfo.uid_len);
    memcpy(taginfo.atqa, info->atqa, 2);
    memcpy(taginfo.ats, info->ats->data, taginfo.ats_len);

    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, sizeof(taginfo), (uint8_t*) &taginfo);
}

data_frame_tx_t *cmd_processor_set_mf1_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == 0 || data[0] == 1)) {
        nfc_tag_mf1_detection_log_clear();
        nfc_tag_mf1_set_detection_enable(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_mf1_detection_status(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (nfc_tag_mf1_is_detection_enable()) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_get_mf1_detection_count(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count = nfc_tag_mf1_detection_log_count();
    if (count == 0xFFFFFFFF) {
        count = 0;
    }
    status = STATUS_DEVICE_SUCCESS;
    return data_frame_make(cmd, status, sizeof(uint32_t), (uint8_t *)&count);
}

data_frame_tx_t *cmd_processor_get_mf1_detection_log(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count;
    uint32_t index;
    uint8_t *resp = NULL;
    nfc_tag_mf1_auth_log_t *logs = get_mf1_auth_log(&count);
    if (length == 4) {
        if (count == 0xFFFFFFFF) {
            length = 0;
            status = STATUS_PAR_ERR;
        } else {
            index = bytes_to_num(data, 4);
            // NRF_LOG_INFO("index = %d", index);
            if (index < count) {
                resp = (uint8_t *)(logs + index);
                length = MIN(count - index, DATA_PACK_MAX_DATA_LENGTH / sizeof(nfc_tag_mf1_auth_log_t));
                length = length * sizeof(nfc_tag_mf1_auth_log_t);
                status = STATUS_DEVICE_SUCCESS;
            } else {
                length = 0;
                status = STATUS_PAR_ERR;
            }
        }
    } else {
        length = 0;
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, length, resp);
}

data_frame_tx_t *cmd_processor_set_mf1_emulator_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length > 0 && (((length - 1) % NFC_TAG_MF1_DATA_SIZE) == 0)) {
        uint8_t block_index = data[0];
        uint8_t block_count = (length - 1) / NFC_TAG_MF1_DATA_SIZE;
        if (block_index + block_count > NFC_TAG_MF1_BLOCK_MAX) {
            status = STATUS_PAR_ERR;
        } else {
            tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_4096);
            nfc_tag_mf1_information_t *info = (nfc_tag_mf1_information_t *)buffer->buffer;
            for (int i = 1, j = block_index; i < length; i += NFC_TAG_MF1_DATA_SIZE, j++) {
                uint8_t *p_block = &data[i];
                memcpy(info->memory[j], p_block, NFC_TAG_MF1_DATA_SIZE);
            }
            status = STATUS_DEVICE_SUCCESS;
        }
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_mf1_emulator_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 2) {
        uint8_t block_index = data[0];
        uint8_t block_count = data[1];

        // block_count > 32 will overflow the maximum message size
        if (block_count != 0 && block_count <= 32 && (uint16_t) block_index + block_count < NFC_TAG_MF1_BLOCK_MAX) {
            tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_4096);
            nfc_tag_mf1_information_t *info = (nfc_tag_mf1_information_t *)buffer->buffer;
            uint16_t result_length = block_count * NFC_TAG_MF1_DATA_SIZE;
            uint8_t result_buffer[result_length];
            for (int i = 0, j = block_index; i < result_length; i += NFC_TAG_MF1_DATA_SIZE, j++) {
                uint8_t *p_block = &result_buffer[i];
                memcpy(p_block, info->memory[j], NFC_TAG_MF1_DATA_SIZE);
            }

            return data_frame_make(cmd, status, result_length, result_buffer);
        }
    }

    return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_mf1_anti_collision_res(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length > 13) {
        // sak(1) + atqa(2) + uid(10)
        status = STATUS_PAR_ERR;
    } else {
        uint8_t uid_length = length - 3;
        if (is_valid_uid_size(uid_length)) {
            nfc_tag_14a_coll_res_reference_t *info = get_mifare_coll_res();
            // copy sak
            info->sak[0] = data[0];
            // copy atqa
            memcpy(info->atqa, &data[1], 2);
            // copy uid
            memcpy(info->uid, &data[3], uid_length);
            // copy size
            *(info->size) = (nfc_tag_14a_uid_size)uid_length;
            status = STATUS_DEVICE_SUCCESS;
        } else {
            status = STATUS_PAR_ERR;
        }
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_set_slot_tag_nick_name(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length > 34 || length < 3) {
        status = STATUS_PAR_ERR;
    } else {
        uint8_t slot = data[0];
        uint8_t sense_type = data[1];
        fds_slot_record_map_t map_info;

        get_fds_map_by_slot_sense_type_for_nick(slot, sense_type, &map_info);

        uint8_t buffer[36];
        buffer[0] = length - 2;
        memcpy(buffer + 1, data + 2, buffer[0]);

        bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(buffer) / 4, buffer);
        if (ret) {
            status = STATUS_DEVICE_SUCCESS;
        } else {
            status = STATUS_FLASH_WRITE_FAIL;
        }
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_slot_tag_nick_name(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 2) {
        status = STATUS_PAR_ERR;
        return data_frame_make(cmd, status, 0, NULL);
    } else {
        uint8_t buffer[36];
        uint8_t slot = data[0];
        uint8_t sense_type = data[1];
        fds_slot_record_map_t map_info;

        get_fds_map_by_slot_sense_type_for_nick(slot, sense_type, &map_info);
        bool ret = fds_read_sync(map_info.id, map_info.key, sizeof(buffer), buffer);
        if (ret) {
            status = STATUS_DEVICE_SUCCESS;
            length = buffer[0];
            data = &buffer[1];
        } else {
            status = STATUS_FLASH_READ_FAIL;
            length = 0;
            data = NULL;
        }
        // must be called within stack allocation of buffer
        return data_frame_make(cmd, status, length, data);
    }
}

data_frame_tx_t *cmd_processor_get_mf1_info(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mf1_info[5] = {};
    mf1_info[0] = nfc_tag_mf1_is_detection_enable();
    mf1_info[1] = nfc_tag_mf1_is_gen1a_magic_mode();
    mf1_info[2] = nfc_tag_mf1_is_gen2_magic_mode();
    mf1_info[3] = nfc_tag_mf1_is_use_mf1_coll_res();
    nfc_tag_mf1_write_mode_t write_mode = nfc_tag_mf1_get_write_mode();
    if (write_mode == NFC_TAG_MF1_WRITE_NORMAL) {
        mf1_info[4] = 0;
    } else if (write_mode == NFC_TAG_MF1_WRITE_DENIED) {
        mf1_info[4] = 1;
    } else if (write_mode == NFC_TAG_MF1_WRITE_DECEIVE) {
        mf1_info[4] = 2;
    } else if (write_mode == NFC_TAG_MF1_WRITE_SHADOW) {
        mf1_info[4] = 3;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 5, mf1_info);
}

data_frame_tx_t *cmd_processor_get_mf1_gen1a_magic_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (nfc_tag_mf1_is_gen1a_magic_mode()) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_set_mf1_gen1a_magic_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == 0 || data[0] == 1)) {
        nfc_tag_mf1_set_gen1a_magic_mode(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_mf1_gen2_magic_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (nfc_tag_mf1_is_gen2_magic_mode()) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_set_mf1_gen2_magic_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == 0 || data[0] == 1)) {
        nfc_tag_mf1_set_gen2_magic_mode(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_mf1_use_coll_res(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (nfc_tag_mf1_is_use_mf1_coll_res()) {
        status = 1;
    } else {
        status = 0;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_set_mf1_use_coll_res(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] == 0 || data[0] == 1)) {
        nfc_tag_mf1_set_use_mf1_coll_res(data[0]);
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_mf1_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    nfc_tag_mf1_write_mode_t write_mode = nfc_tag_mf1_get_write_mode();
    if (write_mode == NFC_TAG_MF1_WRITE_NORMAL) {
        status = 0;
    } else if (write_mode == NFC_TAG_MF1_WRITE_DENIED) {
        status = 1;
    } else if (write_mode == NFC_TAG_MF1_WRITE_DECEIVE) {
        status = 2;
    } else if (write_mode == NFC_TAG_MF1_WRITE_SHADOW) {
        status = 3;
    }
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 1, (uint8_t *)&status);
}

data_frame_tx_t *cmd_processor_set_mf1_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 1 && (data[0] >= 0 || data[0] <= 3)) {
        uint8_t mode = data[0];
        if (mode == 0) {
            nfc_tag_mf1_set_write_mode(NFC_TAG_MF1_WRITE_NORMAL);
        } else if (mode == 1) {
            nfc_tag_mf1_set_write_mode(NFC_TAG_MF1_WRITE_DENIED);
        } else if (mode == 2) {
            nfc_tag_mf1_set_write_mode(NFC_TAG_MF1_WRITE_DECEIVE);
        } else if (mode == 3) {
            nfc_tag_mf1_set_write_mode(NFC_TAG_MF1_WRITE_SHADOW);
        }
        status = STATUS_DEVICE_SUCCESS;
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_get_enabled_slots(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t slot_info[8] = {};
    for (uint8_t slot = 0; slot < 8; slot++) {
        slot_info[slot] = tag_emulation_slot_is_enable(slot);
    }

    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 8, slot_info);
}

data_frame_tx_t *cmd_processor_get_ble_connect_key(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    return data_frame_make(
        cmd,
        STATUS_DEVICE_SUCCESS,
        BLE_CONNECT_KEY_LEN_MAX, // 6
        settings_get_ble_connect_key() // Get key point from config
    );
}

data_frame_tx_t *cmd_processor_set_ble_connect_key(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == BLE_CONNECT_KEY_LEN_MAX) {
        // Must be 6 ASCII characters, can only be 0-9.
        bool is_valid_key = true;
        for (uint8_t i = 0; i < BLE_CONNECT_KEY_LEN_MAX; i++) {
            if (data[i] < 48 || data[i] > 57) {
                is_valid_key = false;
                break;
            }
        }
        if (is_valid_key) {
            // Key is valid, we can update to config
            settings_set_ble_connect_key(data);
            status = STATUS_DEVICE_SUCCESS;
        } else {
            status = STATUS_PAR_ERR;
        }
    } else {
        status = STATUS_PAR_ERR;
    }
    return data_frame_make(cmd, status, 0, NULL);
}

data_frame_tx_t *cmd_processor_del_ble_all_bonds(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    advertising_stop();
    delete_bonds_all();
    return data_frame_make(cmd, STATUS_DEVICE_SUCCESS, 0, NULL);
}

#if defined(PROJECT_CHAMELEON_ULTRA)


/**
 * before reader run, reset reader and on antenna,
 * we must to wait some time, to init picc(power).
 */
data_frame_tx_t *before_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode == DEVICE_MODE_READER) {
        return NULL;
    } else {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
}


/**
 * before reader run, reset reader and on antenna,
 * we must to wait some time, to init picc(power).
 */
data_frame_tx_t *before_hf_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    data_frame_tx_t *ret = before_reader_run(cmd, status, length, data);
    if (ret == NULL) {
        pcd_14a_reader_reset();
        pcd_14a_reader_antenna_on();
        bsp_delay_ms(8);
    }
    return ret;
}

/**
 * after reader run, off antenna, to keep battery.
 */
data_frame_tx_t *after_hf_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    pcd_14a_reader_antenna_off();
    return NULL;
}

#endif

/**
 * (cmd -> processor) function map, the map struct is:
 *       cmd code                               before process               cmd processor                                after process
 */
static cmd_data_map_t m_data_cmd_map[] = {
    {    DATA_CMD_GET_APP_VERSION,              NULL,                        cmd_processor_get_version,                   NULL                   },
    {    DATA_CMD_CHANGE_DEVICE_MODE,           NULL,                        cmd_processor_change_device_mode,            NULL                   },
    {    DATA_CMD_GET_DEVICE_MODE,              NULL,                        cmd_processor_get_device_mode,               NULL                   },
    {    DATA_CMD_ENTER_BOOTLOADER,             NULL,                        cmd_processor_enter_bootloader,              NULL                   },
    {    DATA_CMD_GET_DEVICE_CHIP_ID,           NULL,                        cmd_processor_get_device_chip_id,            NULL                   },
    {    DATA_CMD_GET_DEVICE_ADDRESS,           NULL,                        cmd_processor_get_device_address,            NULL                   },
    {    DATA_CMD_SAVE_SETTINGS,                NULL,                        cmd_processor_save_settings,                 NULL                   },
    {    DATA_CMD_RESET_SETTINGS,               NULL,                        cmd_processor_reset_settings,                NULL                   },
    {    DATA_CMD_SET_ANIMATION_MODE,           NULL,                        cmd_processor_set_animation_mode,            NULL                   },
    {    DATA_CMD_GET_ANIMATION_MODE,           NULL,                        cmd_processor_get_animation_mode,            NULL                   },
    {    DATA_CMD_GET_GIT_VERSION,              NULL,                        cmd_processor_get_git_version,               NULL                   },
    {    DATA_CMD_GET_BATTERY_INFO,             NULL,                        cmd_processor_get_battery_info,              NULL                   },
    {    DATA_CMD_GET_BUTTON_PRESS_CONFIG,      NULL,                        cmd_processor_get_button_press_config,       NULL                   },
    {    DATA_CMD_SET_BUTTON_PRESS_CONFIG,      NULL,                        cmd_processor_set_button_press_config,       NULL                   },
    {    DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG, NULL,                        cmd_processor_get_long_button_press_config,  NULL                   },
    {    DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG, NULL,                        cmd_processor_set_long_button_press_config,  NULL                   },
    {    DATA_CMD_GET_BLE_CONNECT_KEY_CONFIG,   NULL,                        cmd_processor_get_ble_connect_key,           NULL                   },
    {    DATA_CMD_SET_BLE_CONNECT_KEY_CONFIG,   NULL,                        cmd_processor_set_ble_connect_key,           NULL                   },
    {    DATA_CMD_DELETE_ALL_BLE_BONDS,         NULL,                        cmd_processor_del_ble_all_bonds,             NULL                   },
    {    DATA_CMD_GET_DEVICE,                   NULL,                        cmd_processor_get_device,                    NULL                   },
    {    DATA_CMD_GET_SETTINGS,                 NULL,                        cmd_processor_get_settings,                  NULL                   },
    {    DATA_CMD_GET_DEVICE_CAPABILITIES,      NULL,                        NULL,                                        NULL                   },
    {    DATA_CMD_GET_BLE_PAIRING_ENABLE,       NULL,                        cmd_processor_get_ble_pairing_enable,        NULL                   },
    {    DATA_CMD_SET_BLE_PAIRING_ENABLE,       NULL,                        cmd_processor_set_ble_pairing_enable,        NULL                   },

#if defined(PROJECT_CHAMELEON_ULTRA)

    {    DATA_CMD_SCAN_14A_TAG,                 before_hf_reader_run,        cmd_processor_14a_scan,                      after_hf_reader_run    },
    {    DATA_CMD_MF1_SUPPORT_DETECT,           before_hf_reader_run,        cmd_processor_detect_mf1_support,            after_hf_reader_run    },
    {    DATA_CMD_MF1_NT_LEVEL_DETECT,          before_hf_reader_run,        cmd_processor_detect_mf1_nt_level,           after_hf_reader_run    },
    {    DATA_CMD_MF1_DARKSIDE_DETECT,          before_hf_reader_run,        cmd_processor_detect_mf1_darkside,           after_hf_reader_run    },

    {    DATA_CMD_MF1_DARKSIDE_ACQUIRE,         before_hf_reader_run,        cmd_processor_mf1_darkside_acquire,          after_hf_reader_run    },
    {    DATA_CMD_MF1_NT_DIST_DETECT,           before_hf_reader_run,        cmd_processor_mf1_nt_distance,               after_hf_reader_run    },
    {    DATA_CMD_MF1_NESTED_ACQUIRE,           before_hf_reader_run,        cmd_processor_mf1_nested_acquire,            after_hf_reader_run    },

    {    DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK,      before_hf_reader_run,        cmd_processor_mf1_auth_one_key_block,        after_hf_reader_run    },
    {    DATA_CMD_MF1_READ_ONE_BLOCK,           before_hf_reader_run,        cmd_processor_mf1_read_one_block,            after_hf_reader_run    },
    {    DATA_CMD_MF1_WRITE_ONE_BLOCK,          before_hf_reader_run,        cmd_processor_mf1_write_one_block,           after_hf_reader_run    },

    {    DATA_CMD_SCAN_EM410X_TAG,              before_reader_run,           cmd_processor_em410x_scan,                   NULL                   },
    {    DATA_CMD_WRITE_EM410X_TO_T5577,        before_reader_run,           cmd_processor_write_em410x_2_t57,            NULL                   },

#endif

    {    DATA_CMD_SET_SLOT_ACTIVATED,           NULL,                        cmd_processor_set_slot_activated,            NULL                   },
    {    DATA_CMD_SET_SLOT_TAG_TYPE,            NULL,                        cmd_processor_set_slot_tag_type,             NULL                   },
    {    DATA_CMD_SET_SLOT_DATA_DEFAULT,        NULL,                        cmd_processor_set_slot_data_default,         NULL                   },
    {    DATA_CMD_SET_SLOT_ENABLE,              NULL,                        cmd_processor_set_slot_enable,               NULL                   },
    {    DATA_CMD_SLOT_DATA_CONFIG_SAVE,        NULL,                        cmd_processor_slot_data_config_save,         NULL                   },
    {    DATA_CMD_GET_ACTIVE_SLOT,              NULL,                        cmd_processor_get_activated_slot,            NULL                   },
    {    DATA_CMD_GET_SLOT_INFO,                NULL,                        cmd_processor_get_slot_info,                 NULL                   },
    {    DATA_CMD_WIPE_FDS,                     NULL,                        cmd_processor_wipe_fds,                      NULL                   },
    {    DATA_CMD_GET_ENABLED_SLOTS,            NULL,                        cmd_processor_get_enabled_slots,             NULL                   },
    {    DATA_CMD_DELETE_SLOT_SENSE_TYPE,       NULL,                        cmd_processor_delete_slot_sense_type,        NULL                   },



    {    DATA_CMD_SET_EM410X_EMU_ID,            NULL,                        cmd_processor_set_em410x_emu_id,             NULL                   },
    {    DATA_CMD_GET_EM410X_EMU_ID,            NULL,                        cmd_processor_get_em410x_emu_id,             NULL                   },

    {    DATA_CMD_GET_MF1_DETECTION_STATUS,     NULL,                        cmd_processor_get_mf1_detection_status,      NULL                   },
    {    DATA_CMD_SET_MF1_DETECTION_ENABLE,     NULL,                        cmd_processor_set_mf1_detection_enable,      NULL                   },
    {    DATA_CMD_GET_MF1_DETECTION_COUNT,      NULL,                        cmd_processor_get_mf1_detection_count,       NULL                   },
    {    DATA_CMD_GET_MF1_DETECTION_RESULT,     NULL,                        cmd_processor_get_mf1_detection_log,         NULL                   },
    {    DATA_CMD_LOAD_MF1_EMU_BLOCK_DATA,      NULL,                        cmd_processor_set_mf1_emulator_block,        NULL                   },
    {    DATA_CMD_READ_MF1_EMU_BLOCK_DATA,      NULL,                        cmd_processor_get_mf1_emulator_block,        NULL                   },
    {    DATA_CMD_SET_MF1_ANTI_COLLISION_RES,   NULL,                        cmd_processor_set_mf1_anti_collision_res,    NULL                   },
    {    DATA_CMD_GET_MF1_EMULATOR_CONFIG,      NULL,                        cmd_processor_get_mf1_info,                  NULL                   },
    {    DATA_CMD_GET_MF1_GEN1A_MODE,           NULL,                        cmd_processor_get_mf1_gen1a_magic_mode,      NULL                   },
    {    DATA_CMD_SET_MF1_GEN1A_MODE,           NULL,                        cmd_processor_set_mf1_gen1a_magic_mode,      NULL                   },
    {    DATA_CMD_GET_MF1_GEN2_MODE,            NULL,                        cmd_processor_get_mf1_gen2_magic_mode,       NULL                   },
    {    DATA_CMD_SET_MF1_GEN2_MODE,            NULL,                        cmd_processor_set_mf1_gen2_magic_mode,       NULL                   },
    {    DATA_CMD_GET_MF1_USE_FIRST_BLOCK_COLL, NULL,                        cmd_processor_get_mf1_use_coll_res,          NULL                   },
    {    DATA_CMD_SET_MF1_USE_FIRST_BLOCK_COLL, NULL,                        cmd_processor_set_mf1_use_coll_res,          NULL                   },
    {    DATA_CMD_GET_MF1_WRITE_MODE,           NULL,                        cmd_processor_get_mf1_write_mode,            NULL                   },
    {    DATA_CMD_SET_MF1_WRITE_MODE,           NULL,                        cmd_processor_set_mf1_write_mode,            NULL                   },
    {    DATA_CMD_GET_MF1_ANTI_COLL_DATA,       NULL,                        cmd_processor_get_mf1_anti_coll_data,        NULL                   },

    {    DATA_CMD_SET_SLOT_TAG_NICK,            NULL,                        cmd_processor_set_slot_tag_nick_name,        NULL                   },
    {    DATA_CMD_GET_SLOT_TAG_NICK,            NULL,                        cmd_processor_get_slot_tag_nick_name,        NULL                   },
};


data_frame_tx_t *cmd_processor_get_capabilities(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    size_t count = sizeof(m_data_cmd_map) / sizeof(m_data_cmd_map[0]);
    uint16_t commands[count];
    memset(commands, 0, count * sizeof(uint16_t));

    for (size_t i = 0; i < count; i++) {
        commands[i] = PP_HTONS(m_data_cmd_map[i].cmd);
    }

    return data_frame_make(cmd, status, count * sizeof(uint16_t), (uint8_t *)commands);
}


void cmd_map_init() {
    size_t count = sizeof(m_data_cmd_map) / sizeof(m_data_cmd_map[0]);

    for (size_t i = 0; i < count; i++) {
        if (m_data_cmd_map[i].cmd == DATA_CMD_GET_DEVICE_CAPABILITIES) {
            m_data_cmd_map[i].cmd_processor = cmd_processor_get_capabilities;
            return;
        }
    }
}

/**
 * @brief Auto select source to response
 *
 * @param resp data
 */
void auto_response_data(data_frame_tx_t *resp) {
    // TODO Please select the reply source automatically according to the message source,
    //  and do not reply by checking the validity of the link layer by layer
    if (is_usb_working()) {
        usb_cdc_write(resp->buffer, resp->length);
    } else if (is_nus_working()) {
        nus_data_response(resp->buffer, resp->length);
    } else {
        NRF_LOG_ERROR("No connection valid found at response client.");
    }
}


/**@brief Function to process data frame(cmd)
 */
void on_data_frame_received(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    data_frame_tx_t *response = NULL;
    bool is_cmd_support = false;
    // print info
    NRF_LOG_INFO("Data frame: cmd = %02x, status = %02x, length = %d", cmd, status, length);
    NRF_LOG_HEXDUMP_INFO(data, length);
    for (int i = 0; i < ARRAY_SIZE(m_data_cmd_map); i++) {
        if (m_data_cmd_map[i].cmd == cmd) {
            is_cmd_support = true;
            if (m_data_cmd_map[i].cmd_before != NULL) {
                data_frame_tx_t *before_resp = m_data_cmd_map[i].cmd_before(cmd, status, length, data);
                if (before_resp != NULL) {
                    // some problem found before run cmd.
                    response = before_resp;
                    break;
                }
            }
            if (m_data_cmd_map[i].cmd_processor != NULL) response = m_data_cmd_map[i].cmd_processor(cmd, status, length, data);
            if (m_data_cmd_map[i].cmd_after != NULL) {
                data_frame_tx_t *after_resp = m_data_cmd_map[i].cmd_after(cmd, status, length, data);
                if (after_resp != NULL) {
                    // some problem found after run cmd.
                    response = after_resp;
                    break;
                }
            }
            break;
        }
    }
    if (is_cmd_support) {
        // check and response
        if (response != NULL) {
            auto_response_data(response);
        }
    } else {
        // response cmd unsupported.
        response = data_frame_make(cmd, STATUS_INVALID_CMD, 0, NULL);
        auto_response_data(response);
        NRF_LOG_INFO("Data frame cmd invalid: %d,", cmd);
    }
}
