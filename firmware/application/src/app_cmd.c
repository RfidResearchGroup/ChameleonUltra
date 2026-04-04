#include "fds_util.h"
#include "bsp_time.h"
#include "bsp_delay.h"
#include "usb_main.h"
#include "rfid_main.h"
#include "ble_main.h"
#include "syssleep.h"
#include "hex_utils.h"
#include "data_cmd.h"
#include "app_cmd.h"
#include "app_status.h"
#include "tag_persistence.h"
#include "nrf_pwr_mgmt.h"
#include "settings.h"
#include "delayed_reset.h"
#include "netdata.h"
#if defined(PROJECT_CHAMELEON_ULTRA)
#include "bsp_wdt.h"
#include "lf_reader_generic.h"
#include "lf_em4x05_data.h"
#include "rc522.h"
#endif
#include "nfc_14a.h"
#include "nfc_14a_4.h"

#define NRF_LOG_MODULE_NAME app_cmd
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


static void change_slot_auto(uint8_t slot_new) {
    uint8_t slot_now = tag_emulation_get_slot();
    device_mode_t mode = get_device_mode();
    tag_emulation_change_slot(slot_new, mode != DEVICE_MODE_READER);
    apply_slot_change(slot_now, slot_new);
}

static data_frame_tx_t *cmd_processor_get_app_version(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint8_t version_major;
        uint8_t version_minor;
    } PACKED payload;
    payload.version_major = APP_FW_VER_MAJOR;
    payload.version_minor = APP_FW_VER_MINOR;
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}


static data_frame_tx_t *cmd_processor_get_git_version(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    return data_frame_make(cmd, STATUS_SUCCESS, strlen(GIT_VERSION), (uint8_t *)GIT_VERSION);
}


static data_frame_tx_t *cmd_processor_get_device_model(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t resp_data = hw_get_device_type();
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(resp_data), &resp_data);
}


static data_frame_tx_t *cmd_processor_change_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 1) || (data[0] > 1)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    if (data[0] == 1) {
#if defined(PROJECT_CHAMELEON_ULTRA)
        reader_mode_enter();
        return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
#else
        return data_frame_make(cmd, STATUS_NOT_IMPLEMENTED, 0, NULL);
#endif
    } else {
#if defined(PROJECT_CHAMELEON_ULTRA)
        tag_mode_enter();
#endif
        return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
    }
}

static data_frame_tx_t *cmd_processor_get_device_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t resp_data = (get_device_mode() == DEVICE_MODE_READER);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(resp_data), &resp_data);
}

static data_frame_tx_t *cmd_processor_enter_bootloader(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // restart to boot
#define BOOTLOADER_DFU_GPREGRET_MASK            (0xB0)
#define BOOTLOADER_DFU_START_BIT_MASK           (0x01)
#define BOOTLOADER_DFU_START    (BOOTLOADER_DFU_GPREGRET_MASK |         BOOTLOADER_DFU_START_BIT_MASK)
    APP_ERROR_CHECK(sd_power_gpregret_clr(0, 0xffffffff));
    APP_ERROR_CHECK(sd_power_gpregret_set(0, BOOTLOADER_DFU_START));
    nrf_pwr_mgmt_shutdown(NRF_PWR_MGMT_SHUTDOWN_GOTO_DFU);
    // Never into here...
    while (1) __NOP();
    // For the compiler to be happy...
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_device_chip_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint32_t chip_HSW;
        uint32_t chip_LSW;
    } PACKED payload;
    payload.chip_LSW = U32HTONL(NRF_FICR->DEVICEID[0]);
    payload.chip_HSW = U32HTONL(NRF_FICR->DEVICEID[1]);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_get_device_address(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // The FICR value is a just a random number, with no knowledge
    // of the Bluetooth Specification requirements for random addresses.
    // So we need to set a Bluetooth LE random address as a static address.
    // See: https://github.com/zephyrproject-rtos/zephyr/blob/7b6b1328a0cb96fe313a5e2bfc57047471df236e/subsys/bluetooth/controller/hci/nordic/hci_vendor.c#L29

    struct {
        uint16_t device_address_HSW;
        uint32_t device_address_LSW;
    } PACKED payload;
    payload.device_address_LSW = U32HTONL(NRF_FICR->DEVICEADDR[0]);
    payload.device_address_HSW = U16HTONS(NRF_FICR->DEVICEADDR[1] | 0xC000);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_save_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = settings_save_config();
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_reset_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    settings_init_config();
    status = settings_save_config();
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_device_settings(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t settings[7 + BLE_PAIRING_KEY_LEN] = {};
    settings[0] = SETTINGS_CURRENT_VERSION; // current version
    settings[1] = settings_get_animation_config(); // animation mode
    settings[2] = settings_get_button_press_config('A'); // short A button press mode
    settings[3] = settings_get_button_press_config('B'); // short B button press mode
    settings[4] = settings_get_long_button_press_config('A'); // long A button press mode
    settings[5] = settings_get_long_button_press_config('B'); // long B button press mode
    settings[6] = settings_get_ble_pairing_enable(); // is device require pairing
    memcpy(settings + 7, settings_get_ble_connect_key(), BLE_PAIRING_KEY_LEN);
    return data_frame_make(cmd, STATUS_SUCCESS, 7 + BLE_PAIRING_KEY_LEN, settings);
}

static data_frame_tx_t *cmd_processor_set_animation_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 1) || (data[0] >= SettingsAnimationModeMAX)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    settings_set_animation_config(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_animation_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t animation_mode = settings_get_animation_config();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &animation_mode);
}

static data_frame_tx_t *cmd_processor_get_battery_info(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint16_t voltage;
        uint8_t percent;
    } PACKED payload;
    payload.voltage = U16HTONS(batt_lvl_in_milli_volts);
    payload.percent = percentage_batt_lvl;
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_get_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 1) || (!is_settings_button_type_valid(data[0]))) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t button_press_config = settings_get_button_press_config(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(button_press_config), &button_press_config);
}

static data_frame_tx_t *cmd_processor_set_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 2) || (!is_settings_button_type_valid(data[0]))) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    settings_set_button_press_config(data[0], data[1]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_long_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 1) || (!is_settings_button_type_valid(data[0]))) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t button_press_config = settings_get_long_button_press_config(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(button_press_config), &button_press_config);
}

static data_frame_tx_t *cmd_processor_set_long_button_press_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 2) || (!is_settings_button_type_valid(data[0]))) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    settings_set_long_button_press_config(data[0], data[1]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_ble_pairing_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t is_enable = settings_get_ble_pairing_enable();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &is_enable);
}

static data_frame_tx_t *cmd_processor_set_ble_pairing_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 && data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    settings_set_ble_pairing_enable(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

#if defined(PROJECT_CHAMELEON_ULTRA)

static data_frame_tx_t *cmd_processor_hf14a_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    picc_14a_tag_t taginfo;
    status = pcd_14a_reader_scan_auto(&taginfo);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    // uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
    // dynamic length, so no struct
    uint8_t payload[1 + sizeof(taginfo.uid) + sizeof(taginfo.atqa) + sizeof(taginfo.sak) + 1 + 254];
    uint16_t offset = 0;
    payload[offset++] = taginfo.uid_len;
    memcpy(&payload[offset], taginfo.uid, taginfo.uid_len);
    offset += taginfo.uid_len;
    memcpy(&payload[offset], taginfo.atqa, sizeof(taginfo.atqa));
    offset += sizeof(taginfo.atqa);
    payload[offset++] = taginfo.sak;
    payload[offset++] = taginfo.ats_len;
    memcpy(&payload[offset], taginfo.ats, taginfo.ats_len);
    offset += taginfo.ats_len;
    return data_frame_make(cmd, STATUS_HF_TAG_OK, offset, payload);
}

static data_frame_tx_t *cmd_processor_mf1_detect_support(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    status = check_std_mifare_nt_support();
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_detect_prng(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t type;
    status = check_prng_type((mf1_prng_type_t *)&type);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(type), &type);
}

// We have a reusable payload structure.
typedef struct {
    uint8_t type_known;
    uint8_t block_known;
    uint8_t key_known[6];
    uint8_t type_target;
    uint8_t block_target;
} PACKED nested_common_payload_t;

static data_frame_tx_t *cmd_processor_mf1_static_nested_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    mf1_static_nested_core_t sncs;
    if (length != sizeof(nested_common_payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    nested_common_payload_t *payload = (nested_common_payload_t *)data;
    status = static_nested_recover_key(bytes_to_num(payload->key_known, 6), payload->block_known, payload->type_known, payload->block_target, payload->type_target, &sncs);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    // mf1_static_nested_core_t is PACKED and comprises only bytes so we can use it directly
    return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(sncs), (uint8_t *)(&sncs));
}

static data_frame_tx_t *cmd_processor_mf1_darkside_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 4) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    struct {
        uint8_t darkside_status;
        // DarksideCore_t is PACKED and comprises only bytes so we can use it directly
        DarksideCore_t dc;
    } PACKED payload;
    status = darkside_recover_key(data[1], data[0], data[2], data[3], &payload.dc, (mf1_darkside_status_t *)&payload.darkside_status);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    if (payload.darkside_status != DARKSIDE_OK) {
        return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(payload.darkside_status), &payload.darkside_status);
    }
    return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_mf1_detect_nt_dist(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t type_known;
        uint8_t block_known;
        uint8_t key_known[6];
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    struct {
        uint8_t uid[4];
        uint32_t distance;
    } PACKED payload_resp;

    payload_t *payload = (payload_t *)data;
    uint32_t distance;
    status = nested_distance_detect(payload->block_known, payload->type_known, payload->key_known, payload_resp.uid, &distance);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    payload_resp.distance = U32HTONL(distance);
    return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(payload_resp), (uint8_t *)&payload_resp);
}

static data_frame_tx_t *cmd_processor_mf1_nested_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    mf1_nested_core_t ncs[SETS_NR];
    if (length != sizeof(nested_common_payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    nested_common_payload_t *payload = (nested_common_payload_t *)data;
    status = nested_recover_key(bytes_to_num(payload->key_known, 6), payload->block_known, payload->type_known, payload->block_target, payload->type_target, ncs);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    // mf1_nested_core_t is PACKED and comprises only bytes so we can use it directly
    return data_frame_make(cmd, STATUS_HF_TAG_OK, sizeof(ncs), (uint8_t *)(&ncs));
}

static data_frame_tx_t *cmd_processor_mf1_enc_nested_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t key[6];
        uint8_t sector_count;
        uint8_t starting_sector;
    } PACKED payload_t;

    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;

    uint64_t ui64Key = bytes_to_num(payload->key, 6);
    uint8_t sector_data[40][sizeof(mf1_static_nonce_sector_t)];
    uint8_t sectors_acquired = 0;
    uint32_t cuid = 0;

    status = mf1_static_encrypted_nonces_acquire(ui64Key, payload->sector_count, payload->starting_sector, sector_data, &sectors_acquired, &cuid);

    uint8_t response_data[sizeof(uint32_t) + sectors_acquired * sizeof(mf1_static_nonce_sector_t)];
    num_to_bytes(cuid, 4, response_data);
    memcpy(response_data + sizeof(uint32_t), sector_data, sectors_acquired * sizeof(mf1_static_nonce_sector_t));

    return data_frame_make(cmd, status, sectors_acquired * sizeof(mf1_static_nonce_sector_t) + sizeof(uint32_t), response_data);
}

static data_frame_tx_t *cmd_processor_mf1_auth_one_key_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t type;
        uint8_t block;
        uint8_t key[6];
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;
    status = auth_key_use_522_hw(payload->block, payload->type, payload->key);
    pcd_14a_reader_mf1_unauth();
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_check_keys_of_sectors(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 16 || (length - 10) % 6 != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    // init
    mf1_toolbox_check_keys_of_sectors_in_t in = {
        .mask = *(mf1_toolbox_check_keys_of_sectors_mask_t *) &data[0],
        .keys_len = (length - 10) / 6,
        .keys = (mf1_key_t *) &data[10]
    };
    mf1_toolbox_check_keys_of_sectors_out_t out;
    status = mf1_toolbox_check_keys_of_sectors(&in, &out);

    return data_frame_make(cmd, status, sizeof(out), (uint8_t *)&out);
}

static data_frame_tx_t *cmd_processor_mf1_check_keys_on_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 9 || data[2] * 6 + 3 != length) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    mf1_toolbox_check_keys_on_block_in_t in = {
        .block = data[0],
        .key_type = data[1],
        .keys_len = data[2],
        .keys = (mf1_key_t *) &data[3]
    };

    mf1_toolbox_check_keys_on_block_out_t out;
    status = mf1_toolbox_check_keys_on_block(&in, &out);

    return data_frame_make(cmd, status, sizeof(out), (uint8_t *)&out);
}

static data_frame_tx_t *cmd_processor_mf1_hardnested_nonces_acquire(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t slow;
        uint8_t type_known;
        uint8_t block_known;
        uint8_t key_known[6];
        uint8_t type_target;
        uint8_t block_target;
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    payload_t *payload = (payload_t *)data;

    // It is enough to collect 110 nonces at a time. The total transmitted data payload is 495 + 1 bytes
    // Then, the total length can be controlled within 4096, so that when encountering a BLE host that supports large packets, one communication can be completed.
    // There is no need to send or receive packets in separate packets, which improves communication speed.
    uint8_t nonces[500] = { 0x00 };
    if (length < 11) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    status = mf1_hardnested_nonces_acquire(
                 payload->slow,
                 payload->block_known,
                 payload->type_known,
                 bytes_to_num(payload->key_known, 6),
                 payload->block_target,
                 payload->type_target,
                 nonces + 1,
                 sizeof(nonces) - 1,         // The upper limit of the buffer size. Here we take out the first byte to mark the number of collections.
                 &nonces[0]                  // The number of random numbers collected above
             );
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    return data_frame_make(cmd, status, nonces[0] * 4.5, (uint8_t *)(nonces + 1));
}

static data_frame_tx_t *cmd_processor_mf1_read_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t type;
        uint8_t block;
        uint8_t key[6];
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;
    uint8_t block[16] = { 0x00 };
    status = auth_key_use_522_hw(payload->block, payload->type, payload->key);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    status = pcd_14a_reader_mf1_read(payload->block, block);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    return data_frame_make(cmd, status, sizeof(block), block);
}

static data_frame_tx_t *cmd_processor_mf1_write_one_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t type;
        uint8_t block;
        uint8_t key[6];
        uint8_t block_data[16];
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;
    status = auth_key_use_522_hw(payload->block, payload->type, payload->key);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    status = pcd_14a_reader_mf1_write(payload->block, payload->block_data);
    return data_frame_make(cmd, status, 0, NULL);
}

#if defined(PROJECT_CHAMELEON_ULTRA)

static data_frame_tx_t *cmd_processor_hf14a_set_field_on(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode != DEVICE_MODE_READER) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    // Reset and turn on the antenna
    pcd_14a_reader_reset();
    pcd_14a_reader_antenna_on();
    
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_hf14a_set_field_off(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode != DEVICE_MODE_READER) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    
    // Turn off the antenna
    pcd_14a_reader_antenna_off();
    
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

#endif

static data_frame_tx_t *cmd_processor_hf14a_raw(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // Response Buffer
    uint8_t resp[DEF_FIFO_LENGTH] = { 0x00 };
    uint16_t resp_length = 0;

    typedef struct {
        struct { // LSB -> MSB
            uint8_t reserved : 2;

            uint8_t check_response_crc : 1;
            uint8_t keep_rf_field : 1;
            uint8_t auto_select : 1;
            uint8_t append_crc : 1;
            uint8_t wait_response : 1;
            uint8_t activate_rf_field : 1;
        } options;

        // U16NTOHS
        uint16_t resp_timeout;
        uint16_t data_bitlength;

        uint8_t data_buffer[0]; // We can have a lot of data or no data. struct just to compute offsets with min options.
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if (length < sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    NRF_LOG_INFO("activate_rf_field  = %d", payload->options.activate_rf_field);
    NRF_LOG_INFO("wait_response      = %d", payload->options.wait_response);
    NRF_LOG_INFO("append_crc         = %d", payload->options.append_crc);
    NRF_LOG_INFO("auto_select        = %d", payload->options.auto_select);
    NRF_LOG_INFO("keep_rf_field      = %d", payload->options.keep_rf_field);
    NRF_LOG_INFO("check_response_crc = %d", payload->options.check_response_crc);
    NRF_LOG_INFO("reserved           = %d", payload->options.reserved);

    status = pcd_14a_reader_raw_cmd(
                 payload->options.activate_rf_field,
                 payload->options.wait_response,
                 payload->options.append_crc,
                 payload->options.auto_select,
                 payload->options.keep_rf_field,
                 payload->options.check_response_crc,

                 U16NTOHS(payload->resp_timeout),

                 U16NTOHS(payload->data_bitlength),
                 payload->data_buffer,

                 resp,
                 &resp_length,
                 U8ARR_BIT_LEN(resp)
             );

    return data_frame_make(cmd, status, resp_length, resp);
}

static data_frame_tx_t *cmd_processor_hf14a_get_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    hf14a_config_t *hc = get_hf14a_config();
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(hf14a_config_t), (uint8_t *)hc);
}

static data_frame_tx_t *cmd_processor_hf14a_set_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != sizeof(hf14a_config_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    hf14a_config_t hc;
    memcpy(&hc, data, sizeof(hf14a_config_t));
    set_hf14a_config(&hc);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_manipulate_value_block(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t src_type;
        uint8_t src_block;
        uint8_t src_key[6];
        uint8_t operator;
        uint32_t operand;
        uint8_t dst_type;
        uint8_t dst_block;
        uint8_t dst_key[6];
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;

    // scan tag
    picc_14a_tag_t taginfo;
    if (pcd_14a_reader_scan_auto(&taginfo) != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, STATUS_HF_TAG_NO, 0, NULL);
    }

    // auth src
    status = pcd_14a_reader_mf1_auth(&taginfo, payload->src_type, payload->src_block, payload->src_key);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }

    // value block operation
    status = pcd_14a_reader_mf1_manipulate_value_block(payload->operator, payload->src_block, (int32_t) U32NTOHL(payload->operand));
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }

    // auth dst if needed
    if (payload->src_block != payload->dst_block || payload->src_type != payload->dst_type) {
        status = pcd_14a_reader_mf1_auth(&taginfo, payload->dst_type, payload->dst_block, payload->dst_key);
        if (status != STATUS_HF_TAG_OK) {
            return data_frame_make(cmd, status, 0, NULL);
        }
    }

    // transfer value block
    status = pcd_14a_reader_mf1_transfer_value_block(payload->dst_block);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }

    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_em410x_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t card_buffer[2 + LF_EM410X_ELECTRA_TAG_ID_SIZE] = {0x00};
    status = scan_em410x(card_buffer);
    if (status != STATUS_LF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }

    tag_specific_type_t tag_type = (card_buffer[0] << 8) | card_buffer[1];
    uint16_t id_size = (tag_type == TAG_TYPE_EM410X_ELECTRA) ? LF_EM410X_ELECTRA_TAG_ID_SIZE : LF_EM410X_TAG_ID_SIZE;

    return data_frame_make(cmd, STATUS_LF_TAG_OK, 2 + id_size, card_buffer);
}

static data_frame_tx_t *cmd_processor_em410x_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t id[5];
        uint8_t new_key[4];
        uint8_t old_keys[4]; // we can have more than one... struct just to compute offsets with min 1 key
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if (length < sizeof(payload_t) || (length - offsetof(payload_t, old_keys)) % sizeof(payload->old_keys) != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    status = write_em410x_to_t55xx(payload->id, payload->new_key, payload->old_keys, (length - offsetof(payload_t, old_keys)) / sizeof(payload->old_keys));
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_em410x_electra_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t id[13];
        uint8_t new_key[4];
        uint8_t old_keys[4]; // we can have more than one... struct just to compute offsets with min 1 key
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if (length < sizeof(payload_t) || (length - offsetof(payload_t, old_keys)) % sizeof(payload->old_keys) != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    status = write_em410x_electra_to_t55xx(payload->id, payload->new_key, payload->old_keys, (length - offsetof(payload_t, old_keys)) / sizeof(payload->old_keys));
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_hidprox_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t id[13];
        uint8_t old_key[4];
        uint8_t new_keys[4]; // we can have more than one... struct just to compute offsets with min 1 key
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if (length < sizeof(payload_t) || (length - offsetof(payload_t, new_keys)) % sizeof(payload->new_keys) != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    uint8_t format = payload->id[0];
    uint32_t fc = bytes_to_num(payload->id+1, 4);
    uint64_t cn = payload->id[5];
    cn = (cn << 32) | (bytes_to_num(payload->id+6, 4));
    uint32_t il = payload->id[10];
    uint32_t oem = bytes_to_num(payload->id+11, 2);
    status = write_hidprox_to_t55xx(format, fc, cn, il, oem, payload->old_key, payload->new_keys, (length - offsetof(payload_t, new_keys)) / sizeof(payload->new_keys));
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_hidprox_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t card_data[16] = {0x00};
    status = scan_hidprox(card_data, data[0]);
    if (status != STATUS_LF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(card_data), card_data);
}

static data_frame_tx_t *cmd_processor_ioprox_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t card_data[16] = {0};
    uint8_t hint = (data != NULL) ? data[0] : 0;
    status = scan_ioprox(card_data, hint);
    if (status != STATUS_LF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }

    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(card_data), card_data);
}

static data_frame_tx_t *cmd_processor_ioprox_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t card_data[16];  // ioprox_codec_t->data layout: version, facility code, card number, raw8
        uint8_t new_key[4];  
        uint8_t old_keys[4];  // we can have more than one... struct just to compute offsets with min 1 key
    } PACKED payload_t;

    payload_t *payload = (payload_t *)data;

    // Validate packet length
    if (length < sizeof(payload_t) ||
        (length - offsetof(payload_t, old_keys)) % sizeof(payload->old_keys) != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    uint8_t old_cnt = (length - offsetof(payload_t, old_keys)) / sizeof(payload->old_keys);

    // Pass card_data (including raw8 at index 4-11) directly to the T55xx writer.
    status = write_ioprox_to_t55xx(
        payload->card_data, 
        payload->new_key, 
        payload->old_keys, 
        old_cnt
    );

    return data_frame_make(cmd, status, 0, NULL);
}

/**
 * @brief Decode raw8 data to structured ioProx format
 * @param raw8 Input 8 bytes
 * @param output 16 bytes ioprox_codec_t->data layout: version, facility code, card number, raw8
 * @return STATUS_SUCCESS on success
 */
static data_frame_tx_t *cmd_processor_ioprox_decode_raw(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 8) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    
    uint8_t output[16];
    uint8_t result = decode_ioprox_raw(data, output);
    
    if (result != STATUS_SUCCESS) {
        return data_frame_make(cmd, STATUS_CMD_ERR, 0, NULL);
    }
    
    return data_frame_make(cmd, STATUS_SUCCESS, 16, output);
}

/**
 * @brief Encode ioProx parameters to structured ioProx format
 * @param ver Version byte
 * @param fc Facility code byte
 * @param cn Card number (16-bit)
 * @param out 16 bytes ioprox_codec_t->data layout: version, facility code, card number, raw8
 * @return STATUS_SUCCESS on success
 */
static data_frame_tx_t *cmd_processor_ioprox_compose_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 4) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    
    uint8_t output[16];
    memset(output, 0, sizeof(output));
    uint8_t result = encode_ioprox_params(data[0], data[1], (data[2] << 8) | data[3], output);
    
    if (result != STATUS_SUCCESS) {
        return data_frame_make(cmd, STATUS_CMD_ERR, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 16, output);
}

static data_frame_tx_t *cmd_processor_viking_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t card_buffer[4] = {0x00};
    status = scan_viking(card_buffer);
    if (status != STATUS_LF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(card_buffer), card_buffer);
}

static data_frame_tx_t *cmd_processor_viking_write_to_t55xx(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t id[4];
        uint8_t new_key[4];
        uint8_t old_keys[4]; // we can have more than one... struct just to compute offsets with min 1 key
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if (length < sizeof(payload_t) || (length - offsetof(payload_t, old_keys)) % sizeof(payload->old_keys) != 0) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    status = write_viking_to_t55xx(payload->id, payload->new_key, payload->old_keys, (length - offsetof(payload_t, old_keys)) / sizeof(payload->old_keys));
    return data_frame_make(cmd, status, 0, NULL);
}

#define GENERIC_READ_LEN 800
#define GENERIC_READ_TIMEOUT_MS 500
static data_frame_tx_t *cmd_processor_generic_read(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t *outdata = malloc(GENERIC_READ_LEN); 

    if (outdata == NULL) {
        return data_frame_make(cmd, STATUS_MEM_ERR, 0, NULL);
    }

    size_t outlen = 0;
    if (!raw_read_to_buffer(outdata, GENERIC_READ_LEN, GENERIC_READ_TIMEOUT_MS, &outlen)) {
        free(outdata);
        return data_frame_make(cmd, STATUS_CMD_ERR, 0, NULL);
    };
    data_frame_tx_t *frame = data_frame_make(cmd, STATUS_LF_TAG_OK, outlen, outdata);

    free(outdata);

    if (frame == NULL) {
        return data_frame_make(cmd, STATUS_CREATE_RESPONSE_ERR, 0, NULL);
    }

    return frame;
}

#endif


static data_frame_tx_t *cmd_processor_set_active_slot(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] >= TAG_MAX_SLOT_NUM) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    change_slot_auto(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_set_slot_tag_type(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t num_slot;
        uint16_t tag_type;
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;
    tag_specific_type_t tag_type = U16NTOHS(payload->tag_type);
    if (payload->num_slot >= TAG_MAX_SLOT_NUM || !is_tag_specific_type_valid(tag_type)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_emulation_change_type(payload->num_slot, tag_type);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_delete_slot_sense_type(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t num_slot;
        uint8_t sense_type;
    } PACKED payload_t;
    payload_t *payload = (payload_t *)data;
    if ((length != sizeof(payload_t)) ||
            (payload->num_slot >= TAG_MAX_SLOT_NUM) ||
            (payload->sense_type != TAG_SENSE_HF && payload->sense_type != TAG_SENSE_LF)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    tag_emulation_delete_data(payload->num_slot, payload->sense_type);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_set_slot_data_default(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t num_slot;
        uint16_t tag_type;
    } PACKED payload_t;
    if (length != sizeof(payload_t)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    payload_t *payload = (payload_t *)data;
    tag_specific_type_t tag_type = U16NTOHS(payload->tag_type);
    if (payload->num_slot >= TAG_MAX_SLOT_NUM || !is_tag_specific_type_valid(tag_type)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    status = tag_emulation_factory_data(payload->num_slot, tag_type) ? STATUS_SUCCESS : STATUS_NOT_IMPLEMENTED;
    return data_frame_make(cmd, status, 0, NULL);
}

static data_frame_tx_t *cmd_processor_set_slot_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    typedef struct {
        uint8_t slot_index;
        uint8_t sense_type;
        uint8_t enabled;
    } PACKED payload_t;

    payload_t *payload = (payload_t *)data;
    if (length != sizeof(payload_t) ||
            payload->slot_index >= TAG_MAX_SLOT_NUM ||
            (payload->sense_type != TAG_SENSE_HF && payload->sense_type != TAG_SENSE_LF) ||
            payload->enabled > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    uint8_t slot_now = payload->slot_index;
    tag_emulation_slot_set_enable(slot_now, payload->sense_type, payload->enabled);
    if ((!payload->enabled) &&
            (!is_slot_enabled(slot_now, payload->sense_type == TAG_SENSE_HF ? TAG_SENSE_LF : TAG_SENSE_HF))) {
        // HF and LF disabled, need to change slot
        uint8_t slot_prev = tag_emulation_slot_find_next(slot_now);
        NRF_LOG_INFO("slot_now = %d, slot_prev = %d", slot_now, slot_prev);
        if (slot_prev == slot_now) {
            set_slot_light_color(RGB_MAGENTA);
        } else {
            change_slot_auto(slot_prev);
        }
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_slot_data_config_save(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_emulation_save();
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_active_slot(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t slot = tag_emulation_get_slot();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &slot);
}

static data_frame_tx_t *cmd_processor_get_slot_info(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint16_t hf_tag_type;
        uint16_t lf_tag_type;
    } PACKED payload[8];

    tag_slot_specific_type_t tag_types;
    for (uint8_t slot = 0; slot < 8; slot++) {
        tag_emulation_get_specific_types_by_slot(slot, &tag_types);
        payload[slot].hf_tag_type = U16HTONS(tag_types.tag_hf);
        payload[slot].lf_tag_type = U16HTONS(tag_types.tag_lf);
    }

    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_wipe_fds(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    bool success = fds_wipe();
    status = success ? STATUS_SUCCESS : STATUS_FLASH_WRITE_FAIL;
    delayed_reset(50);
    return data_frame_make(cmd, status, 0, NULL);
}

static bool get_active_em410x_type(tag_specific_type_t *tag_type_out, uint16_t *id_size_out) {
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(tag_emulation_get_slot(), &tag_types);
    if (tag_types.tag_lf == TAG_TYPE_EM410X || tag_types.tag_lf == TAG_TYPE_EM410X_ELECTRA) {
        *tag_type_out = tag_types.tag_lf;
        *id_size_out = (tag_types.tag_lf == TAG_TYPE_EM410X_ELECTRA) ? LF_EM410X_ELECTRA_TAG_ID_SIZE : LF_EM410X_TAG_ID_SIZE;
        return true;
    }
    return false;
}

static data_frame_tx_t *cmd_processor_em410x_set_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_specific_type_t tag_type;
    uint16_t id_size;
    if (!get_active_em410x_type(&tag_type, &id_size) || length != id_size) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    memcpy(buffer->buffer, data, id_size);
    tag_emulation_load_by_buffer(tag_type, false);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_em410x_get_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_specific_type_t tag_type;
    uint16_t id_size;
    if (!get_active_em410x_type(&tag_type, &id_size)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data);  // no data in slot, don't send garbage
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    uint8_t resp[2 + LF_EM410X_ELECTRA_TAG_ID_SIZE] = {0x00};
    resp[0] = tag_type >> 8;
    resp[1] = tag_type;
    memcpy(resp + 2, buffer->buffer, id_size);
    return data_frame_make(cmd, STATUS_SUCCESS, 2 + id_size, resp);
}

static data_frame_tx_t *cmd_processor_hidprox_set_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != LF_HIDPROX_TAG_ID_SIZE) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_HID_PROX);
    memcpy(buffer->buffer, data, LF_HIDPROX_TAG_ID_SIZE);
    tag_emulation_load_by_buffer(TAG_TYPE_HID_PROX, false);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_hidprox_get_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(tag_emulation_get_slot(), &tag_types);
    if (tag_types.tag_lf != TAG_TYPE_HID_PROX) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data);  // no data in slot, don't send garbage
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_HID_PROX);
    return data_frame_make(cmd, STATUS_SUCCESS, LF_HIDPROX_TAG_ID_SIZE, buffer->buffer);
}

static data_frame_tx_t *cmd_processor_ioprox_set_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != LF_IOPROX_TAG_ID_SIZE) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_IOPROX);
    memcpy(buffer->buffer, data, LF_IOPROX_TAG_ID_SIZE);
    tag_emulation_load_by_buffer(TAG_TYPE_IOPROX, false);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_ioprox_get_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(tag_emulation_get_slot(), &tag_types);
    if (tag_types.tag_lf != TAG_TYPE_IOPROX) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data);  // no data in slot, don't send garbage
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_IOPROX);
    return data_frame_make(cmd, STATUS_SUCCESS, LF_IOPROX_TAG_ID_SIZE, buffer->buffer);
}

static data_frame_tx_t *cmd_processor_viking_set_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != LF_VIKING_TAG_ID_SIZE) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_VIKING);
    memcpy(buffer->buffer, data, LF_VIKING_TAG_ID_SIZE);
    tag_emulation_load_by_buffer(TAG_TYPE_VIKING, false);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_viking_get_emu_id(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    tag_slot_specific_type_t tag_types;
    tag_emulation_get_specific_types_by_slot(tag_emulation_get_slot(), &tag_types);
    if (tag_types.tag_lf != TAG_TYPE_VIKING) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, data);  // no data in slot, don't send garbage
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_VIKING);
    return data_frame_make(cmd, STATUS_SUCCESS, LF_VIKING_TAG_ID_SIZE, buffer->buffer);
}

static nfc_tag_14a_coll_res_reference_t *get_coll_res_data(bool write) {
    nfc_tag_14a_coll_res_reference_t *info;
    tag_slot_specific_type_t tag_types;

    tag_emulation_get_specific_types_by_slot(tag_emulation_get_slot(), &tag_types);

    switch (tag_types.tag_hf) {
        case TAG_TYPE_MIFARE_1024:
        case TAG_TYPE_MIFARE_2048:
        case TAG_TYPE_MIFARE_4096:
        case TAG_TYPE_MIFARE_Mini:
            info = write ? get_mifare_coll_res() : get_saved_mifare_coll_res();
            break;
        case TAG_TYPE_MF0ICU1:
        case TAG_TYPE_MF0ICU2:
        case TAG_TYPE_MF0UL11:
        case TAG_TYPE_MF0UL21:
        case TAG_TYPE_NTAG_210:
        case TAG_TYPE_NTAG_212:
        case TAG_TYPE_NTAG_213:
        case TAG_TYPE_NTAG_215:
        case TAG_TYPE_NTAG_216:
            info = nfc_tag_mf0_ntag_get_coll_res();
            break;
        case TAG_TYPE_HF14A_4:
            info = nfc_tag_14a_4_get_coll_res();
            break;
        default:
            // no collision resolution data for slot
            info = NULL;
            break;
    }

    return info;
}

static data_frame_tx_t *cmd_processor_hf14a_get_anti_coll_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    nfc_tag_14a_coll_res_reference_t *info = get_coll_res_data(false);

    if (info == NULL) return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);

    // uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
    // dynamic length, so no struct
    uint8_t payload[1 + *info->size + 2 + 1 + 1 + 254];
    uint16_t offset = 0;
    payload[offset++] = *info->size;
    memcpy(&payload[offset], info->uid, *info->size);
    offset += *info->size;
    memcpy(&payload[offset], info->atqa, 2);
    offset += 2;
    payload[offset++] = *info->sak;
    if (info->ats->length > 0) {
        payload[offset++] = info->ats->length;
        memcpy(&payload[offset], info->ats->data, info->ats->length);
        offset += info->ats->length;
    } else {
        payload[offset++] = 0;
    }
    return data_frame_make(cmd, STATUS_SUCCESS, offset, payload);
}

static data_frame_tx_t *cmd_processor_mf1_set_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_detection_log_clear();
    nfc_tag_mf1_set_detection_enable(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t is_enable = nfc_tag_mf1_is_detection_enable();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, (uint8_t *)(&is_enable));
}

static data_frame_tx_t *cmd_processor_mf1_get_detection_count(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count = nfc_tag_mf1_detection_log_count();
    if (count == 0xFFFFFFFF) {
        count = 0;
    }
    uint32_t payload = U32HTONL(count);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(uint32_t), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_mf1_get_detection_log(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count;
    uint32_t index;
    uint8_t *resp = NULL;
    nfc_tag_mf1_auth_log_t *logs = mf1_get_auth_log(&count);
    if (length != 4 || count == 0xFFFFFFFF) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    index = U32NTOHL(*(uint32_t *)data);
    // NRF_LOG_INFO("index = %d", index);
    if (index >= count) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    resp = (uint8_t *)(logs + index);
    length = MIN(count - index, NETDATA_MAX_DATA_LENGTH / sizeof(nfc_tag_mf1_auth_log_t)) * sizeof(nfc_tag_mf1_auth_log_t);
    return data_frame_make(cmd, STATUS_SUCCESS, length, resp);
}

static data_frame_tx_t *cmd_processor_mf1_write_emu_block_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 0 || (((length - 1) % NFC_TAG_MF1_DATA_SIZE) != 0)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t block_index = data[0];
    uint8_t block_count = (length - 1) / NFC_TAG_MF1_DATA_SIZE;
    if (block_index + block_count > NFC_TAG_MF1_BLOCK_MAX) {
       return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_4096);
    nfc_tag_mf1_information_t *info = (nfc_tag_mf1_information_t *)buffer->buffer;
    for (int i = 1, j = block_index; i < length; i += NFC_TAG_MF1_DATA_SIZE, j++) {
        uint8_t *p_block = &data[i];
        memcpy(info->memory[j], p_block, NFC_TAG_MF1_DATA_SIZE);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_read_emu_block_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if ((length != 2) || (data[1] < 1) || (data[1] > 32) || (data[0] + data[1] > NFC_TAG_MF1_BLOCK_MAX)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t block_index = data[0];
    uint8_t block_count = data[1];
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(TAG_TYPE_MIFARE_4096);
    nfc_tag_mf1_information_t *info = (nfc_tag_mf1_information_t *)buffer->buffer;
    uint16_t result_length = block_count * NFC_TAG_MF1_DATA_SIZE;
    uint8_t result_buffer[result_length];
    for (int i = 0, j = block_index; i < result_length; i += NFC_TAG_MF1_DATA_SIZE, j++) {
        memcpy(&result_buffer[i], info->memory[j], NFC_TAG_MF1_DATA_SIZE);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, result_length, result_buffer);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_write_emu_page_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t byte;
    uint8_t active_slot = tag_emulation_get_slot();

    tag_slot_specific_type_t active_slot_tag_types;
    tag_emulation_get_specific_types_by_slot(active_slot, &active_slot_tag_types);

    int nr_pages = nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(active_slot_tag_types.tag_hf);
    // This means wrong slot type.
    if (nr_pages <= 0) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, data);

    if (length < 2) {
        byte = nr_pages;
        return data_frame_make(cmd, STATUS_PAR_ERR, 1, &byte);
    }

    int page_index = data[0];
    int pages_count = data[1];
    int byte_length = (int)pages_count * NFC_TAG_MF0_NTAG_DATA_SIZE;

    if (pages_count == 0) return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
    else if (
        (page_index >= ((int)nr_pages))
        || (pages_count > (((int)nr_pages) - page_index))
        || (((int)length - 2) < byte_length)
    ) {
        byte = nr_pages;
        return data_frame_make(cmd, STATUS_PAR_ERR, 1, &byte);
    }

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(active_slot_tag_types.tag_hf);
    nfc_tag_mf0_ntag_information_t *info = (nfc_tag_mf0_ntag_information_t *)buffer->buffer;

    memcpy(&info->memory[page_index][0], &data[2], byte_length);

    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_emu_page_count(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t byte;
    uint8_t active_slot = tag_emulation_get_slot();

    tag_slot_specific_type_t active_slot_tag_types;
    tag_emulation_get_specific_types_by_slot(active_slot, &active_slot_tag_types);

    int nr_pages = nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(active_slot_tag_types.tag_hf);
    // This means wrong slot type.
    if (nr_pages <= 0) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, data);

    // Convert the int value to u8 value if it's valid.
    byte = nr_pages;
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &byte);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_read_emu_page_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t byte;
    uint8_t active_slot = tag_emulation_get_slot();

    tag_slot_specific_type_t active_slot_tag_types;
    tag_emulation_get_specific_types_by_slot(active_slot, &active_slot_tag_types);

    int nr_pages = nfc_tag_mf0_ntag_get_nr_pages_by_tag_type(active_slot_tag_types.tag_hf);
    // This means wrong slot type.
    if (nr_pages <= 0) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, data);

    if (length < 2) {
        byte = nr_pages;
        return data_frame_make(cmd, STATUS_PAR_ERR, 1, &byte);
    }

    int page_index = data[0];
    int pages_count = data[1];

    if (pages_count == 0) return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(active_slot_tag_types.tag_hf);
    nfc_tag_mf0_ntag_information_t *info = (nfc_tag_mf0_ntag_information_t *)buffer->buffer;

    return data_frame_make(cmd, STATUS_SUCCESS, pages_count * NFC_TAG_MF0_NTAG_DATA_SIZE, &info->memory[page_index][0]);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_version_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t *version_data = nfc_tag_mf0_ntag_get_version_data();
    if (version_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);

    return data_frame_make(cmd, STATUS_SUCCESS, NFC_TAG_MF0_NTAG_VER_SIZE, version_data);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_version_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 8) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);

    uint8_t *version_data = nfc_tag_mf0_ntag_get_version_data();
    if (version_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);
    memcpy(version_data, data, NFC_TAG_MF0_NTAG_VER_SIZE);

    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_signature_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t *signature_data = nfc_tag_mf0_ntag_get_signature_data();
    if (signature_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);

    return data_frame_make(cmd, STATUS_SUCCESS, NFC_TAG_MF0_NTAG_SIG_SIZE, signature_data);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_signature_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != NFC_TAG_MF0_NTAG_SIG_SIZE) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);

    uint8_t *signature_data = nfc_tag_mf0_ntag_get_signature_data();
    if (signature_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);
    memcpy(signature_data, data, NFC_TAG_MF0_NTAG_SIG_SIZE);

    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_counter_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);

    uint8_t index = data[0] & 0x7F;
    uint8_t *counter_data = nfc_tag_mf0_ntag_get_counter_data_by_index(index);
    if (counter_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);

    bool tearing = (counter_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] & MF0_NTAG_AUTHLIM_MASK_IN_CTR) != 0;

    uint8_t response[4];
    memcpy(response, counter_data, 3);
    response[3] = tearing ? 0x00 : 0xBD;

    return data_frame_make(cmd, STATUS_SUCCESS, 4, response);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_counter_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 4) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);

    uint8_t index = data[0] & 0x7F;
    uint8_t *counter_data = nfc_tag_mf0_ntag_get_counter_data_by_index(index);
    if (counter_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);

    // clear tearing event flag
    if ((data[0] & 0x80) == 0x80) {
        counter_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] &= ~MF0_NTAG_TEARING_MASK_IN_AUTHLIM;
    }

    // copy the actual counter value
    memcpy(counter_data, &data[1], 3);

    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_reset_auth_cnt(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // all tags with counters support auth
    uint8_t *counter_data = nfc_tag_mf0_ntag_get_counter_data_by_index(0);
    if (counter_data == NULL) return data_frame_make(cmd, STATUS_INVALID_SLOT_TYPE, 0, NULL);

    uint8_t old_value = counter_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] & MF0_NTAG_AUTHLIM_MASK_IN_CTR;
    counter_data[MF0_NTAG_AUTHLIM_OFF_IN_CTR] &= ~MF0_NTAG_AUTHLIM_MASK_IN_CTR;

    return data_frame_make(cmd, STATUS_SUCCESS, 1, &old_value);
}

static data_frame_tx_t *cmd_processor_hf14a_set_anti_coll_data(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    // uidlen[1]|uid[uidlen]|atqa[2]|sak[1]|atslen[1]|ats[atslen]
    // dynamic length, so no struct
    if ((length < 1) || \
            (!is_valid_uid_size(data[0])) || \
            (length < 1 + data[0] + 2 + 1 + 1) || \
            (length < 1 + data[0] + 2 + 1 + 1 + data[1 + data[0] + 2 + 1])) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_14a_coll_res_reference_t *info = get_coll_res_data(true);

    uint16_t offset = 0;
    *(info->size) = (nfc_tag_14a_uid_size)data[offset];
    offset++;
    memcpy(info->uid, &data[offset], *(info->size));
    offset += *(info->size);
    memcpy(info->atqa, &data[offset], 2);
    offset += 2;
    info->sak[0] = data[offset];
    offset ++;
    info->ats->length = data[offset];
    offset ++;
    memcpy(info->ats->data, &data[offset], info->ats->length);
    offset += info->ats->length;
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_set_slot_tag_nick(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 3 || length > 34) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t slot = data[0];
    uint8_t sense_type = data[1];
    fds_slot_record_map_t map_info;
    if (slot >= TAG_MAX_SLOT_NUM || (sense_type != TAG_SENSE_HF && sense_type != TAG_SENSE_LF)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    get_fds_map_by_slot_sense_type_for_nick(slot, sense_type, &map_info);

    uint8_t buffer[36];
    buffer[0] = length - 2;
    memcpy(buffer + 1, data + 2, buffer[0]);

    bool ret = fds_write_sync(map_info.id, map_info.key, sizeof(buffer), buffer);
    if (!ret) {
        return data_frame_make(cmd, STATUS_FLASH_WRITE_FAIL, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_slot_tag_nick(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 2) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t slot = data[0];
    uint8_t sense_type = data[1];
    uint8_t buffer[36];
    fds_slot_record_map_t map_info;

    if (slot >= TAG_MAX_SLOT_NUM || (sense_type != TAG_SENSE_HF && sense_type != TAG_SENSE_LF)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    get_fds_map_by_slot_sense_type_for_nick(slot, sense_type, &map_info);
    uint16_t buffer_length = sizeof(buffer);
    bool ret = fds_read_sync(map_info.id, map_info.key, &buffer_length, buffer);
    if (!ret) {
        return data_frame_make(cmd, STATUS_FLASH_READ_FAIL, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, buffer[0], &buffer[1]);
}

static data_frame_tx_t *cmd_processor_get_all_slot_nicks(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t response_buffer[TAG_MAX_SLOT_NUM * 2 * 37]; // Max possible size: 8 slots * 2 sense types * (1 byte length + 36 bytes nick)
    uint16_t response_length = 0;

    for (uint8_t slot = 0; slot < TAG_MAX_SLOT_NUM; slot++) {
        uint8_t hf_buffer[36];
        fds_slot_record_map_t hf_map_info;
        get_fds_map_by_slot_sense_type_for_nick(slot, TAG_SENSE_HF, &hf_map_info);
        uint16_t hf_buffer_length = sizeof(hf_buffer);
        bool hf_ret = fds_read_sync(hf_map_info.id, hf_map_info.key, &hf_buffer_length, hf_buffer);

        if (hf_ret && hf_buffer_length > 0) {
            response_buffer[response_length++] = hf_buffer[0];
            for (uint8_t i = 1; i <= hf_buffer[0] && i < hf_buffer_length; i++) {
                response_buffer[response_length++] = hf_buffer[i];
            }
        } else {
            response_buffer[response_length++] = 0;
        }

        uint8_t lf_buffer[36];
        fds_slot_record_map_t lf_map_info;
        get_fds_map_by_slot_sense_type_for_nick(slot, TAG_SENSE_LF, &lf_map_info);
        uint16_t lf_buffer_length = sizeof(lf_buffer);
        bool lf_ret = fds_read_sync(lf_map_info.id, lf_map_info.key, &lf_buffer_length, lf_buffer);

        if (lf_ret && lf_buffer_length > 0) {
            response_buffer[response_length++] = lf_buffer[0];
            for (uint8_t i = 1; i <= lf_buffer[0] && i < lf_buffer_length; i++) {
                response_buffer[response_length++] = lf_buffer[i];
            }
        } else {
            response_buffer[response_length++] = 0;
        }
    }

    return data_frame_make(cmd, STATUS_SUCCESS, response_length, response_buffer);
}

static data_frame_tx_t *cmd_processor_delete_slot_tag_nick(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 2) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    uint8_t slot = data[0];
    uint8_t sense_type = data[1];
    fds_slot_record_map_t map_info;

    if (slot >= TAG_MAX_SLOT_NUM || (sense_type != TAG_SENSE_HF && sense_type != TAG_SENSE_LF)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    get_fds_map_by_slot_sense_type_for_nick(slot, sense_type, &map_info);
    bool ret = fds_delete_sync(map_info.id, map_info.key);
    if (!ret) {
        return data_frame_make(cmd, STATUS_FLASH_WRITE_FAIL, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_emulator_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mf1_info[5] = {};
    mf1_info[0] = nfc_tag_mf1_is_detection_enable();
    mf1_info[1] = nfc_tag_mf1_is_gen1a_magic_mode();
    mf1_info[2] = nfc_tag_mf1_is_gen2_magic_mode();
    mf1_info[3] = nfc_tag_mf1_is_use_mf1_coll_res();
    mf1_info[4] = nfc_tag_mf1_get_write_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 5, mf1_info);
}

static data_frame_tx_t *cmd_processor_mf1_get_gen1a_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mode = nfc_tag_mf1_is_gen1a_magic_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
}

static data_frame_tx_t *cmd_processor_mf1_set_gen1a_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 && data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_set_gen1a_magic_mode(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_gen2_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mode = nfc_tag_mf1_is_gen2_magic_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
}

static data_frame_tx_t *cmd_processor_mf1_set_gen2_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 && data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_set_gen2_magic_mode(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_block_anti_coll_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mode = nfc_tag_mf1_is_use_mf1_coll_res();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
}

static data_frame_tx_t *cmd_processor_mf1_set_block_anti_coll_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 && data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_set_use_mf1_coll_res(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mode = nfc_tag_mf1_get_write_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
}

static data_frame_tx_t *cmd_processor_mf1_set_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] > 3) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_set_write_mode(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf1_get_field_off_do_reset(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t enable = nfc_tag_mf1_is_field_off_do_reset();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &enable);
}

static data_frame_tx_t *cmd_processor_mf1_set_field_off_do_reset(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] >= 2) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf1_set_field_off_do_reset(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_get_enabled_slots(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    struct {
        uint8_t enabled_hf;
        uint8_t enabled_lf;
    } PACKED payload[8];
    for (uint8_t slot = 0; slot < 8; slot++) {
        payload[slot].enabled_hf = is_slot_enabled(slot, TAG_SENSE_HF);
        payload[slot].enabled_lf = is_slot_enabled(slot, TAG_SENSE_LF);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_get_ble_connect_key(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    return data_frame_make(cmd, STATUS_SUCCESS, BLE_PAIRING_KEY_LEN, settings_get_ble_connect_key());
}

static data_frame_tx_t *cmd_processor_set_ble_connect_key(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != BLE_PAIRING_KEY_LEN) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    // Must be 6 ASCII characters, can only be 0-9.
    bool is_valid_key = true;
    for (uint8_t i = 0; i < BLE_PAIRING_KEY_LEN; i++) {
        if (data[i] < '0' || data[i] > '9') {
            is_valid_key = false;
            break;
        }
    }
    if (!is_valid_key) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    // Key is valid, we can update to config
    settings_set_ble_connect_key(data);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_delete_all_ble_bonds(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    advertising_stop();
    delete_bonds_all();
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

#if defined(PROJECT_CHAMELEON_ULTRA)


/**
 * before reader run, reset reader and on antenna,
 * we must to wait some time, to init picc(power).
 */
static data_frame_tx_t *before_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    device_mode_t mode = get_device_mode();
    if (mode != DEVICE_MODE_READER) {
        return data_frame_make(cmd, STATUS_DEVICE_MODE_ERROR, 0, NULL);
    }
    return NULL;
}


/**
 * before reader run, reset reader and on antenna,
 * we must to wait some time, to init picc(power).
 */
static data_frame_tx_t *before_hf_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
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
static data_frame_tx_t *after_hf_reader_run(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    pcd_14a_reader_antenna_off();
    return NULL;
}

#endif

// fct will be defined after m_data_cmd_map because we need to know its size
data_frame_tx_t *cmd_processor_get_device_capabilities(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data);

static data_frame_tx_t *cmd_processor_mf0_ntag_get_uid_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    int rc = nfc_tag_mf0_ntag_get_uid_mode();
    if (rc < 0) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    else {
        uint8_t mode = rc;
        return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
    }
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_uid_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || !nfc_tag_mf0_ntag_set_uid_mode(data[0] != 0)) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mode = nfc_tag_mf0_ntag_get_write_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, &mode);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_write_mode(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] > 4) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf0_ntag_set_write_mode(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_set_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length != 1 || data[0] > 1) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_mf0_ntag_detection_log_clear();
    nfc_tag_mf0_ntag_set_detection_enable(data[0]);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_detection_enable(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t is_enable = nfc_tag_mf0_ntag_is_detection_enable();
    return data_frame_make(cmd, STATUS_SUCCESS, 1, (uint8_t *)(&is_enable));
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_detection_count(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count = nfc_tag_mf0_ntag_detection_log_count();
    if (count == 0xFFFFFFFF) {
        count = 0;
    }
    uint32_t payload = U32HTONL(count);
    return data_frame_make(cmd, STATUS_SUCCESS, sizeof(uint32_t), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_mf0_ntag_get_detection_log(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint32_t count;
    uint32_t index;
    uint8_t *resp = NULL;
    nfc_tag_mf0_ntag_auth_log_t *logs = mf0_get_auth_log(&count);
    if (length != 4 || count == 0xFFFFFFFF) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    index = U32NTOHL(*(uint32_t *)data);
    if (index >= count) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    resp = (uint8_t *)(logs + index);
    length = MIN(count - index, NETDATA_MAX_DATA_LENGTH / sizeof(nfc_tag_mf0_ntag_auth_log_t)) * sizeof(nfc_tag_mf0_ntag_auth_log_t);
    return data_frame_make(cmd, STATUS_SUCCESS, length, resp);
}

static data_frame_tx_t *cmd_processor_mf0_get_emulator_config(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t mf0_info[3] = {};
    mf0_info[0] = nfc_tag_mf0_ntag_is_detection_enable();
    mf0_info[1] = nfc_tag_mf0_ntag_get_uid_mode();
    mf0_info[2] = nfc_tag_mf0_ntag_get_write_mode();
    return data_frame_make(cmd, STATUS_SUCCESS, 3, mf0_info);
}

/**
 * (cmd -> processor) function map, the map struct is:
 *       cmd code                               before process               cmd processor                                after process
 */
#if defined(PROJECT_CHAMELEON_ULTRA)
static data_frame_tx_t *cmd_processor_em4x05_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    em4x05_data_t tag = {0};
    status = scan_em4x05(&tag);
    if (status != STATUS_LF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    struct {
        uint32_t config;
        uint32_t uid;
        uint32_t uid_hi;
        uint8_t  is_em4x69;
    } PACKED payload;
    payload.config    = U32HTONL(tag.config);
    payload.uid       = U32HTONL(tag.uid);
    payload.uid_hi    = U32HTONL(tag.uid_hi);
    payload.is_em4x69 = tag.is_em4x69 ? 1 : 0;
    return data_frame_make(cmd, STATUS_LF_TAG_OK, sizeof(payload), (uint8_t *)&payload);
}

static data_frame_tx_t *cmd_processor_lf_sniff(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    /* Optional 2-byte big-endian timeout in ms from host (default 2000ms) */
    uint32_t timeout_ms = 2000;
    if (length >= 2) {
        timeout_ms = ((uint32_t)data[0] << 8) | data[1];
        if (timeout_ms == 0 || timeout_ms > 10000) timeout_ms = 2000;
    }

    static uint8_t sniff_buf[LF_SNIFF_MAX_SAMPLES];
    size_t outlen = 0;
    raw_read_to_buffer(sniff_buf, LF_SNIFF_MAX_SAMPLES, timeout_ms, &outlen);

    if (outlen == 0) {
        return data_frame_make(cmd, STATUS_LF_TAG_NO_FOUND, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_LF_TAG_OK, (uint16_t)outlen, sniff_buf);
}
#define HF_SNIFF_BUF_SIZE   3800   /* leave room for USB framing */
#define HF_SNIFF_MAX_FRAMES  200

static uint8_t  m_sniff_buf[HF_SNIFF_BUF_SIZE];
static uint16_t m_sniff_buf_len = 0;
static bool     m_sniff_active  = false;
static uint16_t m_sniff_cb_count = 0;   /* debug: total callback invocations */

static void hf14a_sniff_frame_cb(const uint8_t *data, uint16_t szBits) {
    m_sniff_cb_count++;   /* count even if buffer full or inactive */
    if (!m_sniff_active) return;
    uint16_t szBytes = (szBits + 7) / 8;
    /* Check space: 2 bytes header + data */
    if (m_sniff_buf_len + 2 + szBytes > HF_SNIFF_BUF_SIZE) return;
    /* Write bit count big-endian */
    m_sniff_buf[m_sniff_buf_len++] = (szBits >> 8) & 0xFF;
    m_sniff_buf[m_sniff_buf_len++] = szBits & 0xFF;
    /* Write frame bytes */
    memcpy(&m_sniff_buf[m_sniff_buf_len], data, szBytes);
    m_sniff_buf_len += szBytes;
}

static data_frame_tx_t *cmd_processor_hf14a_sniff(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    /* Optional 2-byte big-endian timeout in ms (default 5000ms) */
    uint32_t timeout_ms = 5000;
    if (length >= 2) {
        timeout_ms = ((uint32_t)data[0] << 8) | data[1];
        if (timeout_ms == 0 || timeout_ms > 30000) timeout_ms = 5000;
    }

    /* Reload active slot data before sniffing.
     * The NFCT anti-collision response is built from m_tag_information which
     * points into the shared tag data buffer. After a slot switch the buffer
     * may still contain the previous slot's UID if the FDS async load has not
     * completed. A forced reload here ensures the correct UID is presented
     * during the sniff session.
     * A short settle delay follows to allow the reload to complete before
     * the first field detection can trigger the anti-collision path. */
    tag_emulation_load_data();
    bsp_delay_ms(100);

    /* Install sniff callback into the already-running tag emulation stack.
     * Do NOT call tag_mode_enter() or sense_switch() here — those reinit
     * NFCT and wipe the anti-collision data, breaking the emulation.
     * The device must already be in emulator mode (hw mode --emulator)
     * with a slot active before running this command. */
    m_sniff_buf_len = 0;
    m_sniff_cb_count = 0;
    m_sniff_active  = true;
    nfc_tag_14a_set_sniff_cb(hf14a_sniff_frame_cb);

    /* Wait for duration, yielding each ms so USB stack stays alive.
     * Feed watchdog every iteration — WDT timeout is 5000ms and the
     * main loop cannot feed it while we are blocking here. */
    autotimer *p_at = bsp_obtain_timer(0);
    while (NO_TIMEOUT_1MS(p_at, timeout_ms)) {
        bsp_delay_ms(1);
        bsp_wdt_feed();
    }
    bsp_return_timer(p_at);

    /* Remove callback and restore normal sense state */
    m_sniff_active = false;
    nfc_tag_14a_clear_sniff_cb();
    tag_emulation_sense_run();  /* restore slot-based sense state */

    if (m_sniff_buf_len == 0) {
        return data_frame_make(cmd, STATUS_HF_TAG_NO, 0, NULL);
    }
    return data_frame_make(cmd, STATUS_SUCCESS, m_sniff_buf_len, m_sniff_buf);
}

#endif


/* ========================================================================
 * HF14A-4 ISO14443-4 T=CL emulation commands (6000-range)
 * ======================================================================== */

/**
 * HF14A-4 APDU recv — non-blocking poll.
 * Returns STATUS_SUCCESS + APDU bytes if one is pending, STATUS_HF_TAG_NO otherwise.
 */
static data_frame_tx_t *cmd_processor_hf14a_4_apdu_recv(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    static uint8_t apdu_buf[NFC_14A_4_MAX_APDU];
    uint16_t apdu_len = 0;
    extern bool nfc_tag_14a_4_get_pending_apdu(uint8_t *buf, uint16_t *length);
    if (nfc_tag_14a_4_get_pending_apdu(apdu_buf, &apdu_len)) {
        return data_frame_make(cmd, STATUS_SUCCESS, apdu_len, apdu_buf);
    }
    return data_frame_make(cmd, STATUS_HF_TAG_NO, 0, NULL);
}

/**
 * HF14A-4 APDU send — push a response for the next WTX-waiting I-block.
 * payload: len_be16(2) + resp_bytes
 */
static data_frame_tx_t *cmd_processor_hf14a_4_apdu_send(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    extern void nfc_tag_14a_4_set_response(const uint8_t *data, uint16_t length);
    if (length < 2) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    uint16_t resp_len = ((uint16_t)data[0] << 8) | data[1];
    if (resp_len > NFC_14A_4_MAX_APDU || length < 2 + resp_len)
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    nfc_tag_14a_4_set_response(&data[2], resp_len);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

/**
 * HF14A-4 set anti-collision data (UID/ATQA/SAK/ATS).
 * payload: uid_len(1) uid(n) atqa(2) sak(1) ats_len(1) ats(m)
 */
static data_frame_tx_t *cmd_processor_hf14a_4_set_anti_coll(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length < 1 || !is_valid_uid_size(data[0]) ||
            length < 1 + data[0] + 2 + 1 + 1 ||
            length < 1 + data[0] + 2 + 1 + 1 + data[1 + data[0] + 2 + 1]) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }
    nfc_tag_14a_coll_res_reference_t *info = get_coll_res_data(true);
    if (info == NULL) return data_frame_make(cmd, STATUS_HF_TAG_NO, 0, NULL);
    uint16_t offset = 0;
    *(info->size) = (nfc_tag_14a_uid_size)data[offset]; offset++;
    memcpy(info->uid,  &data[offset], *(info->size));    offset += *(info->size);
    memcpy(info->atqa, &data[offset], 2);                offset += 2;
    info->sak[0]      = data[offset];                    offset++;
    info->ats->length = data[offset];                    offset++;
    memcpy(info->ats->data, &data[offset], info->ats->length);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

/**
 * HF14A-4 add static APDU response pair (pre-load before hw mode -e).
 * payload: cmd_len(1) cmd(n) resp_len(1) resp(m)
 * If cmd_len==0, clears all static responses.
 */
static data_frame_tx_t *cmd_processor_hf14a_4_static_resp(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 0) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    uint8_t cmd_len = data[0];
    if (cmd_len == 0) {
        nfc_tag_14a_4_clear_static_responses();
        return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
    }
    if (length < (uint16_t)(1 + cmd_len + 2)) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    /* resp_len is 2 bytes big-endian to support responses > 255 bytes */
    uint16_t resp_len = ((uint16_t)data[1 + cmd_len] << 8) | data[2 + cmd_len];
    if (length < (uint16_t)(1 + cmd_len + 2 + resp_len)) return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    nfc_tag_14a_4_add_static_response(&data[1], cmd_len, &data[3 + cmd_len], (uint8_t)resp_len);
    return data_frame_make(cmd, STATUS_SUCCESS, 0, NULL);
}

/**
 * HF14A scan keeping field alive after completion — identical to hf14a_scan
 * but registered without after_hf_reader_run so the field stays on and the
 * card remains in T=CL state for subsequent hf14a_raw APDU calls.
 */
static data_frame_tx_t *cmd_processor_hf14a_scan_keep(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    picc_14a_tag_t taginfo;
    status = pcd_14a_reader_scan_auto(&taginfo);
    if (status != STATUS_HF_TAG_OK) {
        return data_frame_make(cmd, status, 0, NULL);
    }
    uint8_t payload[1 + sizeof(taginfo.uid) + sizeof(taginfo.atqa) + sizeof(taginfo.sak) + 1 + 254];
    uint16_t offset = 0;
    payload[offset++] = taginfo.uid_len;
    memcpy(&payload[offset], taginfo.uid, taginfo.uid_len); offset += taginfo.uid_len;
    memcpy(&payload[offset], taginfo.atqa, sizeof(taginfo.atqa)); offset += sizeof(taginfo.atqa);
    payload[offset++] = taginfo.sak;
    payload[offset++] = taginfo.ats_len;
    memcpy(&payload[offset], taginfo.ats, taginfo.ats_len); offset += taginfo.ats_len;
    return data_frame_make(cmd, STATUS_HF_TAG_OK, offset, payload);
}


/**
 * HF14A-4 reader APDU — activate field, select card (with RATS), send one
 * ISO14443-4 T=CL APDU, return the response, keep field alive.
 *
 * This performs the full select+RATS+APDU sequence in a single firmware call,
 * avoiding the USB round-trip gap that would cause the card to lose power.
 *
 * payload: apdu_bytes (raw APDU, no PCB wrapping needed — added here)
 * returns: raw APDU response bytes (PCB stripped)
 */
static data_frame_tx_t *cmd_processor_hf14a_4_reader_apdu(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    if (length == 0 || length > 61) {
        return data_frame_make(cmd, STATUS_PAR_ERR, 0, NULL);
    }

    uint8_t resp_buf[64];    /* RC522 FIFO per chain block */
    uint8_t resp_chain[512]; /* reassembled chained response */
    uint16_t resp_chain_len = 0;
    uint16_t resp_bits = 0;

    /* Step 1: cycle field briefly to return card to IDLE state, then
     * do full select + RATS via scan_auto. This is needed because the card
     * may be in T=CL active state from a previous APDU exchange and won't
     * respond to REQA/WUPA until powered off. */
    pcd_14a_reader_antenna_off();
    bsp_delay_ms(5);
    pcd_14a_reader_reset();
    pcd_14a_reader_antenna_on();
    bsp_delay_ms(8);

    pcd_14a_reader_timeout_set(200);
    picc_14a_tag_t taginfo;
    status = pcd_14a_reader_scan_auto(&taginfo);
    if (status != STATUS_HF_TAG_OK) {
        pcd_14a_reader_timeout_set(DEF_COM_TIMEOUT);
        uint8_t dbg[2] = {0x01, status};
        return data_frame_make(cmd, STATUS_HF_TAG_NO, 2, dbg);
    }
    NRF_LOG_INFO("14A4_READER_APDU: scan_auto OK sak=%02x ats_len=%d",
                  taginfo.sak, taginfo.ats_len);

    /* Step 3: wrap APDU in I-block (PCB=0x02) and send */
    uint8_t frame_buf[64];
    frame_buf[0] = 0x02;  /* PCB: I-block, block_num=0, no CID, no NAD */
    memcpy(&frame_buf[1], data, length);
    crc_14a_append(frame_buf, length + 1);
    uint8_t frame_len = length + 1 + 2;

    NRF_LOG_INFO("14A4_READER_APDU: sending I-block, frame_len=%d", frame_len);

    pcd_14a_reader_timeout_set(600);
    resp_bits = 0;
    status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE,
                frame_buf, frame_len, resp_buf, &resp_bits, U8ARR_BIT_LEN(resp_buf));
    pcd_14a_reader_timeout_set(DEF_COM_TIMEOUT);

    NRF_LOG_INFO("14A4_READER_APDU: APDU transfer status=%d resp_bits=%d", status, resp_bits);

    if (status != STATUS_HF_TAG_OK || resp_bits < 8) {
        uint8_t dbg[6] = {0x03, status, (uint8_t)frame_len,
                          frame_buf[0], frame_buf[1], frame_buf[2]};
        return data_frame_make(cmd, STATUS_HF_TAG_NO, 6, dbg);
    }

    uint8_t resp_bytes = resp_bits / 8;
    if (resp_bytes < 3) {
        uint8_t dbg[2] = {0x04, resp_bytes};
        return data_frame_make(cmd, STATUS_HF_ERR_CRC, 2, dbg);
    }

    /* Verify first block CRC and begin chaining reassembly */
    uint8_t crc_calc[2];
    crc_14a_calculate(resp_buf, resp_bytes - 2, crc_calc);
    if (resp_buf[resp_bytes-2] != crc_calc[0] || resp_buf[resp_bytes-1] != crc_calc[1]) {
        return data_frame_make(cmd, STATUS_HF_ERR_CRC, resp_bytes, resp_buf);
    }

    /* Copy data portion (strip PCB + CRC), then handle chaining */
    uint8_t blk_num = 0;
    uint8_t resp_pcb = resp_buf[0];
    uint8_t dlen = resp_bytes - 3; /* subtract PCB(1) + CRC(2) */
    if (dlen > 0 && resp_chain_len + dlen < sizeof(resp_chain)) {
        memcpy(&resp_chain[resp_chain_len], &resp_buf[1], dlen);
        resp_chain_len += dlen;
    }
    blk_num ^= 1;

    /* ISO14443-4 chaining: PCB bit5 (0x20) set means more blocks follow */
    while (resp_pcb & 0x20) {
        uint8_t rack = 0xA2 | (blk_num & 0x01); /* R(ACK) */
        uint8_t rack_frame[3];
        rack_frame[0] = rack;
        crc_14a_append(rack_frame, 1);
        resp_bits = 0;
        pcd_14a_reader_timeout_set(600);
        status = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE,
                    rack_frame, 3, resp_buf, &resp_bits, U8ARR_BIT_LEN(resp_buf));
        pcd_14a_reader_timeout_set(DEF_COM_TIMEOUT);
        if (status != STATUS_HF_TAG_OK || resp_bits < 24) break;
        resp_bytes = resp_bits / 8;
        crc_14a_calculate(resp_buf, resp_bytes - 2, crc_calc);
        if (resp_buf[resp_bytes-2] != crc_calc[0] || resp_buf[resp_bytes-1] != crc_calc[1]) break;
        resp_pcb = resp_buf[0];
        dlen = resp_bytes - 3;
        if (dlen > 0 && resp_chain_len + dlen < sizeof(resp_chain)) {
            memcpy(&resp_chain[resp_chain_len], &resp_buf[1], dlen);
            resp_chain_len += dlen;
        }
        blk_num ^= 1;
    }

    return data_frame_make(cmd, STATUS_HF_TAG_OK, resp_chain_len, resp_chain);
}

/**
 * HF14A-4 EMV scan — complete EMV card read in a single firmware call.
 *
 * Performs: field cycle → scan_auto (select+RATS) → PPSE → SELECT AID →
 * GPO → READ RECORDs, all without returning to the host between APDUs.
 *
 * Response format (packed, little-endian lengths):
 *   tag_info:    uid_len(1) uid(n) atqa(2) sak(1) ats_len(1) ats(m)
 *   num_apdus(1)
 *   for each APDU pair:
 *     cmd_len(1) cmd(n) resp_len(2 LE) resp(m)
 *
 * Returns STATUS_HF_TAG_NO if card not found.
 * Returns STATUS_HF_TAG_OK with packed data on success (partial data if
 * some APDUs fail — num_apdus reflects how many completed).
 */
static data_frame_tx_t *cmd_processor_hf14a_4_emv_scan(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    static uint8_t out[NETDATA_MAX_DATA_LENGTH];
    uint16_t out_len = 0;

    /* ---- helpers -------------------------------------------------- */
    static uint8_t  abuf[64];   /* TX frame: PCB + APDU + CRC */
    static uint8_t  rbuf[64];   /* single-frame receive buffer (RC522 FIFO = 64 bytes) */
    static uint8_t  chain_buf[512]; /* reassembled chained response */
    uint16_t rbits;
    uint8_t  blk = 0;  /* alternating block number */

    /* Send one I-block APDU, handling ISO14443-4 response chaining.
     * The RC522 FIFO is 64 bytes. If the card chains its response
     * (PCB bit4=1), we send R(ACK) blocks and reassemble here.
     * Returns pointer into chain_buf, sets *rlen_ptr to total length. */
    #define SEND_APDU(apdu_ptr, apdu_sz, rdata_ptr, rlen_ptr) ({         bool _ok = false;         uint8_t _pcb = 0x02 | (blk & 0x01);         abuf[0] = _pcb;         memcpy(&abuf[1], (apdu_ptr), (apdu_sz));         rbits = 0;         uint8_t _st = pcd_14a_reader_raw_cmd(             false, true, true, false, true, false,             600, ((apdu_sz) + 1) * 8, abuf,             rbuf, &rbits, sizeof(rbuf) * 8);         if (_st == STATUS_HF_TAG_OK && rbits > 0) {             uint16_t _rb = rbits;  /* raw_cmd checkCrc=false returns byte count */             /* Verify and strip CRC manually (checkCrc=false above) */             if (_rb >= 3) {                 uint8_t _crc[2]; crc_14a_calculate(rbuf, _rb - 2, _crc);                 if (rbuf[_rb-2] == _crc[0] && rbuf[_rb-1] == _crc[1]) {                     blk ^= 1;                     uint16_t _chain_len = 0;                     uint8_t _resp_pcb = rbuf[0];                     /* Copy data portion (strip PCB and CRC) */                     uint8_t _dlen = _rb - 3;                     if (_dlen > 0 && _chain_len + _dlen < sizeof(chain_buf)) {                         memcpy(&chain_buf[_chain_len], &rbuf[1], _dlen);                         _chain_len += _dlen;                     }                     /* Handle chaining: PCB bit5=1 (b6 in ISO14443-4) means more data */                     while (_resp_pcb & 0x20) {                         /* Send R(ACK) to request next block */                         uint8_t _rack = 0xA2 | (blk & 0x01);                         abuf[0] = _rack;                         crc_14a_append(abuf, 1);                         rbits = 0;                         _st = pcd_14a_reader_bytes_transfer(PCD_TRANSCEIVE,                             abuf, 3, rbuf, &rbits, sizeof(rbuf) * 8);                         if (_st != STATUS_HF_TAG_OK || rbits < 24) break;                         _rb = rbits / 8;  /* bytes_transfer returns bits */                         crc_14a_calculate(rbuf, _rb - 2, _crc);                         if (rbuf[_rb-2] != _crc[0] || rbuf[_rb-1] != _crc[1]) break;                         blk ^= 1;                         _resp_pcb = rbuf[0];                         _dlen = _rb - 3;                         if (_dlen > 0 && _chain_len + _dlen < sizeof(chain_buf)) {                             memcpy(&chain_buf[_chain_len], &rbuf[1], _dlen);                             _chain_len += _dlen;                         }                     }                     *(rdata_ptr) = chain_buf;                     *(rlen_ptr)  = (_chain_len < sizeof(chain_buf) ? _chain_len : (uint16_t)(sizeof(chain_buf) - 1));                     _ok = true;                 }             }         }         _ok;     })

    /* Append a cmd+resp pair to out buffer */
    #define APPEND_PAIR(cmd_ptr, cmd_sz, resp_ptr, resp_sz) do {         if (out_len + 1 + (cmd_sz) + 2 + (resp_sz) < NETDATA_MAX_DATA_LENGTH) {             out[out_len++] = (uint8_t)(cmd_sz);             memcpy(&out[out_len], (cmd_ptr), (cmd_sz)); out_len += (cmd_sz);             out[out_len++] = (uint8_t)((resp_sz) & 0xFF);             out[out_len++] = (uint8_t)((resp_sz) >> 8);             memcpy(&out[out_len], (resp_ptr), (resp_sz)); out_len += (resp_sz);         }     } while(0)

    /* ---- Step 1: scan_auto ----------------------------------------- */
    bsp_delay_ms(10);
    static picc_14a_tag_t tag;
    memset(&tag, 0, sizeof(tag));
    status = pcd_14a_reader_scan_auto(&tag);
    if (status != STATUS_HF_TAG_OK) {
        bsp_delay_ms(20);
        memset(&tag, 0, sizeof(tag));
        status = pcd_14a_reader_scan_auto(&tag);
        if (status != STATUS_HF_TAG_OK) {
            return data_frame_make(cmd, STATUS_HF_TAG_NO, 0, NULL);
        }
    }

    /* After scan_auto completes RATS, give the RC522 time to settle.
     * The hf14a_scan_keep + hf14a_raw path works because the USB round-trip
     * (~2ms) gives the RC522 time to exit its post-receive state before
     * the next transceive. Replicate that delay here. */
    bsp_delay_ms(5);

    /* ---- Clear RC522 stale state after RATS ----------------------- */
    /* After scan_auto+RATS, CommandReg=0x0C (PCD_TRANSCEIVE) and
     * ComIrqReg=0x64 (RxIRq+TxIRq+b6 set). bytes_transfer's wait loop
     * reads ComIrqReg immediately after StartSend — if RxIRq is already
     * set it exits before the PPSE frame is even transmitted.
     *
     * Fix sequence:
     * 1. Idle the RC522 — stops active TRANSCEIVE state
     * 2. Wait for CommandReg to confirm idle (RC522 state machine settles)
     * 3. Clear all ComIrqReg interrupt flags
     * 4. Flush FIFO and clear StartSend bit */
    write_register_single(CommandReg, PCD_IDLE);
    /* Spin until CommandReg confirms idle (usually immediate) */
    {
        uint16_t _w = 0;
        while ((read_register_single(CommandReg) & 0x0F) != PCD_IDLE && _w++ < 1000);
    }
    write_register_single(ComIrqReg,  0x7F);          /* clear ALL IRQ flags */
    set_register_mask(FIFOLevelReg,   0x80);          /* flush FIFO */
    clear_register_mask(BitFramingReg, 0x80);         /* clear StartSend */

    /* ---- Pack tag info ------------------------------------------- */
    out[out_len++] = tag.uid_len;
    memcpy(&out[out_len], tag.uid, tag.uid_len); out_len += tag.uid_len;
    memcpy(&out[out_len], tag.atqa, 2); out_len += 2;
    out[out_len++] = tag.sak;
    out[out_len++] = tag.ats_len;
    memcpy(&out[out_len], tag.ats, tag.ats_len); out_len += tag.ats_len;

    /* Placeholder for num_apdus — fill in at end */
    uint16_t num_apdus_offset = out_len;
    out[out_len++] = 0;
    uint8_t num_apdus = 0;

    pcd_14a_reader_timeout_set(600);

    /* ---- Step 2: SELECT PPSE ------------------------------------- */
    static const uint8_t ppse_cmd[] = {
        0x00, 0xA4, 0x04, 0x00, 0x0E,
        0x32, 0x50, 0x41, 0x59, 0x2E, 0x53, 0x59, 0x53, 0x2E,
        0x44, 0x44, 0x46, 0x30, 0x31, 0x00
    };
    uint8_t *ppse_resp = NULL; uint16_t ppse_rlen = 0;
    {
        bool _ppse_ok = SEND_APDU(ppse_cmd, sizeof(ppse_cmd), &ppse_resp, &ppse_rlen);
        if (!_ppse_ok) {
            goto done;
        }
    }
    APPEND_PAIR(ppse_cmd, sizeof(ppse_cmd), ppse_resp, ppse_rlen);
    num_apdus++;

    /* ---- Extract first AID from PPSE ----------------------------- */
    uint8_t aid[16]; uint8_t aid_len = 0;
    for (uint8_t i = 0; i + 1 < ppse_rlen; i++) {
        if (ppse_resp[i] == 0x4F && ppse_resp[i+1] > 0 && ppse_resp[i+1] <= 16) {
            aid_len = ppse_resp[i+1];
            memcpy(aid, &ppse_resp[i+2], aid_len);
            break;
        }
    }
    if (aid_len == 0) goto done;

    /* ---- Step 3: SELECT AID -------------------------------------- */
    uint8_t sel_cmd[32];
    uint8_t sel_len = 0;
    sel_cmd[sel_len++] = 0x00; sel_cmd[sel_len++] = 0xA4;
    sel_cmd[sel_len++] = 0x04; sel_cmd[sel_len++] = 0x00;
    sel_cmd[sel_len++] = aid_len;
    memcpy(&sel_cmd[sel_len], aid, aid_len); sel_len += aid_len;
    sel_cmd[sel_len++] = 0x00;

    uint8_t *sel_resp; uint16_t sel_rlen;
    if (!SEND_APDU(sel_cmd, sel_len, &sel_resp, &sel_rlen)) goto done;
    APPEND_PAIR(sel_cmd, sel_len, sel_resp, sel_rlen);
    num_apdus++;

    /* ---- Step 4: GPO -------------------------------------------- */
    static const uint8_t gpo_cmd[] = {0x80, 0xA8, 0x00, 0x00, 0x02, 0x83, 0x00, 0x00};
    uint8_t *gpo_resp; uint16_t gpo_rlen;
    if (!SEND_APDU(gpo_cmd, sizeof(gpo_cmd), &gpo_resp, &gpo_rlen)) goto done;
    APPEND_PAIR(gpo_cmd, sizeof(gpo_cmd), gpo_resp, gpo_rlen);
    num_apdus++;

    /* ---- Step 5: parse AFL and READ RECORDs --------------------- */
    /* Find AFL in GPO response (tag 0x94 in format 2, or bytes 3+ in format 1) */
    uint8_t *afl = NULL; uint8_t afl_len = 0;
    if (gpo_rlen > 0 && gpo_resp[0] == 0x77) {
        /* Format 2: search for tag 94 */
        for (uint8_t i = 2; i + 1 < gpo_rlen; ) {
            uint8_t t = gpo_resp[i]; uint8_t l = gpo_resp[i+1];
            if (t == 0x94) { afl = &gpo_resp[i+2]; afl_len = l; break; }
            i += 2 + l;
        }
    } else if (gpo_rlen > 3 && gpo_resp[0] == 0x80) {
        /* Format 1: skip tag(1)+len(1)+AIP(2) */
        afl = &gpo_resp[3]; afl_len = gpo_rlen - 3 - 2; /* -2 for SW */
    }

    /* READ each record */
    for (uint8_t a = 0; a + 3 < afl_len; a += 4) {
        uint8_t sfi    = (afl[a] >> 3) & 0x1F;
        uint8_t rec_s  = afl[a+1];
        uint8_t rec_e  = afl[a+2];
        if (sfi == 0 || rec_s > rec_e) continue;
        for (uint8_t r = rec_s; r <= rec_e; r++) {
            uint8_t rr_cmd[5] = {0x00, 0xB2, r, (uint8_t)((sfi << 3) | 4), 0x00};
            uint8_t *rr_resp; uint16_t rr_rlen;
            if (!SEND_APDU(rr_cmd, 5, &rr_resp, &rr_rlen)) {
                /* RC522 FIFO is 64 bytes — records > 61 bytes fail.
                 * Skip silently rather than aborting the whole scan. */
                NRF_LOG_INFO("14A4_EMV_SCAN: READ RECORD SFI=%d rec=%d failed (response too large?)", sfi, r);
                continue;
            }
            APPEND_PAIR(rr_cmd, 5, rr_resp, rr_rlen);
            num_apdus++;
        }
    }

done:
    pcd_14a_reader_timeout_set(DEF_COM_TIMEOUT);
    out[num_apdus_offset] = num_apdus;
    /* Return HF_TAG_OK even with 0 APDUs so Python can see tag info */
    return data_frame_make(cmd, STATUS_HF_TAG_OK, out_len, out);
}


static data_frame_tx_t *cmd_processor_hf14a_4_debug_counters(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    uint8_t buf[4];
    nfc_tag_14a_4_get_debug_counters(&buf[0], &buf[1], &buf[2], &buf[3]);
    return data_frame_make(cmd, STATUS_SUCCESS, 4, buf);
}

static cmd_data_map_t m_data_cmd_map[] = {
    {    DATA_CMD_GET_APP_VERSION,              NULL,                        cmd_processor_get_app_version,               NULL                   },
    {    DATA_CMD_CHANGE_DEVICE_MODE,           NULL,                        cmd_processor_change_device_mode,            NULL                   },
    {    DATA_CMD_GET_DEVICE_MODE,              NULL,                        cmd_processor_get_device_mode,               NULL                   },
    {    DATA_CMD_SET_ACTIVE_SLOT,              NULL,                        cmd_processor_set_active_slot,               NULL                   },
    {    DATA_CMD_SET_SLOT_TAG_TYPE,            NULL,                        cmd_processor_set_slot_tag_type,             NULL                   },
    {    DATA_CMD_SET_SLOT_DATA_DEFAULT,        NULL,                        cmd_processor_set_slot_data_default,         NULL                   },
    {    DATA_CMD_SET_SLOT_ENABLE,              NULL,                        cmd_processor_set_slot_enable,               NULL                   },
    {    DATA_CMD_SET_SLOT_TAG_NICK,            NULL,                        cmd_processor_set_slot_tag_nick,             NULL                   },
    {    DATA_CMD_GET_SLOT_TAG_NICK,            NULL,                        cmd_processor_get_slot_tag_nick,             NULL                   },
    {    DATA_CMD_SLOT_DATA_CONFIG_SAVE,        NULL,                        cmd_processor_slot_data_config_save,         NULL                   },
    {    DATA_CMD_ENTER_BOOTLOADER,             NULL,                        cmd_processor_enter_bootloader,              NULL                   },
    {    DATA_CMD_GET_DEVICE_CHIP_ID,           NULL,                        cmd_processor_get_device_chip_id,            NULL                   },
    {    DATA_CMD_GET_DEVICE_ADDRESS,           NULL,                        cmd_processor_get_device_address,            NULL                   },
    {    DATA_CMD_SAVE_SETTINGS,                NULL,                        cmd_processor_save_settings,                 NULL                   },
    {    DATA_CMD_RESET_SETTINGS,               NULL,                        cmd_processor_reset_settings,                NULL                   },
    {    DATA_CMD_SET_ANIMATION_MODE,           NULL,                        cmd_processor_set_animation_mode,            NULL                   },
    {    DATA_CMD_GET_ANIMATION_MODE,           NULL,                        cmd_processor_get_animation_mode,            NULL                   },
    {    DATA_CMD_GET_GIT_VERSION,              NULL,                        cmd_processor_get_git_version,               NULL                   },
    {    DATA_CMD_GET_ACTIVE_SLOT,              NULL,                        cmd_processor_get_active_slot,               NULL                   },
    {    DATA_CMD_GET_SLOT_INFO,                NULL,                        cmd_processor_get_slot_info,                 NULL                   },
    {    DATA_CMD_WIPE_FDS,                     NULL,                        cmd_processor_wipe_fds,                      NULL                   },
    {    DATA_CMD_DELETE_SLOT_TAG_NICK,         NULL,                        cmd_processor_delete_slot_tag_nick,          NULL                   },
    {    DATA_CMD_GET_ENABLED_SLOTS,            NULL,                        cmd_processor_get_enabled_slots,             NULL                   },
    {    DATA_CMD_DELETE_SLOT_SENSE_TYPE,       NULL,                        cmd_processor_delete_slot_sense_type,        NULL                   },
    {    DATA_CMD_GET_BATTERY_INFO,             NULL,                        cmd_processor_get_battery_info,              NULL                   },
    {    DATA_CMD_GET_BUTTON_PRESS_CONFIG,      NULL,                        cmd_processor_get_button_press_config,       NULL                   },
    {    DATA_CMD_SET_BUTTON_PRESS_CONFIG,      NULL,                        cmd_processor_set_button_press_config,       NULL                   },
    {    DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG, NULL,                        cmd_processor_get_long_button_press_config,  NULL                   },
    {    DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG, NULL,                        cmd_processor_set_long_button_press_config,  NULL                   },
    {    DATA_CMD_GET_BLE_PAIRING_KEY,          NULL,                        cmd_processor_get_ble_connect_key,           NULL                   },
    {    DATA_CMD_SET_BLE_PAIRING_KEY,          NULL,                        cmd_processor_set_ble_connect_key,           NULL                   },
    {    DATA_CMD_DELETE_ALL_BLE_BONDS,         NULL,                        cmd_processor_delete_all_ble_bonds,          NULL                   },
    {    DATA_CMD_GET_DEVICE_MODEL,             NULL,                        cmd_processor_get_device_model,              NULL                   },
    {    DATA_CMD_GET_DEVICE_SETTINGS,          NULL,                        cmd_processor_get_device_settings,           NULL                   },
    {    DATA_CMD_GET_DEVICE_CAPABILITIES,      NULL,                        cmd_processor_get_device_capabilities,       NULL                   },
    {    DATA_CMD_GET_BLE_PAIRING_ENABLE,       NULL,                        cmd_processor_get_ble_pairing_enable,        NULL                   },
    {    DATA_CMD_SET_BLE_PAIRING_ENABLE,       NULL,                        cmd_processor_set_ble_pairing_enable,        NULL                   },
    {    DATA_CMD_GET_ALL_SLOT_NICKS,           NULL,                        cmd_processor_get_all_slot_nicks,            NULL                   },

#if defined(PROJECT_CHAMELEON_ULTRA)

    {    DATA_CMD_HF14A_SCAN,                   before_hf_reader_run,        cmd_processor_hf14a_scan,                    after_hf_reader_run    },
    {    DATA_CMD_MF1_DETECT_SUPPORT,           before_hf_reader_run,        cmd_processor_mf1_detect_support,            after_hf_reader_run    },
    {    DATA_CMD_MF1_DETECT_PRNG,              before_hf_reader_run,        cmd_processor_mf1_detect_prng,               after_hf_reader_run    },
    {    DATA_CMD_MF1_STATIC_NESTED_ACQUIRE,    before_hf_reader_run,        cmd_processor_mf1_static_nested_acquire,     after_hf_reader_run    },
    {    DATA_CMD_MF1_DARKSIDE_ACQUIRE,         before_hf_reader_run,        cmd_processor_mf1_darkside_acquire,          after_hf_reader_run    },
    {    DATA_CMD_MF1_DETECT_NT_DIST,           before_hf_reader_run,        cmd_processor_mf1_detect_nt_dist,            after_hf_reader_run    },
    {    DATA_CMD_MF1_NESTED_ACQUIRE,           before_hf_reader_run,        cmd_processor_mf1_nested_acquire,            after_hf_reader_run    },
    {    DATA_CMD_MF1_ENC_NESTED_ACQUIRE,       before_hf_reader_run,        cmd_processor_mf1_enc_nested_acquire,        after_hf_reader_run    },

    {    DATA_CMD_MF1_AUTH_ONE_KEY_BLOCK,       before_hf_reader_run,        cmd_processor_mf1_auth_one_key_block,        after_hf_reader_run    },
    {    DATA_CMD_MF1_READ_ONE_BLOCK,           before_hf_reader_run,        cmd_processor_mf1_read_one_block,            after_hf_reader_run    },
    {    DATA_CMD_MF1_WRITE_ONE_BLOCK,          before_hf_reader_run,        cmd_processor_mf1_write_one_block,           after_hf_reader_run    },
    {    DATA_CMD_HF14A_RAW,                    before_reader_run,           cmd_processor_hf14a_raw,                     NULL                   },
    {    DATA_CMD_MF1_MANIPULATE_VALUE_BLOCK,   before_hf_reader_run,        cmd_processor_mf1_manipulate_value_block,    after_hf_reader_run    },
    {    DATA_CMD_MF1_CHECK_KEYS_OF_SECTORS,    before_hf_reader_run,        cmd_processor_mf1_check_keys_of_sectors,     after_hf_reader_run    },
    {    DATA_CMD_MF1_HARDNESTED_ACQUIRE,       before_hf_reader_run,        cmd_processor_mf1_hardnested_nonces_acquire, after_hf_reader_run    },
    {    DATA_CMD_MF1_CHECK_KEYS_ON_BLOCK,      before_hf_reader_run,        cmd_processor_mf1_check_keys_on_block,       after_hf_reader_run    },

    {    DATA_CMD_EM410X_SCAN,                  before_reader_run,           cmd_processor_em410x_scan,                   NULL                   },
    {    DATA_CMD_EM410X_WRITE_TO_T55XX,        before_reader_run,           cmd_processor_em410x_write_to_t55xx,         NULL                   },
    {    DATA_CMD_EM410X_ELECTRA_WRITE_TO_T55XX,before_reader_run,           cmd_processor_em410x_electra_write_to_t55xx, NULL                   },
    {    DATA_CMD_HIDPROX_SCAN,                 before_reader_run,           cmd_processor_hidprox_scan,                  NULL                   },
    {    DATA_CMD_HIDPROX_WRITE_TO_T55XX,       before_reader_run,           cmd_processor_hidprox_write_to_t55xx,        NULL                   },
    {    DATA_CMD_VIKING_SCAN,                  before_reader_run,           cmd_processor_viking_scan,                   NULL                   },
    {    DATA_CMD_VIKING_WRITE_TO_T55XX,        before_reader_run,           cmd_processor_viking_write_to_t55xx,         NULL                   },
    {    DATA_CMD_IOPROX_SCAN,                  before_reader_run,           cmd_processor_ioprox_scan,                   NULL                   },
    {    DATA_CMD_IOPROX_WRITE_TO_T55XX,        before_reader_run,           cmd_processor_ioprox_write_to_t55xx,         NULL                   },
    {    DATA_CMD_ADC_GENERIC_READ,             before_reader_run,           cmd_processor_generic_read,                  NULL                   },

    {    DATA_CMD_HF14A_SET_FIELD_ON,           before_reader_run,           cmd_processor_hf14a_set_field_on,            NULL                   },
    {    DATA_CMD_HF14A_SET_FIELD_OFF,          before_reader_run,           cmd_processor_hf14a_set_field_off,           NULL                   },

    {    DATA_CMD_HF14A_GET_CONFIG,             NULL,                        cmd_processor_hf14a_get_config,              NULL                   },
    {    DATA_CMD_HF14A_SET_CONFIG,             NULL,                        cmd_processor_hf14a_set_config,              NULL                   },

    {    DATA_CMD_IOPROX_DECODE_RAW,            NULL,                        cmd_processor_ioprox_decode_raw,             NULL                   },
    {    DATA_CMD_IOPROX_COMPOSE_ID,            NULL,                        cmd_processor_ioprox_compose_id,             NULL                   },
    {    DATA_CMD_EM4X05_SCAN,                  before_reader_run,           cmd_processor_em4x05_scan,                   NULL                   },
    {    DATA_CMD_LF_SNIFF,                     before_reader_run,           cmd_processor_lf_sniff,                      NULL                   },
    {    DATA_CMD_HF14A_SNIFF,                  NULL,                        cmd_processor_hf14a_sniff,                   NULL                   },

#endif

    {    DATA_CMD_HF14A_GET_ANTI_COLL_DATA,     NULL,                        cmd_processor_hf14a_get_anti_coll_data,      NULL                   },
    {    DATA_CMD_HF14A_SET_ANTI_COLL_DATA,     NULL,                        cmd_processor_hf14a_set_anti_coll_data,      NULL                   },

    {    DATA_CMD_MF1_WRITE_EMU_BLOCK_DATA,     NULL,                        cmd_processor_mf1_write_emu_block_data,      NULL                   },
    {    DATA_CMD_MF1_SET_DETECTION_ENABLE,     NULL,                        cmd_processor_mf1_set_detection_enable,      NULL                   },
    {    DATA_CMD_MF1_GET_DETECTION_COUNT,      NULL,                        cmd_processor_mf1_get_detection_count,       NULL                   },
    {    DATA_CMD_MF1_GET_DETECTION_LOG,        NULL,                        cmd_processor_mf1_get_detection_log,         NULL                   },
    {    DATA_CMD_MF1_GET_DETECTION_ENABLE,     NULL,                        cmd_processor_mf1_get_detection_enable,      NULL                   },
    {    DATA_CMD_MF1_READ_EMU_BLOCK_DATA,      NULL,                        cmd_processor_mf1_read_emu_block_data,       NULL                   },
    {    DATA_CMD_MF1_GET_EMULATOR_CONFIG,      NULL,                        cmd_processor_mf1_get_emulator_config,       NULL                   },
    {    DATA_CMD_MF1_GET_GEN1A_MODE,           NULL,                        cmd_processor_mf1_get_gen1a_mode,            NULL                   },
    {    DATA_CMD_MF1_SET_GEN1A_MODE,           NULL,                        cmd_processor_mf1_set_gen1a_mode,            NULL                   },
    {    DATA_CMD_MF1_GET_GEN2_MODE,            NULL,                        cmd_processor_mf1_get_gen2_mode,             NULL                   },
    {    DATA_CMD_MF1_SET_GEN2_MODE,            NULL,                        cmd_processor_mf1_set_gen2_mode,             NULL                   },
    {    DATA_CMD_MF1_GET_BLOCK_ANTI_COLL_MODE, NULL,                        cmd_processor_mf1_get_block_anti_coll_mode,  NULL                   },
    {    DATA_CMD_MF1_SET_BLOCK_ANTI_COLL_MODE, NULL,                        cmd_processor_mf1_set_block_anti_coll_mode,  NULL                   },
    {    DATA_CMD_MF1_GET_WRITE_MODE,           NULL,                        cmd_processor_mf1_get_write_mode,            NULL                   },
    {    DATA_CMD_MF1_SET_WRITE_MODE,           NULL,                        cmd_processor_mf1_set_write_mode,            NULL                   },
    {    DATA_CMD_MF1_GET_FIELD_OFF_DO_RESET,   NULL,                        cmd_processor_mf1_get_field_off_do_reset,    NULL                   },
    {    DATA_CMD_MF1_SET_FIELD_OFF_DO_RESET,   NULL,                        cmd_processor_mf1_set_field_off_do_reset,    NULL                   },

    {    DATA_CMD_MF0_NTAG_GET_UID_MAGIC_MODE,    NULL,                      cmd_processor_mf0_ntag_get_uid_mode,         NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_UID_MAGIC_MODE,    NULL,                      cmd_processor_mf0_ntag_set_uid_mode,         NULL                   },
    {    DATA_CMD_MF0_NTAG_READ_EMU_PAGE_DATA,    NULL,                      cmd_processor_mf0_ntag_read_emu_page_data,   NULL                   },
    {    DATA_CMD_MF0_NTAG_WRITE_EMU_PAGE_DATA,   NULL,                      cmd_processor_mf0_ntag_write_emu_page_data,  NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_VERSION_DATA,      NULL,                      cmd_processor_mf0_ntag_get_version_data,     NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_VERSION_DATA,      NULL,                      cmd_processor_mf0_ntag_set_version_data,     NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_SIGNATURE_DATA,    NULL,                      cmd_processor_mf0_ntag_get_signature_data,   NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_SIGNATURE_DATA,    NULL,                      cmd_processor_mf0_ntag_set_signature_data,   NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_COUNTER_DATA,      NULL,                      cmd_processor_mf0_ntag_get_counter_data,     NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_COUNTER_DATA,      NULL,                      cmd_processor_mf0_ntag_set_counter_data,     NULL                   },
    {    DATA_CMD_MF0_NTAG_RESET_AUTH_CNT,        NULL,                      cmd_processor_mf0_ntag_reset_auth_cnt,       NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_PAGE_COUNT,        NULL,                      cmd_processor_mf0_ntag_get_emu_page_count,   NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_WRITE_MODE,        NULL,                      cmd_processor_mf0_ntag_get_write_mode,       NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_WRITE_MODE,        NULL,                      cmd_processor_mf0_ntag_set_write_mode,       NULL                   },
    {    DATA_CMD_MF0_NTAG_SET_DETECTION_ENABLE,  NULL,                      cmd_processor_mf0_ntag_set_detection_enable, NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_DETECTION_COUNT,   NULL,                      cmd_processor_mf0_ntag_get_detection_count,  NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_DETECTION_LOG,     NULL,                      cmd_processor_mf0_ntag_get_detection_log,    NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_DETECTION_ENABLE,  NULL,                      cmd_processor_mf0_ntag_get_detection_enable, NULL                   },
    {    DATA_CMD_MF0_NTAG_GET_EMULATOR_CONFIG,   NULL,                      cmd_processor_mf0_get_emulator_config,       NULL                   },

    {    DATA_CMD_EM410X_SET_EMU_ID,              NULL,                      cmd_processor_em410x_set_emu_id,             NULL                   },
    {    DATA_CMD_EM410X_GET_EMU_ID,              NULL,                      cmd_processor_em410x_get_emu_id,             NULL                   },
    {    DATA_CMD_HIDPROX_SET_EMU_ID,             NULL,                      cmd_processor_hidprox_set_emu_id,            NULL                   },
    {    DATA_CMD_HIDPROX_GET_EMU_ID,             NULL,                      cmd_processor_hidprox_get_emu_id,            NULL                   },
    {    DATA_CMD_IOPROX_SET_EMU_ID,              NULL,                      cmd_processor_ioprox_set_emu_id,             NULL                   },
    {    DATA_CMD_IOPROX_GET_EMU_ID,              NULL,                      cmd_processor_ioprox_get_emu_id,             NULL                   },  
    {    DATA_CMD_VIKING_SET_EMU_ID,              NULL,                      cmd_processor_viking_set_emu_id,             NULL                   },
    {    DATA_CMD_VIKING_GET_EMU_ID,              NULL,                      cmd_processor_viking_get_emu_id,             NULL                   },
    /* ISO14443-4 T=CL emulation */
    {    DATA_CMD_HF14A_4_APDU_RECV,              NULL,                        cmd_processor_hf14a_4_apdu_recv,             NULL                   },
    {    DATA_CMD_HF14A_4_APDU_SEND,              NULL,                        cmd_processor_hf14a_4_apdu_send,             NULL                   },
    {    DATA_CMD_HF14A_4_SET_ANTI_COLL,          NULL,                        cmd_processor_hf14a_4_set_anti_coll,         NULL                   },
    {    DATA_CMD_HF14A_4_STATIC_RESP,            NULL,                        cmd_processor_hf14a_4_static_resp,           NULL                   },
    {    DATA_CMD_HF14A_4_READER_APDU,            before_hf_reader_run,        cmd_processor_hf14a_4_reader_apdu,           NULL                   },
    {    DATA_CMD_HF14A_4_EMV_SCAN,               before_hf_reader_run,        cmd_processor_hf14a_4_emv_scan,              NULL                   },
    {    6010,                                     NULL,                        cmd_processor_hf14a_4_debug_counters,        NULL                   },
    /* HF14A scan keeping field alive */
    {    DATA_CMD_HF14A_SCAN_KEEP,                before_hf_reader_run,        cmd_processor_hf14a_scan_keep,               NULL                   },
};

data_frame_tx_t *cmd_processor_get_device_capabilities(uint16_t cmd, uint16_t status, uint16_t length, uint8_t *data) {
    size_t count = ARRAYLEN(m_data_cmd_map);
    uint16_t commands[count];
    memset(commands, 0, count * sizeof(uint16_t));

    for (size_t i = 0; i < count; i++) {
        commands[i] = U16HTONS(m_data_cmd_map[i].cmd);
    }

    return data_frame_make(cmd, STATUS_SUCCESS, count * sizeof(uint16_t), (uint8_t *)commands);
}

/**
 * @brief Auto select source to response
 *
 * @param resp data
 */
static void auto_response_data(data_frame_tx_t *resp) {
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
