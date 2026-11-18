#include "raw_manchester.h"

uint16_t lf_raw_manchester_repetitions(uint32_t frame_ticks, uint32_t ms) {
    if (!frame_ticks || frame_ticks > 625000 || !ms || ms > 5000) return 0;
    uint32_t count = (ms * 125u) / frame_ticks;
    return count <= UINT16_MAX ? count : 0;
}

bool lf_raw_manchester_validate(const uint8_t *record, size_t length) {
    if (!record || length != LF_RAW_MANCHESTER_RECORD_SIZE || record[0] != 1 ||
            (record[1] != 32 && record[1] != 64) || record[2] > 2 ||
            !record[3] || record[3] > LF_RAW_MANCHESTER_MAX_BITS) {
        return false;
    }
    size_t bytes = (record[3] + 7) / 8;
    if (record[3] % 8 && (record[3 + bytes] & ((1u << (8 - record[3] % 8)) - 1))) {
        return false;
    }
    for (size_t i = 4 + bytes; i < length; i++) {
        if (record[i]) {
            return false;
        }
    }
    return true;
}

static void entry(lf_raw_manchester_entry_t *out, uint16_t compare, uint8_t clock) {
    *out = (lf_raw_manchester_entry_t){{compare, 0, 0}, clock};
}

size_t lf_raw_manchester_encode(const uint8_t *data, size_t data_size,
                               uint16_t bits, uint8_t clock, bool invert,
                               bool terminator, lf_raw_manchester_entry_t *out,
                               size_t capacity) {
    size_t count = bits + (terminator ? 4 : 0);
    /* Inverted terminator damping needs a separately validated electrical contract. */
    if ((invert && terminator) || !data || !out || !bits || bits > LF_RAW_MANCHESTER_MAX_BITS ||
            (clock != 32 && clock != 64) ||
            data_size != (size_t)(bits + 7) / 8 || capacity < count) {
        return 0;
    }
    if (bits % 8 && (data[data_size - 1] & ((1u << (8 - bits % 8)) - 1))) {
        return 0;
    }
    size_t offset = 0;
    if (terminator) {
        /* ATA5577C section 5.11.3: data 1, damping on, repeated twice. */
        for (size_t i = 0; i < 4; i++) {
            uint16_t compare = i % 2 ? clock : clock / 2;
            entry(&out[offset++], compare | (invert ? 0 : 0x8000), clock);
        }
    }
    for (size_t i = 0; i < bits; i++) {
        bool bit = (data[i / 8] >> (7 - i % 8)) & 1;
        entry(&out[offset++], clock / 2 | ((bit != invert) ? 0x8000 : 0), clock);
    }
    return count;
}
