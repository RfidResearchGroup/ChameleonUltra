#ifndef FDS_IDS_H
#define FDS_IDS_H

/*
 * Card slot configuration, only one, consistent
 */
#define FDS_EMULATION_CONFIG_FILE_ID        0x1000
#define FDS_EMULATION_CONFIG_RECORD_KEY     0x1

/*
 * Slot for settings like LED animation mode and future options
 */
#define FDS_SETTINGS_FILE_ID                0x1001
#define FDS_SETTINGS_RECORD_KEY             0x1

/*
 * Each card slot has two types of data, high and low frequency
 * FDS file ID follows the card slot, starting from 0x1100 to 0x1107
 * FDS record key mirrors TAG_SENSE_LF/HF so is 1 for LF, 2 for HF (currently)
 */
#define FDS_SLOT_TAG_DUMP_FILE_ID_BASE      0x1100

/*
 * Each card slot has two types of data, high and low frequency, so it can get two names
 * FDS file ID follows the card slot, starting from 0x1200 to 0x1207
 * FDS record key mirrors TAG_SENSE_LF/HF so is 1 for LF, 2 for HF (currently)
 */
#define FDS_SLOT_TAG_NICK_NAME_FILE_ID_BASE 0x1200

// Note that previously assigned records may need to be cleaned from Flash.
// Taking into account the possible overlaps, it boils down to
// ID 0x1066 Keys 0x1066
// ID 0x1067 Keys 0x1067-0x106e
// ID 0x1068 Keys 0x1067-0x106f
// ID 0x1069 Keys 0x1068-0x1070


#endif
