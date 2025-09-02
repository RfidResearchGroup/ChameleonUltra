#include "tag_persistence.h"

#include "fds_ids.h"

#define NRF_LOG_MODULE_NAME tag_persistence
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
NRF_LOG_MODULE_REGISTER();

static void get_fds_map_by_slot_auto_inc_id(uint16_t id, uint8_t slot, tag_sense_type_t sense_type,
                                            fds_slot_record_map_t *map)
{
    if ((sense_type == TAG_SENSE_NO) || (slot > 7)) {
        APP_ERROR_CHECK(NRF_ERROR_INVALID_PARAM);
    }
    map->id = id + slot;
    map->key = sense_type;
}

/**
 * Obtain the KEY and ID of the corresponding data in FDS according to the card slot and the field type specified in the
 * card slot
 */
void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t *map)
{
    get_fds_map_by_slot_auto_inc_id(FDS_SLOT_TAG_DUMP_FILE_ID_BASE, slot, sense_type, map);
}

/**
 * Obtain the KEY and ID of the corresponding data in FDS according to the card slot and the field type specified in the
 * card slot
 */
void get_fds_map_by_slot_sense_type_for_nick(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t *map)
{
    get_fds_map_by_slot_auto_inc_id(FDS_SLOT_TAG_NICK_NAME_FILE_ID_BASE, slot, sense_type, map);
}
