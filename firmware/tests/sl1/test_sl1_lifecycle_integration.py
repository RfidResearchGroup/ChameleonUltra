"""Real tag load/resume and HF sniff integration with synthetic storage/SDK.

This executes the actual tag lifecycle and existing sniff command. Runtime
crypto is a controlled owner here; separate native event tests exercise it.
"""
import ctypes as C
from pathlib import Path
import unittest

import test_sl1_profile as profile
from test_cu_sl1_native_hooks import function

HF=profile.HF
TAG=(HF.parent/'tag_emulation.c').read_text()
APP=(HF.parents[2]/'app_cmd.c').read_text()

EXTRA_SHIM=r'''
static uint8_t host_lf_ram[20];
static uint16_t host_lf_crc;
static unsigned host_dump_reads,host_hf_loads,host_lf_loads,host_lf_sensing,host_stops,host_session;
static unsigned host_poll_calls,host_intervals,host_sniff_on_intervals,host_reload_delays;
static bool host_passive,host_loader_fail;
static unsigned host_hf_saves;
static tag_slot_config_t host_config_before;
static nfc_tag_14a_sniff_cb_t host_rx_callback;
static nfc_tag_14a_tx_sniff_cb_t host_tx_callback;
static tag_data_buffer_t m_tag_data_hf={sizeof(host_live_image),(uint8_t*)&host_live_image,&host_live_crc};
static tag_data_buffer_t m_tag_data_lf={sizeof(host_lf_ram),host_lf_ram,&host_lf_crc};
static int host_hf_load(tag_specific_type_t type,tag_data_buffer_t *buffer) {
    (void)buffer;++host_hf_loads;++host_session;if(host_loader_fail)return 0;
    host_coll.size=&host_live_image.res_coll.size;host_coll.uid=host_live_image.res_coll.uid;
    host_coll.atqa=host_live_image.res_coll.atqa;host_coll.sak=host_live_image.res_coll.sak;host_coll.ats=&host_live_image.res_coll.ats;
    const unsigned counts[]={20,64,128,256};
    return sizeof(nfc_tag_14a_coll_res_entity_t)+sizeof(nfc_tag_mf1_configure_t)+(type>=1000&&type<=1003?counts[type-1000]*16:20);
}
static int host_lf_load(tag_specific_type_t type,tag_data_buffer_t *buffer) {(void)type;(void)buffer;++host_lf_loads;return 20;}
static int host_hf_save(tag_specific_type_t type,tag_data_buffer_t *buffer) {
    (void)buffer;++host_hf_saves;const unsigned counts[]={20,64,128,256};
    return sizeof(nfc_tag_14a_coll_res_entity_t)+sizeof(nfc_tag_mf1_configure_t)+(type>=1000&&type<=1003?counts[type-1000]*16:20);
}
static int host_lf_save(tag_specific_type_t type,tag_data_buffer_t *buffer) {(void)type;(void)buffer;return 20;}
static tag_base_handler_map_t tag_base_map[]={
    {TAG_SENSE_HF,TAG_TYPE_MIFARE_Mini,host_hf_load,host_hf_save,NULL,&m_tag_data_hf},
    {TAG_SENSE_HF,TAG_TYPE_MIFARE_1024,host_hf_load,host_hf_save,NULL,&m_tag_data_hf},
    {TAG_SENSE_HF,TAG_TYPE_MIFARE_2048,host_hf_load,host_hf_save,NULL,&m_tag_data_hf},
    {TAG_SENSE_HF,TAG_TYPE_MIFARE_4096,host_hf_load,host_hf_save,NULL,&m_tag_data_hf},
    {TAG_SENSE_HF,TAG_TYPE_NTAG_213,host_hf_load,host_hf_save,NULL,&m_tag_data_hf},
    {TAG_SENSE_LF,TAG_TYPE_EM410X,host_lf_load,host_lf_save,NULL,&m_tag_data_lf}
};
#define ARRAY_SIZE(a) ARRAYLEN(a)
#define U16NTOHS(x) __builtin_bswap16(x)
bool is_tag_specific_type_valid(tag_specific_type_t type) {return type>=1000&&type<=1003;}
bool is_valid_uid_size(uint8_t size) {return size==4||size==7||size==10;}
static nfc_tag_14a_coll_res_reference_t *get_coll_res_data(bool saved) {(void)saved;return &host_coll;}
#define NRF_LOG_ERROR(...)
typedef struct {uint16_t id,key;} fds_slot_record_map_t;
static void get_fds_map_by_slot_sense_type_for_dump(uint8_t slot,tag_sense_type_t sense,fds_slot_record_map_t *map) {map->id=0x1100+slot;map->key=sense;}
void lf_tag_125khz_sense_switch(bool enable) {host_lf_sensing=enable;}
#define NRF_ERROR_INVALID_PARAM 1
#define APP_ERROR_CHECK(x) do {if(x)abort();} while(0)
#include <stdlib.h>
#define TAG_FIELD_LED_OFF() do {} while(0)
bool g_is_tag_emulating;
static void rgb_marquee_reset(void) {}
static bool m_profile_reload_in_progress;
#define HF_SNIFF_BUF_SIZE 4096
static uint8_t m_sniff_buf[HF_SNIFF_BUF_SIZE];
static uint16_t m_sniff_buf_len,m_sniff_cb_count;
static bool m_sniff_active;
typedef struct {uint16_t status,length;uint8_t data[4096];} data_frame_tx_t;
static data_frame_tx_t host_response;
static unsigned host_wipe_calls,host_reset_calls;
static bool fds_wipe(void) {++host_wipe_calls;memset(host_lengths,0,sizeof(host_lengths));return true;}
static void delayed_reset(unsigned delay) {(void)delay;++host_reset_calls;}
static data_frame_tx_t *data_frame_make(uint16_t command,uint16_t status,uint16_t length,uint8_t *data) {
    (void)command;host_response.status=status;host_response.length=length;if(length)memcpy(host_response.data,data,length);return &host_response;
}
void nfc_tag_14a_set_sniff_cb(nfc_tag_14a_sniff_cb_t cb) {host_rx_callback=cb;}
void nfc_tag_14a_set_tx_sniff_cb(nfc_tag_14a_tx_sniff_cb_t cb) {host_tx_callback=cb;}
void nfc_tag_14a_clear_sniff_cb(void) {host_rx_callback=NULL;}
void nfc_tag_14a_clear_tx_sniff_cb(void) {host_tx_callback=NULL;}
bool nfc_tag_14a_is_sniff_passive(void) {return host_passive;}
void nfc_tag_mf1_sl1_poll(void) {++host_poll_calls;if(host_poll_calls>=2)host_ready=1;}
typedef unsigned autotimer;
static unsigned host_timer;
static autotimer *bsp_obtain_timer(unsigned index) {(void)index;host_timer=0;return &host_timer;}
#define NO_TIMEOUT_1MS(timer,limit) (++*(timer) <= (limit))
static void bsp_return_timer(autotimer *timer) {(void)timer;}
static void bsp_wdt_feed(void) {}
static void bsp_delay_ms(unsigned ms) {
    if(ms!=1)++host_reload_delays;++host_intervals;
    if(host_rx_callback&&host_tx_callback&&host_sensing) {
        ++host_sniff_on_intervals;const uint8_t frame[]={0x26};
        host_rx_callback(frame,7);host_tx_callback(frame,7);
    }
}
'''

SHIM=profile.SHIM
SHIM=SHIM.replace(function(SHIM,'get_buffer_by_tag_type'),'')
SHIM=SHIM.replace(function(SHIM,'tag_emulation_load_by_buffer'),'')
for name in ('tag_emulation_profile_hf_quarantined',):
    SHIM=SHIM.replace(function(SHIM,name),'')

SHIM=SHIM.replace('    if (id >= 0x1100 && id < 0x1108 && key == TAG_SENSE_HF) return id - 0x1100;',
    '    if (id >= 0x1100 && id < 0x1108 && key == TAG_SENSE_HF) return id - 0x1100;\n    if (id >= 0x1100 && id < 0x1108 && key == TAG_SENSE_LF) return 16+id-0x1100;')
SHIM=SHIM.replace('bool fds_read_sync(uint16_t id,uint16_t key,uint16_t *length,uint8_t *buffer) {',
    'bool fds_read_sync(uint16_t id,uint16_t key,uint16_t *length,uint8_t *buffer) {\n    if(id>=0x1100&&id<0x1108)++host_dump_reads;')
# Counters must precede the FDS read double.
SHIM=SHIM.replace('static unsigned host_writes,','static unsigned host_dump_reads;\nstatic unsigned host_writes,')
EXTRA_SHIM=EXTRA_SHIM.replace('static unsigned host_dump_reads,','static unsigned ')
# Track sensing changes with a wrapper declared after the base owner doubles.
EXTRA_SHIM+='''
static void integration_sense_switch(bool enable) {if(!enable)++host_stops;nfc_tag_14a_sense_switch(enable);}
#define nfc_tag_14a_sense_switch integration_sense_switch
'''
SHIM+=EXTRA_SHIM

BRIDGE=profile.BRIDGE
for name in ('tag_emulation_load_data','tag_emulation_sense_run','tag_emulation_sense_switch','tag_emulation_profile_reload_active'):
    BRIDGE=BRIDGE.replace(function(BRIDGE,name),'')
BRIDGE+=r'''
void integration_init(void) {
    host_reset();memset(host_lf_ram,2,20);memset(host_flash[16],2,20);host_lengths[16]=20;
    host_dump_reads=host_hf_loads=host_lf_loads=host_lf_sensing=host_stops=host_session=0;
    host_wipe_calls=host_reset_calls=0;host_poll_calls=host_intervals=host_sniff_on_intervals=host_reload_delays=0;host_passive=host_loader_fail=false;host_hf_saves=0;
    host_rx_callback=host_tx_callback=NULL;m_profile_reload_in_progress=false;host_delete_result=1;
}
unsigned integration_dirty_saved(void) {
    return host_flash[0][offsetof(nfc_tag_mf1_information_t,memory)+16]==91 &&
           host_flash[0][offsetof(nfc_tag_mf1_information_t,res_coll.uid)]==93 && host_flash[16][0]==92;
}
void integration_diverge_config(void) {
    memcpy(&host_config_before,host_flash[24],sizeof(host_config_before));
    tag_emulation_set_slot(3);tag_emulation_slot_set_enable(0,TAG_SENSE_LF,false);
    slotConfig.slots[2].tag_hf=TAG_TYPE_NTAG_213;slotConfig.slots[2].enabled_hf=true;
}
unsigned integration_profile_flash_isolated(void) {
    tag_slot_config_t expected=host_config_before;expected.slots[0].tag_hf=TAG_TYPE_MIFARE_4096;expected.slots[0].enabled_hf=true;
    return !memcmp(host_flash[24],&expected,sizeof(expected));
}
unsigned integration_live_config_dirty(void) {return slotConfig.active_slot==3&&!slotConfig.slots[0].enabled_lf&&slotConfig.slots[2].tag_hf==TAG_TYPE_NTAG_213;}
void integration_save_config(void) {tag_emulation_save_config();}
void integration_remove_config(void) {host_lengths[24]=0;}
unsigned integration_flash_is_default_target(void) {
    tag_slot_config_t expected=m_default_slot_config;expected.slots[0].tag_hf=TAG_TYPE_MIFARE_4096;expected.slots[0].enabled_hf=true;
    return !memcmp(host_flash[24],&expected,sizeof(expected));
}
unsigned integration_flash_equals_live(void) {return !memcmp(host_flash[24],&slotConfig,sizeof(slotConfig));}
unsigned integration_save_calls(void) {return host_hf_saves;}
void integration_prior_type(unsigned slot,unsigned type) {
    slotConfig.slots[slot].tag_hf=type;
    if(type==1100){memset(host_flash[slot],0xA7,20);host_lengths[slot]=20;if(slot==slotConfig.active_slot)memcpy(&host_live_image,host_flash[slot],20);}
    memcpy(host_flash[24],&slotConfig,sizeof(slotConfig));host_lengths[24]=sizeof(slotConfig);
}
void integration_reboot(void) {
    cancel_stage();memset(m_last_status,0,8);memset(m_last_stage,0,8);
    memset(&host_live_image,0,sizeof(host_live_image));host_live_crc=0;
    host_runtime=host_sensing=host_fail_write=host_read_fail=host_fail_image_read=0;host_delete_result=1;host_loader_fail=false;
    m_profile_reload_in_progress=false;m_sense_pending=false;host_mode=DEVICE_MODE_TAG;
    if(host_lengths[24])memcpy(&slotConfig,host_flash[24],sizeof(slotConfig));
    tag_emulation_load_data();tag_emulation_sense_run();
}
void integration_primary_tombstone(void) {
    uint32_t words[16];uint8_t *record=(uint8_t*)words;record_init(record,0);
    write32(record+60,record_crc(record,60));fds_write_sync(0x1300,1,64,record);sl1_secure_zero(record,64);
}
void integration_remove_record(unsigned slot) {host_lengths[8+slot]=0;}
unsigned integration_config_flag(void) {return host_live_image.config.detection_enable;}
void integration_clone(void) {host_live_image.memory[1][0]=91;host_live_image.res_coll.uid[0]=93;host_lf_ram[0]=92;}
void integration_get(unsigned *out) {
    out[0]=host_live_image.memory[1][0];out[1]=host_live_image.res_coll.uid[0];out[2]=host_lf_ram[0];
    out[3]=host_live_crc;out[4]=host_lf_crc;out[5]=host_dump_reads;out[6]=host_hf_loads;out[7]=host_lf_loads;
    out[8]=host_stops;out[9]=host_session;out[10]=host_intervals;out[11]=host_sniff_on_intervals;
    out[12]=host_reload_delays;out[13]=host_poll_calls;out[14]=host_rx_callback!=NULL||host_tx_callback!=NULL;
}
void integration_stop(void) {sl1_profile_sense_request(false);}
unsigned integration_legacy_mutation(unsigned operation) {
    uint8_t type[]={0,3,0xE9},def[]={0,3,0xE8},del[]={0,2},block[17]={1};
    uint8_t identity[]={4,9,8,7,6,4,0,8,0};
    if(operation==0)return cmd_processor_set_slot_tag_type(1004,0,3,type)->status;
    if(operation==1)return cmd_processor_set_slot_data_default(1005,0,3,def)->status;
    if(operation==2)return cmd_processor_delete_slot_sense_type(1024,0,2,del)->status;
    if(operation==3)return cmd_processor_mf1_write_emu_block_data(4000,0,17,block)->status;
    return cmd_processor_hf14a_set_anti_coll_data(4001,0,sizeof(identity),identity)->status;
}
unsigned integration_wipe(void) {return cmd_processor_wipe_fds(1020,0,0,NULL)->status;}
unsigned integration_wipe_calls(void) {return host_wipe_calls+host_reset_calls;}
unsigned integration_sniff(void) {uint8_t timeout[]={0,4};return cmd_processor_hf14a_sniff(2020,0,2,timeout)->status;}
void integration_set(unsigned which,unsigned value) {if(which==0)host_passive=value;if(which==1)slotConfig.slots[0].tag_hf=value;if(which==2)slotConfig.slots[0].enabled_hf=value;if(which==3)host_live_image.config.detection_enable=value;if(which==4)host_loader_fail=value;if(which==5)host_delete_result=(int)value;}
'''
FUNCTIONS=('tag_emulation_profile_hf_quarantined','get_data_loadcb_from_tag_type','get_data_savecb_from_tag_type','get_data_factory_from_tag_type','get_buffer_by_tag_type','get_sense_type_from_tag_type',
    'tag_emulation_load_by_buffer','load_data_by_tag_type','save_data_by_tag_type','delete_data_by_tag_type','tag_emulation_delete_data','tag_emulation_save_data','tag_emulation_hf_sense','tag_emulation_sense_switch_all',
    'tag_emulation_sense_switch','tag_emulation_load_data','tag_emulation_sense_run','tag_emulation_sense_end',
    'tag_emulation_set_slot','tag_emulation_slot_set_enable','tag_emulation_save_config','tag_emulation_change_slot','tag_emulation_change_type','tag_emulation_factory_data','tag_emulation_profile_reload_active')
EXTRA='\n'.join(function(TAG,name) for name in FUNCTIONS)
EXTRA+='\n'+'\n'.join(function(APP,name) for name in (
    'invalidate_sl1_profile','cmd_processor_set_slot_tag_type','cmd_processor_set_slot_data_default','cmd_processor_delete_slot_sense_type','cmd_processor_mf1_write_emu_block_data','cmd_processor_hf14a_set_anti_coll_data','hf14a_sniff_store','hf14a_sniff_frame_cb','hf14a_sniff_tx_frame_cb','cmd_processor_hf14a_sniff','cmd_processor_wipe_fds'))


class TagLifecycleIntegrationTests(unittest.TestCase):
    HOST_SHIM=SHIM
    HOST_BRIDGE=BRIDGE
    EXTRA_SOURCE=EXTRA
    HOST_FLAGS=['-DPROJECT_CHAMELEON_ULTRA=1']
    setUpClass=classmethod(profile.ProfileTests.setUpClass.__func__)
    call=profile.ProfileTests.call
    stage=profile.ProfileTests.stage
    status=profile.ProfileTests.status
    record=profile.ProfileTests.record
    def setUp(self):self.lib.integration_init()
    def values(self):
        values=(C.c_uint*15)();self.lib.integration_get(values);return list(values)

    def test_reader_return_preserves_offline_hf_lf_clone_and_dirty_crc(self):
        self.stage();self.call('save',b'\x01\0')
        self.lib.tag_emulation_load_data()
        before=self.values();self.lib.integration_stop();self.lib.integration_clone()
        self.lib.tag_emulation_sense_run();after=self.values()
        self.assertEqual(after[:3],[91,93,92])
        self.assertEqual(after[3:5],before[3:5],'rebind must retain saved CRCs')
        self.assertEqual(after[5],before[5],'sense_run must not read either dump')
        self.assertEqual(after[7],before[7],'LF must not be reloaded or rebound')
        self.assertEqual(self.lib.host_get(1),0,'changed live image must not inherit stored AES')
        self.assertEqual(self.lib.host_get(2),1)

    def test_resumed_dirty_clone_remains_detectable_by_actual_hf_lf_save_path(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.tag_emulation_load_data();before=self.values();writes=self.lib.host_get(0)
        self.lib.integration_stop();self.lib.integration_clone();self.lib.tag_emulation_sense_run()
        self.assertEqual(self.values()[3:5],before[3:5])
        self.lib.tag_emulation_save_data()
        self.assertEqual(self.lib.host_get(0),writes+2)
        self.assertEqual(self.lib.integration_dirty_saved(),1)
        self.assertNotEqual(self.values()[3:5],before[3:5])

    def test_unchanged_live_mf1_rebinds_stored_aes_without_dump_reload(self):
        self.stage();self.call('save',b'\x01\0');before=self.values()
        self.lib.integration_stop();self.lib.tag_emulation_sense_run();after=self.values()
        self.assertEqual(after[5],before[5]);self.assertEqual(after[3],before[3])
        self.assertEqual(self.lib.host_get(1),1)

    def test_load_data_preserves_running_sensing_contract(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        self.assertEqual(self.lib.host_get(2),1)
        before=self.values();self.lib.tag_emulation_load_data();after=self.values()
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(after[8],before[8])
        self.assertEqual(after[5],before[5]+2)

    def test_other_hf_profile_resume_does_not_rebind_or_read_hf_lf(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        self.lib.integration_set(1,1100);self.lib.integration_stop();self.lib.integration_clone()
        before=self.values();self.lib.tag_emulation_sense_run();after=self.values()
        self.assertEqual(after[:3],[91,93,92]);self.assertEqual(after[5:8],before[5:8])

    def test_real_sniff_preserves_current_aes_handler_and_session_through_interval(self):
        self.stage();self.call('save',b'\x01\0');before=self.values()
        self.assertEqual(self.lib.integration_sniff(),0x68);after=self.values()
        self.assertEqual(after[10:12],[4,4]);self.assertEqual(after[12],0)
        self.assertEqual(after[5:10],before[5:10],'sniff must not load, stop, rebind or reset')
        self.assertEqual(after[14],0);self.assertEqual(self.lib.host_get(1),1)

    def test_sniff_services_pending_sensing_and_entropy_without_reloading(self):
        self.lib.host_set(5,0);self.stage();self.call('save',b'\x01\0');before=self.values()
        self.assertEqual(self.lib.host_get(2),0)
        self.assertEqual(self.lib.integration_sniff(),0x68);after=self.values()
        self.assertGreater(after[11],0);self.assertEqual(after[13],4)
        self.assertEqual(after[5:10],before[5:10]);self.assertEqual(self.lib.host_get(2),1)

    def test_real_tag_delete_failure_preserves_config_and_no_record_is_idempotent(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.integration_set(5,-1)
        self.assertEqual(self.lib.tag_emulation_delete_data(0,2),0)
        self.assertEqual(self.status()[3:5],(1000).to_bytes(2,'big'))
        self.lib.integration_set(5,0)
        self.assertEqual(self.lib.tag_emulation_delete_data(0,2),1)
        self.assertEqual(self.status()[3:5],b"\0\0")

    def test_profile_config_commit_does_not_save_unrelated_live_edits(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.integration_diverge_config()
        self.stage(kind=1003,scenario=0,enabled=0,ats=b'')
        self.assertEqual(self.call('save',b"\x01\0"),0x68)
        self.assertEqual(self.lib.integration_profile_flash_isolated(),1)
        self.assertEqual(self.lib.integration_live_config_dirty(),1)
        writes=self.lib.host_get(0);self.lib.integration_save_config()
        self.assertEqual(self.lib.host_get(0),writes+1)
        self.assertEqual(self.lib.integration_flash_equals_live(),1)

    def test_profile_config_uses_immutable_default_only_for_proven_absence(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.integration_diverge_config();self.lib.integration_remove_config()
        self.stage(kind=1003,scenario=0,enabled=0,ats=b'')
        self.assertEqual(self.call('save',b"\x01\0"),0x68)
        self.assertEqual(self.lib.integration_flash_is_default_target(),1)
        self.assertEqual(self.lib.integration_live_config_dirty(),1)

    def test_profile_config_error_duplicate_unreadable_and_short_baselines_do_not_commit(self):
        for fault in (1,2,3,4):
            self.lib.integration_init();self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
            baseline=self.record(24);self.lib.integration_diverge_config()
            self.stage(kind=1003);self.lib.host_set(23,fault)
            self.assertEqual(self.call('save',b"\x01\0"),0x70)
            self.assertEqual(self.status()[13],4)
            self.assertEqual(self.record(24),baseline)
            self.assertEqual(self.lib.integration_live_config_dirty(),1)
            self.assertEqual(self.lib.host_length(25),64)

    def test_unique_toggle_failure_phases_never_delete_credential_or_binding(self):
        for enabled in (0,1):
            for phase in (1,2):
                self.lib.integration_init();self.stage(enabled=1-enabled);self.call('save',b"\x01\0")
                original=self.record(8);self.lib.host_set(0,self.lib.host_get(0)+phase)
                self.assertEqual(self.call('set_enabled',bytes([1,0,enabled])),0x70)
                retained=self.record(8)
                self.assertEqual(len(retained),64);self.assertEqual(retained[7:60],original[7:60])
                self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),1)
                self.assertEqual(self.lib.host_get(1),0)
                if phase==2:self.assertEqual(retained[6],0)
                self.lib.host_set(0,0)
                self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x68)
                self.assertEqual(self.record(8)[7:60],original[7:60])

    def test_atomic_intermediate_commit_survives_error_and_reboot_with_key_readable(self):
        self.stage();self.call('save',b"\x01\0");original=self.record(8)
        self.lib.host_set(12,self.lib.host_get(0)+1)
        self.assertEqual(self.call('set_enabled',b"\x01\0\0"),0x70)
        retained=self.record(8);self.assertEqual(retained[6],0)
        self.assertEqual(retained[7:60],original[7:60])
        self.lib.integration_reboot();self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.status()[2]&3,1)
        self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x68)
        self.assertEqual(self.record(8)[12:60],original[12:60])

    def test_failed_toggle_retains_same_disabled_key_and_binding(self):
        self.stage();self.call('save',b"\x01\0");original=self.record(8)
        self.lib.host_set(0,self.lib.host_get(0)+2)
        self.assertEqual(self.call('set_enabled',b"\x01\0\0"),0x70)
        retained=self.record(8)
        self.assertEqual(len(retained),64)
        self.assertEqual(retained[6],0)
        self.assertEqual(retained[7:60],original[7:60])
        self.lib.host_set(0,0)
        self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x68)
        self.assertEqual(self.record(8)[12:60],original[12:60])

    def test_key1_lookup_unknown_cannot_acknowledge_clear_or_legacy_invalidation(self):
        self.stage();self.call('save',b"\x01\0");old=self.record(8);dump=self.record(0)
        self.lib.host_set(13,2)
        self.assertEqual(self.call('clear',b"\x01\0"),0x70)
        self.assertFalse(self.lib.sl1_profile_invalidate(0))
        self.assertEqual(self.record(8),old);self.assertEqual(self.record(0),dump)
        self.lib.host_set(13,0)
        self.assertEqual(self.call('clear',b"\x01\0"),0x68)
        self.lib.integration_reboot();self.assertEqual(self.lib.host_get(1),0)

    def test_key1_unknown_rejects_actual_legacy_owners_before_mutation(self):
        for operation in range(5):
            self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
            image=self.record(0);profile_record=self.record(8);before=self.values();metadata=self.status()[3:5]
            self.lib.host_set(13,2)
            self.assertEqual(self.lib.integration_legacy_mutation(operation),0x70)
            self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.record(0),image)
            self.assertEqual(self.record(8),profile_record);self.assertEqual(self.status()[3:5],metadata)
            self.assertEqual(self.lib.host_get(1),0)
            self.lib.tag_emulation_change_type(0,1003)
            self.assertEqual(self.status()[3:5],metadata)
            self.assertFalse(self.lib.tag_emulation_factory_data(0,1000))
            self.assertFalse(self.lib.tag_emulation_delete_data(0,2))
            self.lib.tag_emulation_save_data();self.assertEqual(self.record(0),image)
            self.lib.host_set(13,0);self.assertEqual(self.call('clear',b"\x01\0"),0x68)
            self.lib.integration_reboot();self.assertEqual(self.lib.host_get(1),0)

    def test_duplicate_key1_cannot_revive_after_successful_clear(self):
        self.stage();self.call('save',b"\x01\0");self.lib.host_set(21,1)
        self.assertEqual(self.call('clear',b"\x01\0"),0x68)
        self.lib.host_set(22,1);self.lib.integration_reboot()
        self.assertEqual(self.lib.host_get(1),0)

    def test_tombstone_with_latent_duplicate_and_disable_ambiguity_cannot_acknowledge_revocation(self):
        self.stage();self.call('save',b"\x01\0");self.lib.host_set(21,1)
        self.lib.integration_primary_tombstone()
        self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),2)
        self.lib.host_set(22,1);self.lib.integration_reboot()
        self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.status()[2]&0x40,0)
        self.assertEqual(self.call('set_enabled',b"\x01\0\0"),0x75)
        self.assertEqual(self.call('clear',b"\x01\0"),0x68)
        self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),1)
        self.lib.integration_reboot();self.assertEqual(self.lib.host_get(1),0)

    def test_duplicate_deletion_failure_is_not_acknowledged_and_retry_canonicalizes(self):
        self.stage();self.call('save',b"\x01\0");self.lib.host_set(21,1)
        self.lib.integration_set(5,-1)
        self.assertEqual(self.call('clear',b"\x01\0"),0x70)
        self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),2)
        self.assertEqual(self.lib.host_get(1),0)
        self.lib.integration_set(5,1)
        self.assertEqual(self.call('clear',b"\x01\0"),0x68)
        self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),1)

    def test_full_save_replaces_duplicate_key1_with_one_verified_record(self):
        self.stage();self.call('save',b"\x01\0");self.lib.host_set(21,1)
        self.stage(kind=1003)
        self.assertEqual(self.call('save',b"\x01\0"),0x68)
        self.assertEqual(self.lib.fds_record_count_sync(0x1300,1),1)
        self.lib.host_set(22,1);self.lib.integration_reboot()
        self.assertEqual(self.lib.host_get(1),1)

    def test_config_readback_failure_keeps_old_ram_config_and_quarantine(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.stage(kind=1003);self.lib.host_set(20,1)
        self.assertEqual(self.call('save',b"\x01\0"),0x70)
        self.assertEqual(self.status()[13],4);self.assertEqual(self.lib.host_length(25),64)
        self.assertEqual(self.lib.host_get(2),0)

    def test_incomplete_status_bit_is_distinct_after_reboot_and_lookup_error(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.stage(kind=1003);self.lib.host_set(0,self.lib.host_get(0)+2)
        self.assertEqual(self.call('save',b"\x01\0"),0x70)
        self.lib.integration_reboot();self.assertEqual(self.status()[2],0x50)
        self.lib.integration_init();self.lib.host_set(13,4)
        self.assertEqual(self.status()[2],0x50)
        self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
        self.lib.host_corrupt(8,60);self.lib.integration_reboot()
        self.assertEqual(self.status()[2],0x10)
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(self.lib.host_get(1),0)

    def test_marker_readback_failure_precedes_any_image_config_or_key1_mutation(self):
        for setting,value in ((14,2),(15,2),(18,2),(13,4)):
            self.lib.integration_init();self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
            old_dump=self.record(0);old_config=self.record(24);old_profile=self.record(8)
            self.stage(kind=1003);self.lib.host_set(setting,value)
            self.assertEqual(self.call('save',b"\x01\0"),0x71)
            self.assertEqual(self.status()[13],2)
            self.assertEqual(self.record(0),old_dump);self.assertEqual(self.record(24),old_config)
            self.assertEqual(self.record(8),old_profile);self.assertEqual(self.lib.host_length(25),64)

    def test_final_profile_readback_and_binding_failure_keep_marker(self):
        for setting in (14,15,18,19,13):
            self.lib.integration_init();self.stage()
            self.lib.host_set(setting,2 if setting==13 else 1)
            self.assertEqual(self.call('save',b"\x01\0"),0x70 if setting==13 else 0x71)
            self.assertEqual(self.status()[13],5);self.assertEqual(self.lib.host_length(25),64)
            self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.lib.host_get(1),0)

    def test_unknown_lookup_and_unproven_marker_removal_never_release_hf(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.integration_stop();self.lib.host_set(13,4)
        self.lib.tag_emulation_sense_run();self.assertEqual(self.lib.host_get(2),0)
        self.assertEqual(self.call('clear',b"\x01\0"),0x75)
        for setting in (16,17):
            self.lib.integration_init();self.stage();self.lib.host_set(setting,1)
            self.assertEqual(self.call('save',b"\x01\0"),0x70)
            self.assertEqual(self.status()[13],5)
            self.lib.tag_emulation_sense_run();self.assertEqual(self.lib.host_get(2),0)
            self.assertEqual(self.lib.host_get(1),0)

    def test_pending_or_unknown_factory_wipe_is_rejected_before_any_record_or_reset(self):
        self.stage();self.lib.integration_set(5,-1);self.call('save',b"\x01\0")
        marker=self.record(25);lf=self.record(16)
        self.assertEqual(self.lib.integration_wipe(),0x75)
        self.assertEqual(self.lib.integration_wipe_calls(),0)
        self.assertEqual(self.record(25),marker);self.assertEqual(self.record(16),lf)
        self.lib.integration_init();self.lib.host_set(13,4)
        self.assertEqual(self.lib.integration_wipe(),0x75);self.assertEqual(self.lib.integration_wipe_calls(),0)
        self.lib.host_set(13,0);self.assertEqual(self.lib.integration_wipe(),0x68)

    def test_durable_quarantine_survives_phases_geometries_non_mf1_slots_and_reboot(self):
        for target in (0,1):
            for prior in (1000,1003,1100):
                for phase in (2,3,4):
                    with self.subTest(target=target,prior=prior,phase=phase):
                        self.lib.integration_init()
                        self.stage(slot=target,kind=1000 if prior==1100 else prior,scenario=0,enabled=0,ats=b'')
                        self.call('save',bytes([1,target]));self.lib.integration_prior_type(target,prior)
                        new_type=1000 if prior==1003 else 1003
                        self.stage(slot=target,kind=new_type)
                        self.lib.host_set(0,self.lib.host_get(0)+phase)
                        self.assertEqual(self.call('save',bytes([1,target])),0x70)
                        self.assertEqual(self.status(target)[13],phase+1)
                        dump=self.record(target);lf=self.record(16)
                        self.assertEqual(self.lib.host_length(25+target),64)
                        self.lib.host_set(0,0);calls=self.lib.integration_save_calls()
                        self.lib.tag_emulation_save_data()
                        if target==0:self.assertEqual(self.lib.integration_save_calls(),calls)
                        self.assertEqual(self.record(target),dump)
                        self.lib.integration_reboot();self.lib.tag_emulation_change_slot(target,True)
                        self.assertEqual(self.lib.host_get(2),0)
                        self.assertEqual(self.status(target)[2]&16,16)
                        self.assertEqual(self.call('clear',bytes([1,target])),0x75)
                        self.assertFalse(self.lib.sl1_profile_invalidate(target))
                        self.lib.tag_emulation_save_data();self.assertEqual(self.record(target),dump)
                        self.assertEqual(self.record(16),lf)
                        self.stage(slot=target,kind=new_type)
                        self.assertEqual(self.call('save',bytes([1,target])),0x68)
                        self.assertEqual(self.lib.host_length(25+target),0)
                        self.assertEqual(self.lib.host_get(2),1)

    def test_false_image_terminal_result_can_have_committed_bytes_without_losing_quarantine(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.stage(kind=1003);self.lib.host_set(12,self.lib.host_get(0)+2)
        self.assertEqual(self.call('save',b"\x01\0"),0x70)
        self.assertEqual(self.status()[13],3);dump=self.record(0)
        self.assertGreater(len(dump),4000);self.assertEqual(self.lib.host_length(25),64)
        self.lib.tag_emulation_change_slot(1,True);self.assertEqual(self.record(0),dump)
        self.lib.integration_reboot();self.lib.tag_emulation_change_slot(0,True)
        self.assertEqual(self.lib.host_get(2),0)

    def test_inactive_partial_commit_preserves_other_active_hf_lf_and_runtime(self):
        self.stage();self.call('save',b"\x01\0");before=self.values()
        active=self.record(0);lf=self.record(16)
        self.stage(slot=1,kind=1003);self.lib.host_set(0,self.lib.host_get(0)+3)
        self.assertEqual(self.call('save',b"\x01\x01"),0x70)
        self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.record(0),active)
        self.assertEqual(self.record(16),lf);self.assertEqual(self.lib.host_get(1),1)
        self.assertEqual(self.lib.host_get(2),1)
        self.lib.host_set(0,0);self.stage(slot=1,kind=1003)
        self.assertEqual(self.call('save',b"\x01\x01"),0x68)
        self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(1),1)

    def test_marker_corruption_and_delete_failure_remain_quarantined_until_full_retry(self):
        self.stage();self.lib.integration_set(5,-1)
        self.assertEqual(self.call('save',b"\x01\0"),0x70)
        self.assertEqual(self.status()[13],5);self.assertEqual(self.lib.host_length(25),64)
        self.lib.host_corrupt(25,0);self.lib.integration_reboot()
        self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.status()[2]&16,16)
        self.assertEqual(self.call('clear',b"\x01\0"),0x75)
        self.stage();self.assertEqual(self.call('save',b"\x01\0"),0x68)
        self.assertEqual(self.lib.host_length(25),0)

    def test_reload_failure_keeps_durable_marker_and_blocks_later_legacy_save(self):
        self.stage(kind=1003);self.lib.integration_set(4,1)
        self.assertEqual(self.call('save',b"\x01\0"),0x71)
        self.assertEqual(self.status()[13],6);dump=self.record(0)
        self.lib.tag_emulation_save_data();self.assertEqual(self.record(0),dump)
        self.lib.integration_reboot();self.assertEqual(self.lib.host_get(2),0)
        self.stage(kind=1003);self.assertEqual(self.call('save',b"\x01\0"),0x68)

    def test_partial_profile_commit_cannot_be_overwritten_by_real_slot_switch_save(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b"\x01\0")
        self.lib.integration_clone()
        self.stage(kind=1003);self.lib.host_set(0,self.lib.host_get(0)+4)
        self.assertEqual(self.call('save',b"\x01\0"),0x70)
        committed=self.record(0)
        self.lib.host_set(0,0)
        self.lib.tag_emulation_change_slot(1,True)
        self.assertEqual(self.record(0),committed)

    def test_clear_preserves_dirty_hf_lf_crc_for_absent_tombstone_disabled_and_configured(self):
        for state in ('absent','tombstone','disabled','configured','corrupt'):
            with self.subTest(state=state):
                self.lib.integration_init()
                if state in ('absent','tombstone'):
                    self.stage(scenario=0,enabled=0,ats=b'')
                else:self.stage(enabled=int(state!='disabled'))
                self.call('save',b"\x01\0");self.lib.tag_emulation_load_data()
                if state=='absent':self.lib.integration_remove_record(0)
                if state=='corrupt':self.lib.host_corrupt(8,60)
                self.lib.integration_clone();before=self.values()
                self.assertEqual(self.call('clear',b"\x01\0"),0x68)
                after=self.values()
                self.assertEqual(after[:8],before[:8])
                self.assertEqual(self.lib.host_get(1),0)
                self.assertEqual(self.lib.host_get(2),1)
                writes=self.lib.host_get(0);self.lib.tag_emulation_save_data()
                self.assertEqual(self.lib.host_get(0),writes+2)
                self.assertEqual(self.lib.integration_dirty_saved(),1)

    def test_clear_pending_profile_resumes_classic_without_loading_dirty_ram(self):
        self.lib.host_set(5,0);self.stage();self.call('save',b"\x01\0")
        self.lib.integration_clone();before=self.values()
        self.assertEqual(self.lib.host_get(2),0)
        self.assertEqual(self.call('clear',b"\x01\0"),0x68)
        self.assertEqual(self.values()[:8],before[:8])
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.lib.host_get(2),1)
        self.assertEqual(self.lib.host_get(5),0)

    def test_disable_dirty_profile_preserves_ram_and_enable_mismatch_fails_inert(self):
        self.stage();self.call('save',b"\x01\0");self.lib.tag_emulation_load_data()
        self.lib.integration_clone();before=self.values()
        self.assertEqual(self.call('set_enabled',b"\x01\0\0"),0x68)
        after=self.values();self.assertEqual(after[:5],before[:5]);self.assertEqual(after[6:10],before[6:10])
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.lib.host_get(2),1)
        writes=self.lib.host_get(0)
        self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x75)
        self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(0),writes)
        self.assertEqual(self.lib.host_get(1),0)
        self.lib.tag_emulation_save_data();self.assertEqual(self.lib.integration_dirty_saved(),1)

    def test_enable_rebinds_matching_live_image_preserving_dirty_config_crc_and_lf(self):
        self.stage(enabled=0);self.call('save',b"\x01\0");self.lib.tag_emulation_load_data()
        self.lib.integration_set(3,1);before=self.values()
        self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x68)
        after=self.values();self.assertEqual(after[:5],before[:5])
        self.assertEqual(after[7],before[7]);self.assertEqual(self.lib.integration_config_flag(),1)
        self.assertEqual(self.lib.host_get(1),1);self.assertEqual(self.lib.host_get(2),1)

    def test_control_flash_failures_preserve_dirty_data_and_leave_aes_off(self):
        for command,payload,phase in (('clear',b"\x01\0",1),('set_enabled',b"\x01\0\0",1),('set_enabled',b"\x01\0\0",2)):
            self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
            self.lib.integration_clone();before=self.values()
            self.lib.host_set(0,self.lib.host_get(0)+phase)
            self.assertEqual(self.call(command,payload),0x70)
            self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(1),0)
            self.assertEqual(self.lib.host_get(2),1)

    def test_controls_reject_reader_and_field_without_changing_live_data(self):
        for flag in (1,2):
            self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
            self.lib.integration_clone();before=self.values();writes=self.lib.host_get(0)
            self.lib.host_set(flag,1)
            self.assertEqual(self.call('clear',b"\x01\0"),0x75)
            self.assertEqual(self.call('set_enabled',b"\x01\0\0"),0x75)
            self.assertEqual(self.values(),before);self.assertEqual(self.lib.host_get(0),writes)

    def test_enable_absent_tombstone_corrupt_or_wrong_type_cannot_overwrite_live_data(self):
        for state in ('absent','tombstone','corrupt','wrong_type'):
            self.lib.integration_init()
            if state in ('absent','tombstone'):self.stage(scenario=0,enabled=0,ats=b'')
            else:self.stage()
            self.call('save',b"\x01\0")
            if state=='absent':self.lib.integration_remove_record(0)
            if state=='corrupt':self.lib.host_corrupt(8,60)
            if state=='wrong_type':self.lib.integration_set(1,1100)
            self.lib.integration_clone();before=self.values();writes=self.lib.host_get(0)
            self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x75)
            self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(0),writes)
            self.assertEqual(self.lib.host_get(1),0)

    def test_enable_preserves_disabled_hf_and_stopped_run_intent(self):
        for disabled in (False,True):
            self.lib.integration_init();self.stage(enabled=0);self.call('save',b"\x01\0")
            self.lib.integration_stop()
            if disabled:self.lib.integration_set(2,0)
            before=self.values()
            self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x68)
            self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(2),0)
            self.assertEqual(self.lib.host_get(1),0)
            self.assertEqual(self.status()[2]&3,3)

    def test_control_record_read_failure_preserves_live_data_and_fails_enable_inert(self):
        for command,payload,expected in (('clear',b"\x01\0",0x70),('set_enabled',b"\x01\0\x01",0x75)):
            self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
            self.lib.integration_clone();before=self.values();self.lib.host_set(4,1)
            self.assertEqual(self.call(command,payload),expected)
            self.assertEqual(self.values()[:5],before[:5])
            self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.lib.host_get(2),1)

    def test_pending_toggle_controls_preserve_run_intent_without_ram_reload(self):
        for enabled in (0,1):
            self.lib.integration_init();self.lib.host_set(5,0)
            self.stage();self.call('save',b"\x01\0");before=self.values()
            self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.lib.host_get(5),1)
            self.assertEqual(self.call('set_enabled',bytes([1,0,enabled])),0x68)
            self.assertEqual(self.values()[:5],before[:5])
            self.assertEqual(self.lib.host_get(1),enabled)
            self.assertEqual(self.lib.host_get(2),int(not enabled))
            self.assertEqual(self.lib.host_get(5),enabled)
            self.lib.host_set(5,1);self.lib.sl1_profile_poll()
            self.assertEqual(self.lib.host_get(2),1)

    def test_nonactive_toggle_does_not_change_current_dirty_buffers_or_runtime(self):
        self.stage();self.call('save',b"\x01\0")
        self.stage(slot=1);self.call('save',b"\x01\x01")
        self.lib.integration_clone();before=self.values();runtime=self.lib.host_get(1)
        self.assertEqual(self.call('set_enabled',b"\x01\x01\0"),0x68)
        self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.values()[6:10],before[6:10])
        self.assertEqual(self.lib.host_get(1),runtime)
        self.assertEqual(self.lib.host_get(2),1)

    def test_clear_does_not_restart_disabled_or_other_hf_owner(self):
        for condition in ('disabled','other_type'):
            self.lib.integration_init();self.stage();self.call('save',b"\x01\0")
            self.lib.integration_stop()
            if condition=='disabled':self.lib.integration_set(2,0)
            else:self.lib.integration_set(1,1100)
            self.lib.integration_clone();before=self.values()
            self.assertEqual(self.call('clear',b"\x01\0"),0x68)
            self.assertEqual(self.values()[:8],before[:8])
            self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.lib.host_get(1),0)

    def test_enable_bind_failure_is_truthful_and_preserves_live_ram(self):
        self.stage(enabled=0);self.call('save',b"\x01\0");before=self.values()
        self.lib.integration_set(4,1)
        self.assertEqual(self.call('set_enabled',b"\x01\0\x01"),0x75)
        self.assertEqual(self.values()[:5],before[:5]);self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(self.status()[12:14],b"\x75\x06")

    def test_pending_invalidation_does_not_start_disabled_wrong_type_or_non_tag_owner(self):
        for condition in ('disabled','other_type','none_mode'):
            self.lib.integration_init();self.lib.host_set(5,0)
            self.stage();self.call('save',b"\x01\0")
            if condition=='disabled':self.lib.integration_set(2,0)
            elif condition=='other_type':self.lib.integration_set(1,1100)
            else:self.lib.host_set(2,0)
            self.assertTrue(self.lib.sl1_profile_invalidate(0))
            self.lib.sl1_profile_poll()
            self.assertEqual(self.lib.host_get(2),0)
            self.assertEqual(self.lib.host_get(1),0)

    def test_sniff_refuses_reader_or_passive_owner_without_side_effect(self):
        self.stage();self.call('save',b'\x01\0');before=self.values()
        self.lib.host_set(2,1);self.assertEqual(self.lib.integration_sniff(),0x75)
        self.lib.host_set(2,2);self.lib.integration_set(0,1)
        self.assertEqual(self.lib.integration_sniff(),0x75);self.assertEqual(self.values(),before)


class LiteTagResumeIntegrationTests(unittest.TestCase):
    HOST_SHIM=SHIM
    HOST_BRIDGE=BRIDGE
    EXTRA_SOURCE=EXTRA
    HOST_FLAGS=[]
    setUpClass=classmethod(profile.ProfileTests.setUpClass.__func__)
    call=profile.ProfileTests.call
    stage=profile.ProfileTests.stage
    status=profile.ProfileTests.status
    values=TagLifecycleIntegrationTests.values
    def setUp(self):self.lib.integration_init()

    def test_lite_resume_keeps_live_hf_lf_and_saved_crc_without_rebind(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        self.lib.integration_stop();self.lib.integration_clone();before=self.values()
        self.lib.tag_emulation_sense_run();after=self.values()
        self.assertEqual(after[:3],[91,93,92]);self.assertEqual(after[3:8],before[3:8])
        self.assertEqual(self.lib.host_get(2),1)

    def test_lite_pure_load_keeps_sensing_enabled(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        before=self.values();self.lib.tag_emulation_load_data();after=self.values()
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(after[8],before[8])
        self.assertEqual(after[5],before[5]+2)

if __name__=='__main__':unittest.main()
