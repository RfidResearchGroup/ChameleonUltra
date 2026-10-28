#include "tag_emulation.h"

#include "crc_utils.h"
#include "fds_ids.h"
#include "fds_util.h"
#include "lf_tag_em.h"
#include "nfc_14a.h"
#include "nfc_mf0_ntag.h"
#include "nfc_mf1.h"
#include "rfid_main.h"
#if defined(PROJECT_CHAMELEON_ULTRA)
#include "sl1_profile.h"
#endif
#include "nfc_14a_4.h"
#include "nfc_seos.h"
#include "rgb_marquee.h"
#include "tag_persistence.h"

#define NRF_LOG_MODULE_NAME tag_emu
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

/*
 * A card slot can simulate up to two cards at the same time, one ID 125kHz EM410X, and one IC 13.56MHz 14A.(May be able to support more in the future)
 * When starting, you should start the startup listener on demand (there is no emulated card when there is no data, but you need to monitor the state on demand)
 * If the retrieved card slot configuration has a specified type of card, then loading the specified type of data should be carried out, and the necessary parameters of initialization should be performed.
 * When the on-field entry is detected, in addition to the relevant LED, you also need to start the emulation card according to whether the current data is loaded.
 * In the emulation card, all operations should be carried out based on the data loaded in RAM. After the analog card is over, the modified data should be preserved to Flash
 *
 * ......
 */

// Is the logo in the analog card?
bool g_is_tag_emulating = false;

static tag_specific_type_t tag_specific_type_old2new_lf_values[][2] = {TAG_SPECIFIC_TYPE_OLD2NEW_LF_VALUES};
static tag_specific_type_t tag_specific_type_old2new_hf_values[][2] = {TAG_SPECIFIC_TYPE_OLD2NEW_HF_VALUES};
static tag_specific_type_t tag_specific_type_lf_values[] = {TAG_SPECIFIC_TYPE_LF_VALUES};
static tag_specific_type_t tag_specific_type_hf_values[] = {TAG_SPECIFIC_TYPE_HF_VALUES};

bool is_tag_specific_type_valid(tag_specific_type_t tag_type) {
    bool valid = false;
    for (uint16_t i = 0; i < ARRAYLEN(tag_specific_type_lf_values); i++) {
        valid |= (tag_type == tag_specific_type_lf_values[i]);
    }
    for (uint16_t i = 0; i < ARRAYLEN(tag_specific_type_hf_values); i++) {
        valid |= (tag_type == tag_specific_type_hf_values[i]);
    }
    return valid;
}
// **********************  Specific parameters start **********************

/**
 * Tag data stored in flash. Total length must be aligned by 4 bytes (whole words).
 */
static uint8_t m_tag_data_buffer_lf[20];  // LF card data buffer
static uint16_t m_tag_data_lf_crc;
static tag_data_buffer_t m_tag_data_lf = {sizeof(m_tag_data_buffer_lf), m_tag_data_buffer_lf, &m_tag_data_lf_crc};

static uint8_t m_tag_data_buffer_hf[4500];  // HF card data buffer
static uint16_t m_tag_data_hf_crc;
static tag_data_buffer_t m_tag_data_hf = {sizeof(m_tag_data_buffer_hf), m_tag_data_buffer_hf, &m_tag_data_hf_crc};

/**
 * Eight card slots, each card slot has its own unique configuration
 */
#define TAG_SLOT_CONFIG_DEFAULTS { \
    .version = TAG_SLOT_CONFIG_CURRENT_VERSION, \
    .active_slot = 0, \
    .slots = { \
        { .enabled_hf = true,  .enabled_lf = true,  .tag_hf = TAG_TYPE_MIFARE_1024, .tag_lf = TAG_TYPE_EM410X,    }, \
        { .enabled_hf = true,  .enabled_lf = false, .tag_hf = TAG_TYPE_MF0ICU1,     .tag_lf = TAG_TYPE_UNDEFINED, }, \
        { .enabled_hf = false, .enabled_lf = true,  .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_EM410X,    }, \
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, }, \
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, }, \
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, }, \
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, }, \
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, }, \
    }, \
}
static const tag_slot_config_t m_default_slot_config = TAG_SLOT_CONFIG_DEFAULTS;
static tag_slot_config_t slotConfig ALIGN_U32 = TAG_SLOT_CONFIG_DEFAULTS;
#undef TAG_SLOT_CONFIG_DEFAULTS
// The card slot configuration unique CRC, once the slot configuration changes, can be checked by CRC
static uint16_t m_slot_config_crc;
static bool m_profile_reload_in_progress;
bool tag_emulation_profile_hf_quarantined(uint8_t slot) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    return slot >= TAG_MAX_SLOT_NUM || sl1_profile_hf_incomplete(slot);
#else
    (void)slot;
    return false;
#endif
}

// ********************** Specific parameter ends **********************

/**
 * The data of the tag is loaded to the RAM and the mapping table of the operation of the regulating notification,
 * The mapping structure is:
 * Field -type detailed tag type Loading data The notification of the notification of the notification of the notification of the call recovery data before saving the data of the realization data of the function card data
 */
static tag_base_handler_map_t tag_base_map[] = {
    // LF tag emulation
    {TAG_SENSE_LF, TAG_TYPE_EM410X,      lf_tag_data_loadcb,           lf_tag_em410x_data_savecb,    lf_tag_em410x_data_factory,    &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_EM410X_ELECTRA, lf_tag_data_loadcb,        lf_tag_em410x_data_savecb,    lf_tag_em410x_data_factory,    &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_HID_PROX,    lf_tag_data_loadcb,           lf_tag_hidprox_data_savecb,   lf_tag_hidprox_data_factory,   &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_IOPROX,      lf_tag_data_loadcb,           lf_tag_ioprox_data_savecb,    lf_tag_ioprox_data_factory,    &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_VIKING,      lf_tag_data_loadcb,           lf_tag_viking_data_savecb,    lf_tag_viking_data_factory,    &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_PAC,         lf_tag_data_loadcb,           lf_tag_pac_data_savecb,       lf_tag_pac_data_factory,       &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_JABLOTRON,   lf_tag_data_loadcb,           lf_tag_jablotron_data_savecb, lf_tag_jablotron_data_factory, &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_IDTECK,      lf_tag_data_loadcb,           lf_tag_idteck_data_savecb,    lf_tag_idteck_data_factory,    &m_tag_data_lf},
    {TAG_SENSE_LF, TAG_TYPE_FARPOINTE_PYRAMID, lf_tag_data_loadcb,     lf_tag_pyramid_data_savecb,   lf_tag_pyramid_data_factory,   &m_tag_data_lf},
    // MF1 tag emulation
    {TAG_SENSE_HF, TAG_TYPE_MIFARE_Mini, nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MIFARE_1024, nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MIFARE_2048, nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MIFARE_4096, nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf},
    // NTAG tag emulation
    {TAG_SENSE_HF, TAG_TYPE_NTAG_210,    nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_NTAG_212,    nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_NTAG_213,    nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_NTAG_215,    nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_NTAG_216,    nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    // MF0 tag emulation
    {TAG_SENSE_HF, TAG_TYPE_MF0ICU1,     nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MF0ICU2,     nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MF0UL11,     nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_MF0UL21,     nfc_tag_mf0_ntag_data_loadcb, nfc_tag_mf0_ntag_data_savecb, nfc_tag_mf0_ntag_data_factory, &m_tag_data_hf},
    // ISO14443-4 T=CL emulation
    {TAG_SENSE_HF, TAG_TYPE_HF14A_4,     nfc_tag_14a_4_data_loadcb,    nfc_tag_14a_4_data_savecb,    nfc_tag_14a_4_data_factory,    &m_tag_data_hf},
    {TAG_SENSE_HF, TAG_TYPE_SEOS,        nfc_tag_seos_data_loadcb,     nfc_tag_seos_data_savecb,     nfc_tag_seos_data_factory,     &m_tag_data_hf},
};

static void tag_emulation_load_config(void);
static void tag_emulation_save_config(void);

/**
 * get the data loader for the specific type of tag
 */
static tag_datas_loadcb_t get_data_loadcb_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_on_load;
        }
    }
    return NULL;
}

/**
 * accordingToTheSpecifiedDetailedLabelType,ObtainTheOperationFunctionBeforeTheDataPreservationOfTheData
 */
static tag_datas_savecb_t get_data_savecb_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_on_save;
        }
    }
    return NULL;
}

/**
 * get factory data for specific tag type
 */
static tag_datas_factory_t get_data_factory_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_factory;
        }
    }
    return NULL;
}

/**
 * accordingToTheSpecifiedDetailedLabelType,ObtainItsBasicFieldInductionType
 */
tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].sense_type;
        }
    }
    return TAG_SENSE_NO;
}

/**
 * Get buffer data according to tag type.
 */
tag_data_buffer_t *get_buffer_by_tag_type(tag_specific_type_t type) {
    if (type == TAG_TYPE_UNDEFINED) {
        return NULL;
    }
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_buffer;
        }
    }
    NRF_LOG_ERROR("no buffer valid for tag type %d.", type);
    return NULL;
}

/**
 * Load data from memory to the emulated card data.
 */
bool tag_emulation_load_by_buffer(tag_specific_type_t tag_type, bool update_crc) {
    if (!m_profile_reload_in_progress && get_sense_type_from_tag_type(tag_type) == TAG_SENSE_HF &&
            tag_emulation_profile_hf_quarantined(tag_emulation_get_slot())) return false;
    // data has been read to buffer,
    // here we load buffer to the emulator to config pwm seq for the activated card slot.
    tag_datas_loadcb_t loader = get_data_loadcb_from_tag_type(tag_type);
    if (loader == NULL) {
        NRF_LOG_INFO("no data loader exists for the tag type.");
        return false;
    }

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        return false;
    }

    int length = loader(tag_type, buffer);
    if (length <= 0 || length > buffer->length) return false;
    if (update_crc) {
        // Preserve the saved-image reference when only rebinding live RAM.
        calc_14a_crc_lut(buffer->buffer, length, (uint8_t *)buffer->crc);
    }
    return true;
}

/**
 * Load card data based on tag type.
 */
static void load_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    if (get_sense_type_from_tag_type(tag_type) == TAG_SENSE_HF &&
            tag_emulation_profile_hf_quarantined(slot)) return;
    // maybeTheCardSlotIsNotEnabledToUseTheemulationOfThisTypeOfLabel,AndSkipTheDataDirectlyToLoadThisData
    if (tag_type == TAG_TYPE_UNDEFINED) {
        return;
    }

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        return;
    }

    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);

    // get fds record for the card slot
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);

    // load data to the buffer according to the card slot currently activated.
    // If the length of data does not match the length of the buffer,
    // it may be caused by the firmware update at this time, the data must be deleted and rebuilt.
    uint16_t length = buffer->length;
    bool ret = fds_read_sync(map_info.id, map_info.key, &length, buffer->buffer);
    if (false == ret) {
        NRF_LOG_INFO("tag slot data no exists.");
        return;
    }
    ret = tag_emulation_load_by_buffer(tag_type, true);
    if (ret) {
        NRF_LOG_INFO("load tag data in slot %d, type %d done.", slot, tag_type);
    }
}

/**
 * Save data according to the type
 */
static void save_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    // Never let stale RAM be sized by metadata from a partially committed image.
    if (get_sense_type_from_tag_type(tag_type) == TAG_SENSE_HF &&
            tag_emulation_profile_hf_quarantined(slot)) return;
    // Maybe the card slot is not enabled to use the emulation of this type of label, and skip it directly to save this data
    if (tag_type == TAG_TYPE_UNDEFINED) {
        return;
    }

    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        return;
    }

    // The length of the data to be saved by the user should not exceed the size of the global buffer
    int data_byte_length = 0;
    tag_datas_savecb_t fn_savecb = get_data_savecb_from_tag_type(tag_type);
    if (fn_savecb == NULL) {  // Make sure that there is a real estate process
        NRF_LOG_INFO("Tag data saver no impl.");
        return;
    }

    data_byte_length = fn_savecb(tag_type, buffer);
    // Make sure to save data, we can judge whether the data has changed through CRC
    if (data_byte_length <= 0) {
        NRF_LOG_INFO("Tag type %d data no save.", tag_type);
        return;
    }
    // Make sure that the data to be stored is not greater than the size of the current buffer area
    if (data_byte_length > buffer->length) {
        NRF_LOG_ERROR("Tag data save length overflow.", tag_type);
        return;
    }
    uint16_t crc;
    calc_14a_crc_lut(buffer->buffer, data_byte_length, (uint8_t *)&crc);
    // Determine whether the data has changed
    if (crc == *buffer->crc) {
        NRF_LOG_INFO("Tag slot data no change, length = %d", data_byte_length);
        return;
    }
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    // Get the special card slot FDS record information
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // Call the blocked FDS to write the function, and write the data of the specified field type of the card slot into the Flash
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (sense_type == TAG_SENSE_HF && !sl1_profile_before_legacy_save(slot, tag_type, buffer->buffer)) return;
#endif
    bool ret = fds_write_sync(map_info.id, map_info.key, data_byte_length, buffer->buffer);
    if (ret) {
        NRF_LOG_INFO("Save tag slot data success.");
    } else {
        NRF_LOG_ERROR("Save tag slot data error.");
    }
    // After the preservation is completed, the CRC of the BUFFER in the corresponding memory
    if (ret) *buffer->crc = crc;
}

/**
 * Delete data according to the type
 */
static bool delete_data_by_tag_type(uint8_t slot, tag_sense_type_t sense_type) {
    if (sense_type == TAG_SENSE_NO) return false;
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int count = fds_delete_sync(map_info.id, map_info.key);
    NRF_LOG_INFO("Slot %d delete sense type %d data, record count: %d", slot, sense_type, count);
    return count >= 0;
}

/**
 * Load the emulation card data data. Note that loading is just data operation,
 * Start the analog card, please call tag_emulation_sense_run function, otherwise you will not sensor the field event
 */
void tag_emulation_load_data(void) {
    uint8_t slot = tag_emulation_get_slot();
    load_data_by_tag_type(slot, slotConfig.slots[slot].tag_hf);
    load_data_by_tag_type(slot, slotConfig.slots[slot].tag_lf);
#if defined(PROJECT_CHAMELEON_ULTRA)
    sl1_profile_load_active();
#endif
}

/**
 *Save the emulated card configuration data. At the right time, this function should be called for data preservation of data
 */
void tag_emulation_save_data(void) {
    uint8_t slot = tag_emulation_get_slot();
    save_data_by_tag_type(slot, slotConfig.slots[slot].tag_hf);
    save_data_by_tag_type(slot, slotConfig.slots[slot].tag_lf);
}

/**
 * @brief Get the type of labeling of the emulation card from the corresponding card slot.
 *
 * @param slot Card slot
 * @param tag_type Label
 */
void tag_emulation_get_specific_types_by_slot(uint8_t slot, tag_slot_specific_type_t *tag_types) {
    tag_types->tag_hf = slotConfig.slots[slot].tag_hf;
    tag_types->tag_lf = slotConfig.slots[slot].tag_lf;
}

/**
 * Delete the data specified by a card slot, if it is the current activated card slot data, we also need to dynamically close the emulation of this card
 */
bool tag_emulation_delete_data(uint8_t slot, tag_sense_type_t sense_type) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (sense_type == TAG_SENSE_HF && !sl1_profile_invalidate(slot)) return false;
#endif
    // delete data
    if (!delete_data_by_tag_type(slot, sense_type)) return false;
    // Close the corresponding card type of the corresponding card slot
    switch (sense_type) {
        case TAG_SENSE_HF: {
            slotConfig.slots[slot].tag_hf = TAG_TYPE_UNDEFINED;
            slotConfig.slots[slot].enabled_hf = false;
        }
        break;
        case TAG_SENSE_LF: {
            slotConfig.slots[slot].tag_lf = TAG_TYPE_UNDEFINED;
            slotConfig.slots[slot].enabled_lf = false;
        }
        break;
        default:
            break;
    }
    // If the deleted card slot data is currently activated (being emulated), we also need to make dynamic shutdown
    if (slotConfig.active_slot == slot) {
        tag_emulation_sense_switch(sense_type, false);
    }
    return true;
}

/**
 * Set the data of a card slot to the preset data from the factory
 */
bool tag_emulation_factory_data(uint8_t slot, tag_specific_type_t tag_type) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (get_sense_type_from_tag_type(tag_type) == TAG_SENSE_HF && !sl1_profile_invalidate(slot)) return false;
#endif
    tag_datas_factory_t factory = get_data_factory_from_tag_type(tag_type);
    // The process of implementing the data formatting data!
    if (factory != NULL && factory(slot, tag_type)) {
        // If the current data card slot number currently set is the current activated card slot, then we need to update to the memory
        if (tag_emulation_get_slot() == slot) {
            load_data_by_tag_type(slot, tag_type);
        }
        return true;
    }
    return false;
}

/**
 * Switch field induction monitoring status
 * @param enable: Whether to make the field induction
 */
static void tag_emulation_hf_sense(bool enable) {
    if (enable && tag_emulation_profile_hf_quarantined(tag_emulation_get_slot())) enable = false;
#if defined(PROJECT_CHAMELEON_ULTRA)
    sl1_profile_sense_request(enable);
#else
    nfc_tag_14a_sense_switch(enable);
#endif
}

static void tag_emulation_sense_switch_all(bool enable) {
    uint8_t slot = tag_emulation_get_slot();
    // NRF_LOG_INFO("Slot %d tag type hf %d, lf %d", slot, slotConfig.slots[slot].tag_hf, slotConfig.slots[slot].tag_lf);
    if (enable && (slotConfig.slots[slot].enabled_hf) && (slotConfig.slots[slot].tag_hf != TAG_TYPE_UNDEFINED)) {
        tag_emulation_hf_sense(true);
    } else {
        tag_emulation_hf_sense(false);
    }
    if (enable && (slotConfig.slots[slot].enabled_lf) && (slotConfig.slots[slot].tag_lf != TAG_TYPE_UNDEFINED)) {
        lf_tag_125khz_sense_switch(true);
    } else {
        lf_tag_125khz_sense_switch(false);
    }
}

/**
 * Switch field induction monitoring status
 * @param type: Field sensor type
 * @param enable: Whether to enable this type of field induction
 */
void tag_emulation_sense_switch(tag_sense_type_t type, bool enable) {
    uint8_t slot = tag_emulation_get_slot();
    // Check the parameters, not allowed to switch non -normal field
    switch (type) {
        case TAG_SENSE_NO:
            APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
            break;
        case TAG_SENSE_HF:
            if (enable && (slotConfig.slots[slot].enabled_hf) &&
                    (slotConfig.slots[slot].tag_hf != TAG_TYPE_UNDEFINED)) {
                tag_emulation_hf_sense(true);
            } else {
                tag_emulation_hf_sense(false);
            }
            break;
        case TAG_SENSE_LF:
            if (enable && (slotConfig.slots[slot].enabled_lf) &&
                    (slotConfig.slots[slot].tag_lf != TAG_TYPE_UNDEFINED)) {
                lf_tag_125khz_sense_switch(true);
            } else {
                lf_tag_125khz_sense_switch(false);
            }
            break;
    }
}

static void tag_emulation_migrate_slot_config_v0_to_v8(void) {
    // Copy old slotConfig content
    uint8_t tmpbuf[sizeof(slotConfig)];
    memcpy(tmpbuf, (uint8_t *)&slotConfig, sizeof(tmpbuf));
    NRF_LOG_INFO("Migrating slotConfig v0...");
    NRF_LOG_HEXDUMP_INFO(tmpbuf, sizeof(tmpbuf));
    // Populate new slotConfig struct
    slotConfig.version = TAG_SLOT_CONFIG_CURRENT_VERSION;
    slotConfig.active_slot = tmpbuf[0];
    for (uint8_t i = 0; i < ARRAYLEN(slotConfig.slots); i++) {
        bool enabled = tmpbuf[4 + (i * 4)] & 1;

        slotConfig.slots[i].tag_hf = tmpbuf[4 + (i * 4) + 2];
        for (uint8_t j = 0; j < ARRAYLEN(tag_specific_type_old2new_hf_values); j++) {
            if (slotConfig.slots[i].tag_hf == tag_specific_type_old2new_hf_values[j][0]) {
                slotConfig.slots[i].tag_hf = tag_specific_type_old2new_hf_values[j][1];
            }
        }
        slotConfig.slots[i].enabled_hf = slotConfig.slots[i].tag_hf != TAG_TYPE_UNDEFINED ? enabled : false;
        NRF_LOG_INFO("Slot %i HF: %02X->%04X enabled:%i", i, tmpbuf[4 + (i * 4) + 2], slotConfig.slots[i].tag_hf, slotConfig.slots[i].enabled_hf);

        slotConfig.slots[i].tag_lf = tmpbuf[4 + (i * 4) + 3];
        for (uint8_t j = 0; j < ARRAYLEN(tag_specific_type_old2new_lf_values); j++) {
            if (slotConfig.slots[i].tag_lf == tag_specific_type_old2new_lf_values[j][0]) {
                slotConfig.slots[i].tag_lf = tag_specific_type_old2new_lf_values[j][1];
            }
        }
        slotConfig.slots[i].enabled_lf = slotConfig.slots[i].tag_lf != TAG_TYPE_UNDEFINED ? enabled : false;
        NRF_LOG_INFO("Slot %i LF: %02X->%04X enabled:%i", i, tmpbuf[4 + (i * 4) + 3], slotConfig.slots[i].tag_lf, slotConfig.slots[i].enabled_lf);
    }
}

static void tag_emulation_migrate_slot_config(void) {
    switch (slotConfig.version) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
            tag_emulation_migrate_slot_config_v0_to_v8();

            /*
             * Add new migration steps ABOVE THIS COMMENT
             * `tag_emulation_save_config()` and `break` statements should only be used on the last migration step, all the previous steps must fall
             * through to the next case.
             */

            tag_emulation_save_config();
        case TAG_SLOT_CONFIG_CURRENT_VERSION:
            break;
        default:
            NRF_LOG_ERROR("Unsupported slotConfig migration attempted! (%d -> %d)", slotConfig.version, TAG_SLOT_CONFIG_CURRENT_VERSION);
            break;
    }
}

/**
 * Load the emulated card configuration data, note that loading is just a card slot configuration
 */
static void tag_emulation_load_config(void) {
    uint16_t length = sizeof(slotConfig);
    // Read the card slot configuration data
    bool ret = fds_read_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY, &length, (uint8_t *)&slotConfig);
    if (ret) {
        // After the reading is completed, we will save a BCC of the current configuration. When it is stored later, it can be used as a reference for the contrast between changes.
        calc_14a_crc_lut((uint8_t *)&slotConfig, sizeof(slotConfig), (uint8_t *)&m_slot_config_crc);
        NRF_LOG_INFO("Load tag slot config done.");
        if (slotConfig.version < TAG_SLOT_CONFIG_CURRENT_VERSION) {  // old slotConfig, need to migrate
            tag_emulation_migrate_slot_config();
        }
    } else {
        NRF_LOG_INFO("Tag slot config does not exist.");
    }
}

/**
 * Save the emulated card configuration data
 */
static void tag_emulation_save_config(void) {
    // We are configured the card slot configuration, and we need to calculate the current card slot configuration CRC code to judge whether the data below is updated
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&slotConfig, sizeof(slotConfig), (uint8_t *)&new_calc_crc);
    if (new_calc_crc != m_slot_config_crc) {  // Before saving, make sure that the card slot configuration has changed
        NRF_LOG_INFO("Save tag slot config start.");
        bool ret = fds_write_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY, sizeof(slotConfig), (uint8_t *)&slotConfig);
        if (ret) {
            NRF_LOG_INFO("Save tag slot config success.");
            m_slot_config_crc = new_calc_crc;
        } else {
            NRF_LOG_ERROR("Save tag slot config error.");
        }
    } else {
        NRF_LOG_INFO("Tag slot config no change.");
    }
}

/**
 * Start tag emulation
 */
void tag_emulation_sense_run(void) {
#if defined(PROJECT_CHAMELEON_ULTRA)
    uint8_t slot = tag_emulation_get_slot();
    tag_specific_type_t type = slotConfig.slots[slot].tag_hf;
    if (!tag_emulation_profile_hf_quarantined(slot) && !nfc_tag_14a_sensing_enabled() && type >= TAG_TYPE_MIFARE_Mini &&
            type <= TAG_TYPE_MIFARE_4096 && slotConfig.slots[slot].enabled_hf &&
            get_device_mode() != DEVICE_MODE_READER) {
        // Sensing teardown detached the MF1 runtime, not its live RAM image.
        // Rebind that image without replacing unsaved HF/LF data or saved CRC.
        if (tag_emulation_load_by_buffer(type, false)) sl1_profile_load_active();
    }
#endif
    tag_emulation_sense_switch_all(true);
}

/**
 * Stop the tag emulation. Note that this function will absolutely block NFC-related events, including awakening MCU
 * If you still need to be awakened by NFC after the MCU is required, please do not call this function
 */
void tag_emulation_sense_end(void) {
    TAG_FIELD_LED_OFF();
    tag_emulation_sense_switch_all(false);
}

/**
 * Initialized tag emulation
 */
void tag_emulation_init(void) {
    tag_emulation_load_config();  // Configuration of loading the card slot of the emulation card
    tag_emulation_load_data();    // Load the data of the emulated card
}

/**
 * Save the tag data (written from RAM to Flash)
 */
void tag_emulation_save(void) {
    tag_emulation_save_config();  // Save the card slot configuration
    tag_emulation_save_data();    // Save card slot data
}

/**
 * Get the currently activated card slot index
 */
uint8_t tag_emulation_get_slot(void) {
    return slotConfig.active_slot;
}

/**
 * Set the currently activated card slot index
 */
void tag_emulation_set_slot(uint8_t index) {
    slotConfig.active_slot = index;  // Re -set to the new switched card slot
    rgb_marquee_reset();             // force animation color refresh according to new slot
}

/**
 * Switch to the card slot of the specified index, this function will automatically complete the data loading
 */
void tag_emulation_change_slot(uint8_t index, bool sense_disable) {
    if (sense_disable) {
        // Turn off the analog card to avoid triggering the emulation when switching the card slot
        tag_emulation_sense_end();
    }
    tag_emulation_save_data();      // Save the data of the current card, in case of there is a change
    g_is_tag_emulating = false;     // Reset the emulating flag
    tag_emulation_set_slot(index);  // Update the index of the activated card slot
    tag_emulation_load_data();      // Then reload the data of the card slot
    if (sense_disable) {
        // According to the configuration of the new card slot, the monitoring status of our update
        tag_emulation_sense_run();
    }
}

/**
 * Determine whether the specified card slot is enabled
 */
bool is_slot_enabled(uint8_t slot, tag_sense_type_t sense_type) {
    if (sense_type == TAG_SENSE_LF) {
        return slotConfig.slots[slot].enabled_lf;
    }
    if (sense_type == TAG_SENSE_HF) {
        return slotConfig.slots[slot].enabled_hf;
    }
    return false;
}

/**
 * Set whether the specified card slot is enabled
 */
void tag_emulation_slot_set_enable(uint8_t slot, tag_sense_type_t sense_type, bool enable) {
    // Set the capacity of the corresponding card slot directly
    if (sense_type == TAG_SENSE_LF) {
        slotConfig.slots[slot].enabled_lf = enable;
    }
    if (sense_type == TAG_SENSE_HF) {
        slotConfig.slots[slot].enabled_hf = enable;
    }
}

/**
 * Find the next valid card slot
 */
uint8_t tag_emulation_slot_find_next(uint8_t slot_now) {
    uint8_t start_slot = (slot_now + 1 == TAG_MAX_SLOT_NUM) ? 0 : slot_now + 1;
    for (uint8_t i = start_slot;;) {
        if (i == slot_now) return slot_now;                                              // No other activated card slots were found after a loop
        if (slotConfig.slots[i].enabled_hf || slotConfig.slots[i].enabled_lf) return i;  // Check whether the card slot that is currently traversed is enabled, so that the capacity determines that the current card slot is the card slot that can effectively enable capacity
        i++;
        if (i == TAG_MAX_SLOT_NUM) {  // Continue the next cycle
            i = 0;
        }
    }
    return slot_now;  // If you cannot find it, the specified return value of the pass is returned by default
}

/**
 * Find the previous valid card slot
 */
uint8_t tag_emulation_slot_find_prev(uint8_t slot_now) {
    uint8_t start_slot = (slot_now == 0) ? (TAG_MAX_SLOT_NUM - 1) : slot_now - 1;
    for (uint8_t i = start_slot;;) {
        if (i == slot_now) return slot_now;                                              // No other activated card slots were found after a loop
        if (slotConfig.slots[i].enabled_hf || slotConfig.slots[i].enabled_lf) return i;  // Check whether the card slot that is currently traversed is enabled, so that the capacity determines that the current card slot is the card slot that can effectively enable capacity
        if (i == 0) {                                                                    // Continue the next cycle
            i = TAG_MAX_SLOT_NUM - 1;
        } else {
            i--;
        }
    }
    return slot_now;  // If you cannot find it, the specified return value of the pass is returned by default
}

/**
 * Set the card specified by the specified card slot card slot card type card to the specified type
 */
void tag_emulation_change_type(uint8_t slot, tag_specific_type_t tag_type) {
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
#if defined(PROJECT_CHAMELEON_ULTRA)
    if (sense_type == TAG_SENSE_HF && !sl1_profile_invalidate(slot)) return;
#endif
    NRF_LOG_INFO("sense type = %d", sense_type);
    switch (sense_type) {
        case TAG_SENSE_LF: {
            slotConfig.slots[slot].tag_lf = tag_type;
            break;
        }
        case TAG_SENSE_HF: {
            slotConfig.slots[slot].tag_hf = tag_type;
            break;
        }
        default:
            break;  // never happen
    }
    NRF_LOG_INFO("tag type = %d", tag_type);
    // After the update is completed, we need to notify the relevant data in the update of the memory
    if (sense_type != TAG_SENSE_NO) {
        load_data_by_tag_type(slot, tag_type);
        NRF_LOG_INFO("reload data success.");
    }
}

/**
 * The factory initialization function of the card emulation.
 * Defaults to a dual-frequency card in slot 1, a hf M1 card in slot 2, and a lf em410x card in slot 3.
 */
void tag_emulation_factory_init(void) {
    fds_slot_record_map_t map_info;

    // Initialized a dual -frequency card in the card slot, if there is no historical record, it is a new state of factory.
    if (slotConfig.slots[0].enabled_hf && slotConfig.slots[0].tag_hf == TAG_TYPE_MIFARE_1024) {
        // Initialize a high -frequency M1 card in the card slot 1, if it does not exist.
        get_fds_map_by_slot_sense_type_for_dump(0, TAG_SENSE_HF, &map_info);
        if (!fds_is_exists(map_info.id, map_info.key)) {
            tag_emulation_factory_data(0, slotConfig.slots[0].tag_hf);
        }
    }

    if (slotConfig.slots[0].enabled_lf && slotConfig.slots[0].tag_lf == TAG_TYPE_EM410X) {
        // Initialize a low -frequency EM410X card in slot 1, if it does not exist.
        get_fds_map_by_slot_sense_type_for_dump(0, TAG_SENSE_LF, &map_info);
        if (!fds_is_exists(map_info.id, map_info.key)) {
            tag_emulation_factory_data(0, slotConfig.slots[0].tag_lf);
        }
    }

    if (slotConfig.slots[1].enabled_hf && slotConfig.slots[1].tag_hf == TAG_TYPE_MF0ICU1) {
        // Initialize a high -frequency M1 card in the card slot 2, if it does not exist.
        get_fds_map_by_slot_sense_type_for_dump(1, TAG_SENSE_HF, &map_info);
        if (!fds_is_exists(map_info.id, map_info.key)) {
            tag_emulation_factory_data(1, slotConfig.slots[1].tag_hf);
        }
    }

    if (slotConfig.slots[2].enabled_lf && slotConfig.slots[2].tag_lf == TAG_TYPE_EM410X) {
        // Initialize a low -frequency EM410X card in slot 3, if it does not exist.
        get_fds_map_by_slot_sense_type_for_dump(2, TAG_SENSE_LF, &map_info);
        if (!fds_is_exists(map_info.id, map_info.key)) {
            tag_emulation_factory_data(2, slotConfig.slots[2].tag_lf);
        }
    }
}

/* Persist target HF fields over the persisted/default baseline. Unrelated live
 * edits stay volatile and dirty; RAM target fields advance only after readback. */
bool tag_emulation_profile_save_config(uint8_t slot, uint16_t type) {
    if (slot >= TAG_MAX_SLOT_NUM || type < TAG_TYPE_MIFARE_Mini || type > TAG_TYPE_MIFARE_4096)
        return false;
    tag_slot_config_t next ALIGN_U32;
    int count = fds_record_count_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY);
    if (count < 0 || count > 1) return false;
    if (count == 0) {
        memcpy(&next, &m_default_slot_config, sizeof(next));
    } else {
        uint16_t length = sizeof(next);
        if (!fds_read_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY,
                           &length, (uint8_t *)&next) || length != sizeof(next) ||
                next.version != TAG_SLOT_CONFIG_CURRENT_VERSION || next.active_slot >= TAG_MAX_SLOT_NUM)
            return false;
    }
    next.slots[slot].tag_hf = type;
    next.slots[slot].enabled_hf = true;
    if (!fds_write_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY,
                         sizeof(next), &next)) return false;
    tag_slot_config_t confirmed ALIGN_U32;
    uint16_t length = sizeof(confirmed);
    bool verified = fds_read_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY,
                                  &length, (uint8_t *)&confirmed) && length == sizeof(confirmed) &&
                    !memcmp(&next, &confirmed, sizeof(next));
    memset(&confirmed, 0, sizeof(confirmed));
    if (!verified) return false;
    // Preserve unrelated live edits; their ordinary save remains dirty.
    slotConfig.slots[slot].tag_hf = type;
    slotConfig.slots[slot].enabled_hf = true;
    calc_14a_crc_lut((uint8_t *)&next, sizeof(next), (uint8_t *)&m_slot_config_crc);
    return true;
}

/* Transaction reload reports FDS and native loader errors rather than treating
 * an old RAM buffer as a successfully loaded new profile. */
bool tag_emulation_profile_reload_active(void) {
    uint8_t slot = tag_emulation_get_slot();
    uint16_t type = slotConfig.slots[slot].tag_hf;
    if (type < TAG_TYPE_MIFARE_Mini || type > TAG_TYPE_MIFARE_4096) return false;
    static const uint16_t block_counts[] = {20, 64, 128, 256};
    uint16_t expected = sizeof(nfc_tag_14a_coll_res_entity_t) + sizeof(nfc_tag_mf1_configure_t) +
                        block_counts[type - TAG_TYPE_MIFARE_Mini] * 16U;
    expected = (expected + 3U) & ~3U;
#if defined(PROJECT_CHAMELEON_ULTRA)
    sl1_profile_sense_request(false);
#endif
    uint16_t length = m_tag_data_hf.length;
    if (!fds_read_sync(FDS_SLOT_TAG_DUMP_FILE_ID_BASE + slot, TAG_SENSE_HF,
                       &length, m_tag_data_hf.buffer) || length != expected) return false;
    m_profile_reload_in_progress = true;
    bool loaded = tag_emulation_load_by_buffer(type, true);
    m_profile_reload_in_progress = false;
    if (!loaded) return false;
#if defined(PROJECT_CHAMELEON_ULTRA)
    sl1_profile_load_active();
#endif
    return true;
}
