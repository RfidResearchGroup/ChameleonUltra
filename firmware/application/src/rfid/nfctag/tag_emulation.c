#include "crc_utils.h"
#include "nfc_14a.h"
#include "lf_tag_em.h"
#include "nfc_mf1.h"
#include "nfc_ntag.h"
#include "fds_ids.h"
#include "fds_util.h"
#include "tag_emulation.h"
#include "tag_persistence.h"
#include "rgb_marquee.h"


#define NRF_LOG_MODULE_NAME tag_emu
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();


/*
 * A card slot can simulate up to two cards at the same time, one ID 125kHz EM410X, and one IC 13.56MHz 14A.(May be able to support more in the future)
 * When starting, you should start the startup listener on demand (there is no emulated card when there is no data, but you need to monitor the state on demand)
 * If the retrieved card slot configuration has a specified type of card, then loading the specified type of data should be carried out, and the necessary parameters of initialization should be performed.
 * When the on-field entry is detected, in addition to the relevant LED, you also need to start the simulation card according to whether the current data is loaded.
 * In the simulation card, all operations should be carried out based on the data loaded in RAM. After the analog card is over, the modified data should be preserved to Flash
 *
 *
 *
 * ......
 */

// Is the logo in the analog card?
bool g_is_tag_emulating = false;

static tag_specific_type_t tag_specific_type_old2new_lf_values[][2] = { TAG_SPECIFIC_TYPE_OLD2NEW_LF_VALUES };
static tag_specific_type_t tag_specific_type_old2new_hf_values[][2] = { TAG_SPECIFIC_TYPE_OLD2NEW_HF_VALUES };
static tag_specific_type_t tag_specific_type_lf_values[] = { TAG_SPECIFIC_TYPE_LF_VALUES };
static tag_specific_type_t tag_specific_type_hf_values[] = { TAG_SPECIFIC_TYPE_HF_VALUES };

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
 * The label data exists in the information in Flash, and the total length must be aligned by 4 bytes (whole words)!IntersectionIntersection
 */
static uint8_t m_tag_data_buffer_lf[12];      // Low -frequency card data buffer
static uint16_t m_tag_data_lf_crc;
static tag_data_buffer_t m_tag_data_lf = { sizeof(m_tag_data_buffer_lf), m_tag_data_buffer_lf, &m_tag_data_lf_crc };

static uint8_t m_tag_data_buffer_hf[4500];    // High -frequency card data buffer
static uint16_t m_tag_data_hf_crc;
static tag_data_buffer_t m_tag_data_hf = { sizeof(m_tag_data_buffer_hf), m_tag_data_buffer_hf, &m_tag_data_hf_crc };

/**
 * Eight card slots, each card slot has its own unique configuration
 */
static tag_slot_config_t slotConfig ALIGN_U32 = {
    // Configure activated card slot, default activation of the 0th card slot (the first card)
    .version = TAG_SLOT_CONFIG_CURRENT_VERSION,
    .active_slot = 0,
    // Configuration card slots
    // See tag_emulation_factory_init for actual tag content
    .slots = {
        { .enabled_hf = true,  .enabled_lf = true,  .tag_hf = TAG_TYPE_MIFARE_1024, .tag_lf = TAG_TYPE_EM410X,    },   // 1
        { .enabled_hf = true,  .enabled_lf = false, .tag_hf = TAG_TYPE_MIFARE_1024, .tag_lf = TAG_TYPE_UNDEFINED, },   // 2
        { .enabled_hf = false, .enabled_lf = true,  .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_EM410X,    },   // 3
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, },   // 4
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, },   // 5
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, },   // 6
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, },   // 7
        { .enabled_hf = false, .enabled_lf = false, .tag_hf = TAG_TYPE_UNDEFINED,   .tag_lf = TAG_TYPE_UNDEFINED, },   // 8
    },
};
// The card slot configuration unique CRC, once the slot configuration changes, can be checked by CRC
static uint16_t m_slot_config_crc;

// ********************** Specific parameter ends **********************


/**
 * The data of the label is loaded to the RAM and the mapping table of the operation of the regulating notification,
 * The mapping structure is:
 * Field -type detailed label type Loading data The notification of the notification of the notification of the notification of the call recovery data before saving the data of the realization data of the function card data
 */
static tag_base_handler_map_t tag_base_map[] = {
    // Low -frequency ID card simulation
    { TAG_SENSE_LF,    TAG_TYPE_EM410X,         lf_tag_em410x_data_loadcb,    lf_tag_em410x_data_savecb,    lf_tag_em410x_data_factory,    &m_tag_data_lf },
    // MF1 tag simulation
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_Mini,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_1024,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_2048,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_MIFARE_4096,    nfc_tag_mf1_data_loadcb,      nfc_tag_mf1_data_savecb,      nfc_tag_mf1_data_factory,      &m_tag_data_hf },
    // NTAG tag simulation
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_213,      nfc_tag_ntag_data_loadcb,     nfc_tag_ntag_data_savecb,      nfc_tag_ntag_data_factory,     &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_215,      nfc_tag_ntag_data_loadcb,     nfc_tag_ntag_data_savecb,      nfc_tag_ntag_data_factory,     &m_tag_data_hf },
    { TAG_SENSE_HF,    TAG_TYPE_NTAG_216,      nfc_tag_ntag_data_loadcb,     nfc_tag_ntag_data_savecb,      nfc_tag_ntag_data_factory,     &m_tag_data_hf },
};


static void tag_emulation_load_config(void);
static void tag_emulation_save_config(void);

/**
 * accordingToTheSpecifiedDetailedLabelType,ObtainTheImplementationFunctionOfTheDataThatProcessesTheLoadedLoaded
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
 * accordingToTheSpecifiedDetailedLabelType,ObtainTheOperationFunctionOfTheDataFactoryInitialized
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
 * obtainTheBufferInformationAccordingToTheType
 */
tag_data_buffer_t *get_buffer_by_tag_type(tag_specific_type_t type) {
    for (int i = 0; i < ARRAY_SIZE(tag_base_map); i++) {
        if (tag_base_map[i].tag_type == type) {
            return tag_base_map[i].data_buffer;
        }
    }
    return NULL;
}

/**
* loadDataFromMemoryToTheSimulationCardData
 */
bool tag_emulation_load_by_buffer(tag_specific_type_t tag_type, bool update_crc) {
    // theDataHasBeenLoadedToTheBufferArea,AndTheConfigurationOfTheActivatedCardSlotIsNext, //PassTheBufferOfTheSettingOfTheSettingSimulationCardType (highFrequencyCard,LowFrequencyCard)ToIt
    tag_datas_loadcb_t fn_loadcb = get_data_loadcb_from_tag_type(tag_type);
    if (fn_loadcb == NULL) {    //makeSureThatThereIsACorrespondingLoadingProcess
        NRF_LOG_INFO("Tag data loader no impl.");
        return false;
    }
    //theCorrespondingImplementation,WeHaveLoadedTheData
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    int length = fn_loadcb(tag_type, buffer);
    if (length > 0 && update_crc) {
        // afterReadingIsCompleted,WeCanSaveACrcOfTheCurrentDataWhenItIsStoredLater,ItCanBeUsedAsAReferenceForChangesComparison
        calc_14a_crc_lut(buffer->buffer, length, (uint8_t *)buffer->crc);
        return true;
    }
    return false;
}

/**
 * loadTheDataAccordingToTheType
 */
static void load_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    // maybeTheCardSlotIsNotEnabledToUseTheSimulationOfThisTypeOfLabel,AndSkipTheDataDirectlyToLoadThisData
    if (tag_type == TAG_TYPE_UNDEFINED) {
        return;
    }
    // getTheSpecialBufferInformation
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        NRF_LOG_ERROR("No buffer valid!");
        return;
    }
    tag_sense_type_t sense_type = get_sense_type_from_tag_type(tag_type);
    // getTheSpecialCardSlotFdsRecordInformation
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    // accordingToTheTypeOfTheCardSlotCurrentlyActivated,LoadTheDataOfTheDesignatedFieldToTheBuffer //Tip:IfTheLengthOfTheDataCannotMatchTheLengthOfTheBuffer,ItMayBeCausedByTheFirmwareUpdateAtThisTime,TheDataMustBeDeletedAndRebuilt
    uint16_t length = buffer->length;
    bool ret = fds_read_sync(map_info.id, map_info.key, &length, buffer->buffer);
    if (false == ret) {
        NRF_LOG_INFO("Tag slot data no exists.");
        return;
    }
    ret = tag_emulation_load_by_buffer(tag_type, true);
    if (ret) {
        NRF_LOG_INFO("Load tag slot %d, type %d data done.", slot, tag_type);
    }
}

/**
 * Save data according to the type
 */
static void save_data_by_tag_type(uint8_t slot, tag_specific_type_t tag_type) {
    // Maybe the card slot is not enabled to use the simulation of this type of label, and skip it directly to save this data
    if (tag_type == TAG_TYPE_UNDEFINED) {
        return;
    }
    tag_data_buffer_t *buffer = get_buffer_by_tag_type(tag_type);
    if (buffer == NULL) {
        NRF_LOG_ERROR("No buffer valid!");
        return;
    }
    // The length of the data to be saved by the user should not exceed the size of the global buffer
    int data_byte_length = 0;
    tag_datas_savecb_t fn_savecb = get_data_savecb_from_tag_type(tag_type);
    if (fn_savecb == NULL) {        //Make sure that there is a real estate process
        NRF_LOG_INFO("Tag data saver no impl.");
        return;
    } else {
        data_byte_length = fn_savecb(tag_type, buffer);
    }
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
    // Calculate the length of the data to be saved (automatically fill in the whole word)
    int data_word_length = (data_byte_length / 4) + (data_byte_length % 4 > 0 ? 1 : 0);
    // Call the blocked FDS to write the function, and write the data of the specified field type of the card slot into the Flash
    bool ret = fds_write_sync(map_info.id, map_info.key, data_word_length, buffer->buffer);
    if (ret) {
        NRF_LOG_INFO("Save tag slot data success.");
    } else {
        NRF_LOG_ERROR("Save tag slot data error.");
    }
    //After the preservation is completed, the CRC of the BUFFER in the corresponding memory
    *buffer->crc = crc;
}

/**
 * Delete data according to the type
 */
static void delete_data_by_tag_type(uint8_t slot, tag_sense_type_t sense_type) {
    if (sense_type == TAG_SENSE_NO) {
        return;
    }
    fds_slot_record_map_t map_info;
    get_fds_map_by_slot_sense_type_for_dump(slot, sense_type, &map_info);
    int count = fds_delete_sync(map_info.id, map_info.key);
    NRF_LOG_INFO("Slot %d delete sense type %d data, record count: %d", slot, sense_type, count);
}

/**
 * Load the simulation card data data. Note that loading is just data operation,
 * Start the analog card, please call tag_emulation_sense_run function, otherwise you will not sensor the field event
 */
void tag_emulation_load_data(void) {
    uint8_t slot = tag_emulation_get_slot();
    load_data_by_tag_type(slot, slotConfig.slots[slot].tag_hf);
    load_data_by_tag_type(slot, slotConfig.slots[slot].tag_lf);
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
 * @brief Get the type of labeling of the simulation card from the corresponding card slot.
 *
 * @param slot Card slot
 * @param tag_type Label
 */
void tag_emulation_get_specific_types_by_slot(uint8_t slot, tag_slot_specific_type_t *tag_types) {
    tag_types->tag_hf = slotConfig.slots[slot].tag_hf;
    tag_types->tag_lf = slotConfig.slots[slot].tag_lf;
}

/**
 * Delete the data specified by a card slot, if it is the current activated card slot data, we also need to dynamically close the simulation of this card
 */
void tag_emulation_delete_data(uint8_t slot, tag_sense_type_t sense_type) {
    // delete data
    delete_data_by_tag_type(slot, sense_type);
    //Close the corresponding card type of the corresponding card slot
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
    // If the deleted card slot data is currently activated (being simulated), we also need to make dynamic shutdown
    if (slotConfig.active_slot == slot) {
        tag_emulation_sense_switch(sense_type, false);
    }
}

/**
 * Set the data of a card slot to the preset data from the factory
 */
bool tag_emulation_factory_data(uint8_t slot, tag_specific_type_t tag_type) {
    tag_datas_factory_t factory = get_data_factory_from_tag_type(tag_type);
    if (factory != NULL) {
        // The process of implementing the data formatting data!
        if (factory(slot, tag_type)) {
            // If the current data card slot number currently set is the current activated card slot, then we need to update to the memory
            if (tag_emulation_get_slot() == slot) {
                load_data_by_tag_type(slot, tag_type);
            }
            return true;
        }
    }
    return false;
}

/**
 * Switch field induction monitoring status
 * @param enable: Whether to make the field induction
 */
static void tag_emulation_sense_switch_all(bool enable) {
    uint8_t slot = tag_emulation_get_slot();
    // NRF_LOG_INFO("Slot %d tag type hf %d, lf %d", slot, slotConfig.slots[slot].tag_hf, slotConfig.slots[slot].tag_lf);
    if (enable && (slotConfig.slots[slot].enabled_hf) && (slotConfig.slots[slot].tag_hf != TAG_TYPE_UNDEFINED)) {
        nfc_tag_14a_sense_switch(true);
    } else {
        nfc_tag_14a_sense_switch(false);
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
                nfc_tag_14a_sense_switch(true);
            } else {
                nfc_tag_14a_sense_switch(false);
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
    for (uint8_t i = 0; i <  ARRAYLEN(slotConfig.slots); i++) {
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
        if (slotConfig.version < TAG_SLOT_CONFIG_CURRENT_VERSION) { // old slotConfig, need to migrate
            tag_emulation_migrate_slot_config();
        }
    } else {
        NRF_LOG_INFO("Tag slot config does not exist.");
    }
}

/**
 *Save the emulated card configuration data
 */
static void tag_emulation_save_config(void) {
    // We are configured the card slot configuration, and we need to calculate the current card slot configuration CRC code to judge whether the data below is updated
    uint16_t new_calc_crc;
    calc_14a_crc_lut((uint8_t *)&slotConfig, sizeof(slotConfig), (uint8_t *)&new_calc_crc);
    if (new_calc_crc != m_slot_config_crc) {    // Before saving, make sure that the card slot configuration has changed
        NRF_LOG_INFO("Save tag slot config start.");
        bool ret = fds_write_sync(FDS_EMULATION_CONFIG_FILE_ID, FDS_EMULATION_CONFIG_RECORD_KEY, sizeof(slotConfig) / 4, (uint8_t *)&slotConfig);
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
 * Start label simulation
 */
void tag_emulation_sense_run(void) {
    tag_emulation_sense_switch_all(true);
}

/**
 * Stop the label simulation. Note that this function will absolutely block NFC -related events, including awakening MCU
 * If you still need to be awakened by NFC after the MCU is required, please do not call this function
 */
void tag_emulation_sense_end(void) {
    TAG_FIELD_LED_OFF();
    tag_emulation_sense_switch_all(false);
}

/**
 *Initialized label simulation
 */
void tag_emulation_init(void) {
    tag_emulation_load_config();    // Configuration of loading the card slot of the simulation card
    tag_emulation_load_data();      // Load the data of the emulated card
}

/**
 *Save the label data (written from RAM to Flash)
 */
void tag_emulation_save(void) {
    tag_emulation_save_config();    // Save the card slot configuration
    tag_emulation_save_data();      // Save card slot data
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
    slotConfig.active_slot = index;    // Re -set to the new switched card slot
    rgb_marquee_reset(); // force animation color refresh according to new slot
}

/**
 * Switch to the card slot of the specified index, this function will automatically complete the data loading
 */
void tag_emulation_change_slot(uint8_t index, bool sense_disable) {
    if (sense_disable) {
        // Turn off the analog card to avoid triggering the simulation when switching the card slot
        tag_emulation_sense_end();
    }
    tag_emulation_save_data();      // Save the data of the current card, if there is a change, if there is a change
    g_is_tag_emulating = false;     // Reset the logo position
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
bool tag_emulation_slot_is_enabled(uint8_t slot, tag_sense_type_t sense_type) {
    switch (sense_type) {
        case TAG_SENSE_LF: {
            return slotConfig.slots[slot].enabled_lf;
            break;
        }
        case TAG_SENSE_HF: {
            return slotConfig.slots[slot].enabled_hf;
            break;
        }
        default:
            return false;
            break; //Never happen
    }
}

/**
 * Set whether the specified card slot is enabled
 */
void tag_emulation_slot_set_enable(uint8_t slot, tag_sense_type_t sense_type, bool enable) {
    //Set the capacity of the corresponding card slot directly
    switch (sense_type) {
        case TAG_SENSE_LF: {
            slotConfig.slots[slot].enabled_lf = enable;
            break;
        }
        case TAG_SENSE_HF: {
            slotConfig.slots[slot].enabled_hf = enable;
            break;
        }
        default:
            break; //Never happen
    }
}

/**
 *Find the next valid card slot
 */
uint8_t tag_emulation_slot_find_next(uint8_t slot_now) {
    uint8_t start_slot = (slot_now + 1 == TAG_MAX_SLOT_NUM) ? 0 : slot_now + 1;
    for (uint8_t i = start_slot;;) {
        if (i == slot_now) return slot_now;         // No other activated card slots were found after a loop
        if (slotConfig.slots[i].enabled_hf || slotConfig.slots[i].enabled_lf) return i;    // Check whether the card slot that is currently traversed is enabled, so that the capacity determines that the current card slot is the card slot that can effectively enable capacity
        i++;
        if (i == TAG_MAX_SLOT_NUM) {            // Continue the next cycle
            i = 0;
        }
    }
    return slot_now;    // If you cannot find it, the specified return value of the pass is returned by default
}

/**
 * Find the previous valid card slot
 */
uint8_t tag_emulation_slot_find_prev(uint8_t slot_now) {
    uint8_t start_slot = (slot_now == 0) ? (TAG_MAX_SLOT_NUM - 1) : slot_now - 1;
    for (uint8_t i = start_slot;;) {
        if (i == slot_now) return slot_now;         //No other activated card slots were found after a loop
        if (slotConfig.slots[i].enabled_hf || slotConfig.slots[i].enabled_lf) return i;   // Check whether the card slot that is currently traversed is enabled, so that the capacity determines that the current card slot is the card slot that can effectively enable capacity
        if (i == 0) {    // Continue the next cycle
            i = TAG_MAX_SLOT_NUM - 1;
        } else {
            i--;
        }
    }
    return slot_now;    // If you cannot find it, the specified return value of the pass is returned by default
}

/**
 *Set the card specified by the specified card slot card slot card type card to the specified type
 */
void tag_emulation_change_type(uint8_t slot, tag_specific_type_t tag_type) {
    tag_sense_type_t sense_type =  get_sense_type_from_tag_type(tag_type);
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
            break; //Never happen
    }
    NRF_LOG_INFO("tag type = %d", tag_type);
    //After the update is completed, we need to notify the relevant data in the update of the memory
    if (sense_type != TAG_SENSE_NO) {
        load_data_by_tag_type(slot, tag_type);
        NRF_LOG_INFO("reload data success.");
    }
}

/**
 * @briefThe factory initialization function of the simulation card
 * Some data that can be used to initialize the default factory factory
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

    if (slotConfig.slots[1].enabled_hf && slotConfig.slots[1].tag_hf == TAG_TYPE_MIFARE_1024) {
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
