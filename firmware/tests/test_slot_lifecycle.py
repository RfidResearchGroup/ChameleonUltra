"""Compile actual lifecycle/save functions against fault-injected host stubs.

No SDK or radio simulation: verifies ordering, status and retry bookkeeping only.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    match = re.search(r"^.*\b" + name + r"\([^;\n]*\) \{", source, re.MULTILINE)
    start = source.index("{", match.start())
    line = source.rfind("\n", 0, start) + 1
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[line:end]


class SlotLifecycle(unittest.TestCase):
    def test_real_functions_with_faults(self):
        source = (ROOT / "application/src/rfid/nfctag/tag_emulation.c").read_text()
        command_source = (ROOT / "application/src/app_cmd.c").read_text()
        main_source = (ROOT / "application/src/app_main.c").read_text()
        lf_source = (ROOT / "application/src/rfid/nfctag/lf/lf_tag_em.c").read_text()
        # Remove prototypes so extraction always selects definitions.
        source = source.replace("static bool tag_emulation_save_config(void);", "")
        names = ["save_data_by_tag_type", "tag_emulation_save_data",
                 "tag_emulation_save_config", "tag_emulation_save",
                 "tag_emulation_slot_set_enable", "tag_emulation_change_type",
                  "tag_emulation_factory_data", "tag_emulation_change_slot",
                  "is_slot_enabled"]
        code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "raw_manchester.h"
#define NRF_LOG_INFO(...) ((void)0)
#define NRF_LOG_ERROR(...) ((void)0)
#define TAG_TYPE_UNDEFINED 0
#define TAG_TYPE_RAW_MANCHESTER 100
#define TAG_SENSE_NO 0
#define TAG_SENSE_LF 1
#define TAG_SENSE_HF 2
#define DEVICE_MODE_READER 1
#define TAG_MAX_SLOT_NUM 2
#define STATUS_PAR_ERR 8
#define PACKED __attribute__((packed))
#define RGB_MAGENTA 3
#define RGB_LIST_NUM 1
#define FDS_EMULATION_CONFIG_FILE_ID 1
#define FDS_EMULATION_CONFIG_RECORD_KEY 1
typedef int tag_specific_type_t;
typedef int tag_sense_type_t;
typedef int device_mode_t;
typedef struct { int length; uint8_t *buffer; uint16_t *crc; } tag_data_buffer_t;
typedef int (*tag_datas_savecb_t)(int, tag_data_buffer_t *);
typedef bool (*tag_datas_factory_t)(uint8_t, int);
typedef struct { int id, key; } fds_slot_record_map_t;
static struct { uint8_t active_slot; struct { int tag_hf, tag_lf; bool enabled_hf, enabled_lf; } slots[2]; } slotConfig;
static uint16_t m_slot_config_crc, data_crc;
static uint8_t bytes[32];
static tag_data_buffer_t buffer = {32, bytes, &data_crc};
static bool fail_write, fail_factory, sensing;
static int writes, loads, stops, starts, mode;
static bool g_is_tag_emulating;
static int m_tag_type, persisted_length;
static bool persisted_exists;
static uint8_t persisted[32];
static void lf_tag_data_clear(void) { m_tag_type = 0; }
static bool fds_read_sync(int id, int key, uint16_t *length, uint8_t *out) {
    if (!persisted_exists) return false;
    *length = persisted_length;
    memcpy(out, persisted, persisted_length);
    return true;
}
static bool tag_emulation_load_by_buffer(int type, bool update_crc) {
    if (!lf_raw_manchester_validate(bytes, sizeof(bytes))) return false;
    m_tag_type = type;
    if (update_crc) data_crc = 123;
    return true;
}
int lf_tag_raw_manchester_data_savecb(int type, tag_data_buffer_t *b);
static int applied, led_changes;
static uint8_t tag_emulation_get_slot(void) { return slotConfig.active_slot; }
static void tag_emulation_set_slot(uint8_t slot) { slotConfig.active_slot = slot; }
static void tag_emulation_sense_end(void) { sensing = false; stops++; }
static void tag_emulation_sense_run(void) { sensing = slotConfig.slots[slotConfig.active_slot].enabled_lf || slotConfig.slots[slotConfig.active_slot].enabled_hf; starts++; }
static void tag_emulation_load_data(void) { assert(!sensing); loads++; bytes[0] = 99; }
static void apply_slot_change(int old, int next) { applied++; }
static uint8_t tag_emulation_slot_find_next(uint8_t slot) { return 1 - slot; }
static uint8_t tag_emulation_slot_find_prev(uint8_t slot) { return 1 - slot; }
static void rgb_marquee_stop(void) { led_changes++; }
static uint32_t *hw_get_led_array(void) { static uint32_t pins[1]; return pins; }
static void nrf_gpio_pin_clear(uint32_t pin) { led_changes++; }
static void set_slot_light_color(int color) { led_changes++; }
static int get_device_mode(void) { return mode; }
static int get_sense_type_from_tag_type(int type) { return type ? TAG_SENSE_LF : TAG_SENSE_NO; }
static tag_data_buffer_t *get_buffer_by_tag_type(int type) { return &buffer; }
static int savecb(int type, tag_data_buffer_t *b) { return 32; }
static tag_datas_savecb_t get_data_savecb_from_tag_type(int type) { return type == TAG_TYPE_RAW_MANCHESTER ? lf_tag_raw_manchester_data_savecb : savecb; }
static void calc_14a_crc_lut(uint8_t *b, int n, uint8_t *out) { *(uint16_t *)out = 123; }
static void get_fds_map_by_slot_sense_type_for_dump(int slot, int sense, fds_slot_record_map_t *map) { map->id = 2; map->key = 2; }
static bool fds_write_sync(int id, int key, int n, uint8_t *b) { writes++; return !fail_write; }
static void tag_emulation_sense_switch(int sense, bool enable) { sensing = enable; if (enable) starts++; else stops++; }
static void load_data_by_tag_type(uint8_t slot, int type);
static bool factory(uint8_t slot, int type) { if (!slot) assert(!sensing); return !fail_factory; }
static tag_datas_factory_t get_data_factory_from_tag_type(int type) { return factory; }
typedef struct { int status; } data_frame_tx_t;
#define STATUS_SUCCESS 0
#define STATUS_FLASH_WRITE_FAIL 9
static data_frame_tx_t response;
static data_frame_tx_t *data_frame_make(int cmd, int status, int length, void *data) { response.status = status; return &response; }
'''
        code += function(lf_source, "lf_tag_raw_manchester_data_savecb")
        code += function(source, "load_data_by_tag_type")
        code += "\n".join(function(source, name) for name in names)
        code += function(command_source, "cmd_processor_slot_data_config_save")
        for name in ["change_slot_auto", "cmd_processor_set_active_slot", "cmd_processor_set_slot_enable"]:
            code += function(command_source, name)
        code += function(main_source, "cycle_slot")
        code += r'''
int main(void) {
    slotConfig.slots[0].tag_lf = 7;
    fail_write = true;
    assert(cmd_processor_slot_data_config_save(0, 0, 0, NULL)->status == STATUS_FLASH_WRITE_FAIL);
    writes = 0;
    assert(!tag_emulation_save());
    assert(writes == 2 && data_crc == 0 && m_slot_config_crc == 0);
    fail_write = false;
    assert(tag_emulation_save());
    assert(writes == 4 && data_crc == 123 && m_slot_config_crc == 123);
    assert(tag_emulation_save() && writes == 4);
    assert(cmd_processor_slot_data_config_save(0, 0, 0, NULL)->status == STATUS_SUCCESS);
    tag_emulation_slot_set_enable(0, TAG_SENSE_LF, true);
    assert(sensing && starts == 1);
    tag_emulation_change_type(1, 7);
    assert(loads == 0 && sensing && stops == 0);
    tag_emulation_change_type(0, 7);
    assert(loads == 0 && sensing && stops == 1 && starts == 2);
    assert(tag_emulation_factory_data(0, 7));
    assert(loads == 0 && sensing && stops == 2 && starts == 3);
    fail_factory = true;
    assert(!tag_emulation_factory_data(0, 7));
    assert(loads == 0 && sensing && stops == 3 && starts == 4);
    mode = DEVICE_MODE_READER;
    sensing = false;
    tag_emulation_change_type(0, 7);
    tag_emulation_slot_set_enable(0, TAG_SENSE_LF, true);
    assert(!sensing && starts == 4);
    // Failed switches must keep the active slot, dirty bytes and emulation flag.
    mode = 0;
    sensing = true;
    g_is_tag_emulating = true;
    data_crc = 0;
    bytes[0] = 42;
    fail_write = true;
    int old_loads = loads;
    uint8_t next = 1;
    assert(cmd_processor_set_active_slot(0, 0, 1, &next)->status == STATUS_FLASH_WRITE_FAIL);
    assert(slotConfig.active_slot == 0 && sensing && bytes[0] == 42);
    assert(loads == old_loads && applied == 0 && g_is_tag_emulating);
    cycle_slot(false);
    cycle_slot(true);
    assert(slotConfig.active_slot == 0 && sensing && bytes[0] == 42);
    assert(loads == old_loads && applied == 0 && led_changes == 0);
    uint8_t disable[] = {0, TAG_SENSE_LF, 0};
    assert(cmd_processor_set_slot_enable(0, 0, sizeof(disable), disable)->status == STATUS_FLASH_WRITE_FAIL);
    assert(slotConfig.slots[0].enabled_lf && sensing && slotConfig.active_slot == 0);
    assert(loads == old_loads && applied == 0 && bytes[0] == 42);
    mode = DEVICE_MODE_READER;
    sensing = false;
    int old_starts = starts, old_stops = stops;
    assert(cmd_processor_set_active_slot(0, 0, 1, &next)->status == STATUS_FLASH_WRITE_FAIL);
    assert(!sensing && starts == old_starts && stops == old_stops);
    assert(slotConfig.active_slot == 0 && bytes[0] == 42 && g_is_tag_emulating);
    // Retry actually saves and switches; reader mode must remain unsensed.
    fail_write = false;
    assert(cmd_processor_set_active_slot(0, 0, 1, &next)->status == STATUS_SUCCESS);
    assert(slotConfig.active_slot == 1 && loads == old_loads + 1 && applied == 1);
    assert(!sensing && !g_is_tag_emulating && bytes[0] == 99);
    mode = 0;
    cycle_slot(false);
    assert(slotConfig.active_slot == 0 && sensing && applied == 2);
    // Real persisted RAW loader: missing, truncated and invalid records must not trap a slot.
    for (int fault = 0; fault < 3; fault++) {
        for (int button = 0; button < 3; button++) {
            slotConfig.active_slot = 0;
            slotConfig.slots[0].tag_lf = TAG_TYPE_RAW_MANCHESTER;
            slotConfig.slots[1].tag_lf = 7;
            slotConfig.slots[1].enabled_lf = true;
            sensing = false;
            persisted_exists = fault != 0;
            persisted_length = fault == 1 ? 4 : 32;
            memset(persisted, 0, sizeof(persisted));
            persisted[0] = fault == 2 ? 99 : 1;
            persisted[1] = 32; persisted[3] = 8; persisted[4] = 0xAA;
            m_tag_type = TAG_TYPE_RAW_MANCHESTER;
            data_crc = 456;
            load_data_by_tag_type(0, TAG_TYPE_RAW_MANCHESTER);
            assert(m_tag_type == 0 && data_crc == 456);
            int old_writes = writes;
            fail_write = true;
            if (button) cycle_slot(button == 2);
            else assert(cmd_processor_set_active_slot(0, 0, 1, &next)->status == STATUS_SUCCESS);
            assert(slotConfig.active_slot == 1 && writes == old_writes);
            assert(data_crc == 456);
        }
    }
    // Valid RAW dirty bytes still block both command and buttons on FDS failure.
    slotConfig.active_slot = 0;
    sensing = false;
    persisted_exists = true; persisted_length = 32; persisted[0] = 1;
    load_data_by_tag_type(0, TAG_TYPE_RAW_MANCHESTER);
    assert(m_tag_type == TAG_TYPE_RAW_MANCHESTER && data_crc == 123);
    bytes[4] ^= 0x80;
    data_crc = 456;
    int raw_writes = writes;
    assert(cmd_processor_set_active_slot(0, 0, 1, &next)->status == STATUS_FLASH_WRITE_FAIL);
    cycle_slot(false); cycle_slot(true);
    assert(slotConfig.active_slot == 0 && data_crc == 456 && writes == raw_writes + 3);
    // Invalid mutation of loaded data is an error, not an empty slot.
    bytes[0] = 99;
    assert(!tag_emulation_save_data() && slotConfig.active_slot == 0);
    bytes[0] = 1;
    fail_write = false;
    assert(tag_emulation_save_data() && data_crc == 123);
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            cfile = Path(tmp) / "lifecycle.c"
            binary = Path(tmp) / "lifecycle"
            cfile.write_text(code)
            protocol = ROOT / "application/src/rfid/nfctag/lf/protocols"
            subprocess.run(["cc", "-std=c11", "-I", str(protocol), str(cfile),
                            str(protocol / "raw_manchester.c"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
