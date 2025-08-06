#ifndef NFC_TAG_H
#define NFC_TAG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "app_util.h"
#include "tag_base_type.h"
#include "utils.h"

// Up to eight card slots
#define TAG_MAX_SLOT_NUM 8

extern bool g_is_tag_emulating;

// Tag data buffer
typedef struct {
    uint16_t length;
    uint8_t *buffer;
    uint16_t *crc;
} tag_data_buffer_t;

// Farming impact enable and closed energy switching function
typedef void (*tag_sense_switch_t)(bool enable);
// Flash data is notified to the registrar after loading to RAM
typedef int (*tag_datas_loadcb_t)(tag_specific_type_t type, tag_data_buffer_t *buffer);
// The data should be saved to the registered person before Flash
typedef int (*tag_datas_savecb_t)(tag_specific_type_t type, tag_data_buffer_t *buffer);
// Data factory initialization function
typedef bool (*tag_datas_factory_t)(uint8_t slot, tag_specific_type_t type);

// The data of the tag data loading and the recovery function of the preservation event mapping table
typedef struct {
    tag_sense_type_t sense_type;
    tag_specific_type_t tag_type;
    tag_datas_loadcb_t data_on_load;
    tag_datas_savecb_t data_on_save;
    tag_datas_factory_t data_factory;
    tag_data_buffer_t *data_buffer;
} tag_base_handler_map_t;

/**
 * The storage configuration of parameters such as the type of card emulated in the card slot
 * This configuration can be preserved by persistently to Flash
 * 4 bytes a word, keep in mind the entire word alignment
 */
#define TAG_SLOT_CONFIG_CURRENT_VERSION 8
// Intended struct size, for static assert
#define TAG_SLOT_CONFIG_CURRENT_SIZE 68

typedef struct {
    // Basic configuration
    uint8_t version;      // struct version (U8 so map on old .activated<=7 field)
    uint8_t active_slot;  // Which slot is currently active
    uint32_t : 0;         // U32 align
    struct {              // 4-byte slot config + 2*2-byte tag_specific_types
        // Individual slot configuration
        uint32_t enabled_hf : 1;  // Whether to enable the HF card
        uint32_t enabled_lf : 1;  // Whether to enable the LF card
        uint32_t : 0;             // U32 align
        // Specific type of emulated card
        union {
            uint16_t U16_tag_hf;
            tag_specific_type_t tag_hf;
        };
        union {
            uint16_t U16_tag_lf;
            tag_specific_type_t tag_lf;
        };
    } slots[TAG_MAX_SLOT_NUM];
} PACKED tag_slot_config_t;

// Use the macro to check the struct size
STATIC_ASSERT(sizeof(tag_slot_config_t) == TAG_SLOT_CONFIG_CURRENT_SIZE);

// The most basic emulation card initialization program
void tag_emulation_init(void);
// Some of the data stored in RAM can be saved to Flash through this interface
void tag_emulation_save(void);

// Starting and ending of the emulation card
void tag_emulation_sense_run(void);
void tag_emulation_sense_end(void);

// Farming response enable state switching package function
void tag_emulation_sense_switch(tag_sense_type_t type, bool enable);
// Delete the type of card specified in the card slot
void tag_emulation_delete_data(uint8_t slot, tag_sense_type_t sense_type);
// Initial data of the factory data of the specified card slot into the factory of the specified type of card
bool tag_emulation_factory_data(uint8_t slot, tag_specific_type_t tag_type);
// Change the type of the card that is being emulated
void tag_emulation_change_type(uint8_t slot, tag_specific_type_t tag_type);
// Load the data from the memory to the emulation card buffer
bool tag_emulation_load_by_buffer(tag_specific_type_t tag_type, bool update_crc);

tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type);
tag_data_buffer_t *get_buffer_by_tag_type(tag_specific_type_t type);

// Set the card slot currently used
void tag_emulation_set_slot(uint8_t index);
// Get the card slot currently used
uint8_t tag_emulation_get_slot(void);
// Switch the card slot to control whether the passing parameter control is closed during the switching period to listen to
void tag_emulation_change_slot(uint8_t index, bool sense_disable);
// Get the card slot to enable the state
bool is_slot_enabled(uint8_t slot, tag_sense_type_t sense_type);
// Set the card slot to enable
void tag_emulation_slot_set_enable(uint8_t slot, tag_sense_type_t sense_type, bool enable);
// Get the emulation card type of the corresponding card slot
void tag_emulation_get_specific_types_by_slot(uint8_t slot, tag_slot_specific_type_t *tag_types);
// Initialize some factory data
void tag_emulation_factory_init(void);

// In the direction, query any card slot that enable
uint8_t tag_emulation_slot_find_next(uint8_t slot_now);
uint8_t tag_emulation_slot_find_prev(uint8_t slot_now);
bool is_tag_specific_type_valid(tag_specific_type_t tag_type);

#endif
