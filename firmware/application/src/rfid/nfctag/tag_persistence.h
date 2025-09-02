#ifndef TAG_PERSISTENCE_H
#define TAG_PERSISTENCE_H

#include <stdint.h>

#include "tag_base_type.h"

typedef struct {
    uint16_t key;
    uint16_t id;
} fds_slot_record_map_t;

/**
 * According to the specified card slot and card field type, obtain the mapping object of the FDS information of the
 * corresponding card data
 */
void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t *map);
/**
 *According to the specified card slot and card field type, obtain the mapping object of the FDS information of the
 *nickname of the corresponding card data
 */
void get_fds_map_by_slot_sense_type_for_nick(uint8_t slot, tag_sense_type_t sense_type, fds_slot_record_map_t *map);

#endif
