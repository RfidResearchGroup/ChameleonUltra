#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Structure for packed wiegand messages
// Always align lowest value (last transmitted) bit to ordinal position 0 (lowest valued bit bottom)
typedef struct {
    uint8_t length;  // number of encoded bits in wiegand message (excluding headers and preamble)
    uint64_t hi;     // bits in x<<64 positions
    uint64_t lo;     // lowest ordinal positions
} wiegand_message_t;

// Structure for unpacked wiegand card, like HID prox
typedef struct {
    uint32_t facility_code;
    uint64_t card_number;
    uint32_t issue_level;
    uint32_t oem;
    uint8_t format;
} wiegand_card_t;

typedef struct {
    bool has_parity;
    uint32_t max_fc;   // max facility code
    uint64_t max_cn;   // max cardNumber
    uint32_t max_il;   // max issue_level
    uint32_t max_oem;  // max oem
} card_format_descriptor_t;

typedef enum {
    H10301 = 1,
    IND26,
    IND27,
    INDASC27,
    TECOM27,
    W2804,
    IND29,
    ATSW30,
    ADT31,
    HCP32,
    HPP32,
    KASTLE,
    KANTECH,
    WIE32,
    D10202,
    H10306,
    N10002,
    OPTUS34,
    SMP34,
    BQT34,
    C1K35S,
    C15001,
    S12906,
    SIE36,
    H10320,
    H10302,
    H10304,
    P10004,
    HGEN37,
    MDI37,
    BQT38,
    ISCS,
    PW39,
    P10001,
    CASI40,
    BC40,
    DEFCON32,
    H800002,
    C1K48S,
    AVIG56,
    IR56,
    ACTPHID,
} card_format_t;

// Structure for defined Wiegand card formats available for packing/unpacking
typedef struct {
    card_format_t format;
    uint64_t (*pack)(wiegand_card_t *card);
    wiegand_card_t *(*unpack)(uint64_t hi, uint64_t lo);
    uint32_t bits;  // number of bits in this format
    card_format_descriptor_t fields;
} card_format_table_t;

#define WIEGAND_MATCH_MAX_FORMATS (5)

typedef struct {
    uint8_t format;
    uint8_t has_parity;
    uint8_t fixed_mismatches;
    uint64_t repacked;
} wiegand_match_entry_t;

typedef struct {
    uint8_t valid;
    uint8_t count;
    uint64_t raw;
    wiegand_match_entry_t entries[WIEGAND_MATCH_MAX_FORMATS];
} wiegand_match_info_t;

extern uint64_t pack(wiegand_card_t *card);
extern wiegand_card_t *unpack(uint8_t format_hint, uint8_t length, uint64_t hi, uint64_t lo);
extern wiegand_card_t *wiegand_card_alloc();
extern bool wiegand_get_match_info(wiegand_match_info_t *out);
