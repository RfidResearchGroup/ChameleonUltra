"""Actual profile transaction and config helper with synthetic flash failures.

FDS and RF are in-memory host shims. SHA-256 uses Python hashlib independently
of the firmware SDK. No hardware or firmware build is invoked.
"""
import ctypes as C
import hashlib
import zlib
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_cu_sl1_native_hooks import function

ROOT = Path(__file__).resolve().parents[3]
HF = ROOT / 'firmware/application/src/rfid/nfctag/hf'

SHIM = r'''

typedef uint32_t ret_code_t;
#define NRF_SUCCESS 0
#define DEVICE_MODE_READER 1
#define DEVICE_MODE_TAG 2
#define NRF_LOG_INFO(...)
#define NRF_LOG_HEXDUMP_INFO(...)
#define ARRAYLEN(a) (sizeof(a)/sizeof((a)[0]))
typedef struct { uint8_t data[4500]; size_t used; } nrf_crypto_hash_context_t;
static void (*host_hash_callback)(const uint8_t *,size_t,uint8_t *);
void host_set_hash(void (*cb)(const uint8_t *,size_t,uint8_t *)) {host_hash_callback=cb;}
static const unsigned g_nrf_crypto_hash_sha256_info;
static unsigned nrf_crypto_hash_init(nrf_crypto_hash_context_t *c,const void *i) {(void)i;c->used=0;return 0;}
static unsigned nrf_crypto_hash_update(nrf_crypto_hash_context_t *c,const uint8_t *p,size_t n) {
    if(c->used+n>sizeof(c->data))return 1;memcpy(c->data+c->used,p,n);c->used+=n;return 0;
}
static unsigned nrf_crypto_hash_finalize(nrf_crypto_hash_context_t *c,uint8_t *out,size_t *n) {
    if(!host_hash_callback)return 1;*n=32;host_hash_callback(c->data,c->used,out);return 0;
}
static tag_specific_type_t tag_specific_type_old2new_lf_values[][2] = {TAG_SPECIFIC_TYPE_OLD2NEW_LF_VALUES};
static tag_specific_type_t tag_specific_type_old2new_hf_values[][2] = {TAG_SPECIFIC_TYPE_OLD2NEW_HF_VALUES};
static tag_slot_config_t slotConfig;
static uint16_t m_slot_config_crc;
static unsigned host_mode, host_field, host_runtime, host_ready, host_sensing, host_reloads;
static unsigned host_writes, host_fail_write, host_read_fail, host_fail_reload,host_fail_after_commit;
static uint8_t host_flash[33][4500];
static uint16_t host_lengths[33];
typedef enum {FDS_LOOKUP_ERROR=-1,FDS_LOOKUP_ABSENT=0,FDS_LOOKUP_PRESENT=1} fds_record_lookup_result_t;
static unsigned host_lookup_error,host_corrupt_record_key,host_readback_fail_key,host_retain_marker,host_error_after_delete,host_short_record_key,host_damage_binding_on_final,host_fail_image_read,host_corrupt_config;
static nfc_tag_mf1_information_t host_live_image;
static uint16_t host_live_crc;
static uint8_t host_duplicate_key1[64];
static bool host_duplicate_present,host_duplicate_selected;
static unsigned host_config_fault;
bool tag_emulation_profile_hf_quarantined(uint8_t slot) {return sl1_profile_hf_incomplete(slot);}
tag_data_buffer_t *get_buffer_by_tag_type(tag_specific_type_t type) {
    (void)type;static tag_data_buffer_t live;
    live=(tag_data_buffer_t){sizeof(host_live_image),(uint8_t*)&host_live_image,&host_live_crc};return &live;
}
static unsigned host_index(uint16_t id, uint16_t key) {
    if (id == 0x1000 && key == 1) return 24;
    if (id >= 0x1100 && id < 0x1108 && key == TAG_SENSE_HF) return id - 0x1100;
    if (id >= 0x1300 && id < 0x1308 && key == 1) return 8 + id - 0x1300;
    if (id >= 0x1300 && id < 0x1308 && key == 2) return 25 + id - 0x1300;
    return 23;
}
bool fds_write_sync(uint16_t id,uint16_t key,uint16_t length,void *buffer) {
    ++host_writes;
    if (host_writes == host_fail_write) return false;
    unsigned i=host_index(id,key), stored_length=(length+3U)&~3U;
    memcpy(host_flash[i],buffer,stored_length); host_lengths[i]=stored_length;
    if(id>=0x1300&&id<0x1308&&key==host_corrupt_record_key)host_flash[i][20]^=1;
    if(id>=0x1300&&id<0x1308&&key==1&&host_damage_binding_on_final)host_flash[id-0x1300][offsetof(nfc_tag_mf1_information_t,memory)+16]^=1;
    if(host_writes==host_fail_after_commit)return false;
    if(host_corrupt_config&&id==0x1000)host_flash[i][0]^=1;
    if(host_fail_reload && id==0x1000)host_fail_image_read=1;
    return true;
}
bool fds_read_sync(uint16_t id,uint16_t key,uint16_t *length,uint8_t *buffer) {
    unsigned i=host_index(id,key);
    if ((id==0x1000&&host_config_fault==3) || (id>=0x1300&&key==1&&(host_lookup_error&2)) || (host_fail_image_read&&id>=0x1100&&id<0x1108&&key==TAG_SENSE_HF) || (id>=0x1300&&key==host_readback_fail_key) || host_read_fail || !host_lengths[i] || *length < host_lengths[i]) { *length=0; return false; }
    if(id==0x1300&&key==1&&host_duplicate_selected&&host_duplicate_present){*length=64;memcpy(buffer,host_duplicate_key1,64);return true;}
    *length=host_lengths[i];if(id==0x1000&&host_config_fault==4)*length=60;if(id>=0x1300&&key==host_short_record_key)*length=60; memcpy(buffer,host_flash[i],*length); return true;
}
int fds_record_count_sync(uint16_t id,uint16_t key) {
    if(id==0x1000&&host_config_fault==1)return -1;
    if(id==0x1000&&host_config_fault==2)return 2;
    if(id>=0x1300&&(host_lookup_error&(1U<<key)))return -1;
    return (host_lengths[host_index(id,key)]?1:0)+(id==0x1300&&key==1&&host_duplicate_present?1:0);
}
bool fds_is_exists(uint16_t id,uint16_t key) {return fds_record_count_sync(id,key)>0;}
fds_record_lookup_result_t fds_record_lookup_sync(uint16_t id,uint16_t key) {
    int count=fds_record_count_sync(id,key);
    return count<0?FDS_LOOKUP_ERROR:count?FDS_LOOKUP_PRESENT:FDS_LOOKUP_ABSENT;
}
static int host_delete_result=1;
int fds_delete_sync(uint16_t id,uint16_t key) {
    unsigned index=host_index(id,key);if(host_delete_result<0)return -1;
    int count=fds_record_count_sync(id,key);if(count<=0)return count;
    if(key==2&&host_retain_marker)return 1;
    host_lengths[index]=0;if(id==0x1300&&key==1)host_duplicate_present=false;
    if(key==2&&host_error_after_delete)host_lookup_error|=4;return count;
}
unsigned get_device_mode(void) { return host_mode; }
uint8_t tag_emulation_get_slot(void) { return slotConfig.active_slot; }
bool is_slot_enabled(uint8_t slot,tag_sense_type_t sense) { (void)sense; return slotConfig.slots[slot].enabled_hf; }
void tag_emulation_get_specific_types_by_slot(uint8_t slot,tag_slot_specific_type_t *out) {
    out->tag_hf=slotConfig.slots[slot].tag_hf; out->tag_lf=slotConfig.slots[slot].tag_lf;
}
bool nfc_tag_mf1_sl1_field_present(void) { return host_field; }
bool nfc_tag_mf1_sl1_disable(void) { if(host_field)return false; host_runtime=0; return true; }
bool nfc_tag_mf1_sl1_is_enabled(void) { return host_runtime; }
bool nfc_tag_mf1_sl1_ready(void) { return host_ready; }
bool nfc_tag_mf1_sl1_configure(uint8_t slot,const uint8_t *uid,uint8_t size,const uint8_t key[16]) {
    (void)slot;(void)uid;(void)size;(void)key; host_runtime=1;return true;
}
void nfc_tag_14a_sense_switch(bool enable) { host_sensing=enable; if(!enable)host_runtime=0; }
bool nfc_tag_14a_sensing_enabled(void) { return host_sensing; }
bool tag_emulation_load_by_buffer(tag_specific_type_t type,bool update_crc) { (void)type;(void)update_crc;return true; }
static nfc_tag_14a_coll_res_reference_t host_coll;
static nfc_tag_14a_uid_size host_uid_size;
static uint8_t host_uid[7];
nfc_tag_14a_coll_res_reference_t *get_mifare_coll_res(void) { return &host_coll; }
static void calc_14a_crc_lut(uint8_t *data,size_t size,uint8_t *out) {
    unsigned crc=0;for(size_t i=0;i<size;++i)crc+=data[i];out[0]=crc;out[1]=crc>>8;
}
void sl1_secure_zero(void *p,size_t n) { volatile uint8_t *q=p;while(n--)*q++=0; }
void tag_emulation_load_data(void);
void tag_emulation_sense_run(void);
bool tag_emulation_profile_save_config(uint8_t slot,uint16_t type);
'''

BRIDGE = r'''
void tag_emulation_load_data(void) {
    ++host_reloads; host_uid_size=4; memcpy(host_uid,"\x01\x02\x03\x04",4);
    if(host_lengths[slotConfig.active_slot])memcpy(&host_live_image,host_flash[slotConfig.active_slot],host_lengths[slotConfig.active_slot]);
    host_coll.size=&host_uid_size;host_coll.uid=host_uid;sl1_profile_load_active();
}
void tag_emulation_sense_run(void) { sl1_profile_sense_request(true); }
void tag_emulation_sense_switch(tag_sense_type_t type,bool enable) { (void)type;sl1_profile_sense_request(enable); }
bool tag_emulation_profile_reload_active(void) { if(host_read_fail)return false;tag_emulation_load_data(); return true; }
void host_reset(void) {
    memset(&slotConfig,0,sizeof(slotConfig));slotConfig.version=8;
    for(unsigned i=0;i<8;++i) {slotConfig.slots[i].tag_lf=TAG_TYPE_EM410X;slotConfig.slots[i].enabled_lf=1;}
    memset(host_flash,0,sizeof(host_flash));memset(host_lengths,0,sizeof(host_lengths));
    host_mode=host_field=host_runtime=host_sensing=host_reloads=host_writes=host_fail_write=host_read_fail=host_fail_image_read=0;
    host_lookup_error=host_corrupt_record_key=host_readback_fail_key=host_retain_marker=host_error_after_delete=host_short_record_key=host_damage_binding_on_final=host_fail_image_read=host_corrupt_config=0;
    host_config_fault=0;host_duplicate_present=host_duplicate_selected=false;memset(host_duplicate_key1,0,64);
    host_delete_result=1;host_mode=DEVICE_MODE_TAG;memset(&host_live_image,0,sizeof(host_live_image));host_live_crc=0;
    host_ready=1; host_fail_reload=host_fail_after_commit=0; cancel_stage();memset(m_last_status,0,8);memset(m_last_stage,0,8);m_sense_pending=false;
}
void host_reboot(void) {
    cancel_stage();host_runtime=host_sensing=0;m_sense_pending=false;
    host_fail_write=host_read_fail=host_fail_image_read=0;
    if(host_lengths[24])memcpy(&slotConfig,host_flash[24],sizeof(slotConfig));
    tag_emulation_load_data();
}
void host_set(unsigned which,unsigned value) {
    if(which==0)host_fail_write=value;
    if(which==1)host_field=value;
    if(which==2)host_mode=value;
    if(which==3)slotConfig.active_slot=value;
    if(which==4)host_read_fail=value;
    if(which==5)host_ready=value;
    if(which==6)host_fail_reload=value;
    if(which==7)host_sensing=value;
    if(which==8)host_live_image.memory[1][0]^=1;
    if(which==9)host_live_image.res_coll.uid[0]^=1;
    if(which==10)host_live_image.config.use_mf1_coll_res=value;
    if(which==12)host_fail_after_commit=value;
    if(which==13)host_lookup_error=value;
    if(which==14)host_corrupt_record_key=value;
    if(which==15)host_readback_fail_key=value;
    if(which==16)host_retain_marker=value;
    if(which==17)host_error_after_delete=value;
    if(which==18)host_short_record_key=value;
    if(which==19)host_damage_binding_on_final=value;
    if(which==20)host_corrupt_config=value;
    if(which==21){memcpy(host_duplicate_key1,host_flash[8],64);host_duplicate_present=true;}
    if(which==22)host_duplicate_selected=value;
    if(which==23)host_config_fault=value;
}
unsigned host_get(unsigned which) {
    if(which==0)return host_writes;if(which==1)return host_runtime;if(which==2)return host_sensing;
    if(which==3)return slotConfig.active_slot;if(which==4)return host_reloads;return m_sense_pending;
}
unsigned host_length(unsigned index) { return host_lengths[index]; }
unsigned host_memory_offset(void) { return offsetof(nfc_tag_mf1_information_t,memory); }
void host_resize_record(unsigned index,unsigned length) { host_lengths[index]=length; }
bool host_read_image(unsigned slot,unsigned type) { return image_read(slot,type); }
void host_record(unsigned index,uint8_t *out) {memcpy(out,host_flash[index],host_lengths[index]);}
void host_corrupt(unsigned index,unsigned offset) {host_flash[index][offset]^=1;}
unsigned host_lf(unsigned slot) {return slotConfig.slots[slot].tag_lf==TAG_TYPE_EM410X && slotConfig.slots[slot].enabled_lf;}
void host_migrate(void) {
    uint8_t old[68]={0};old[0]=3;
    for(unsigned i=0;i<8;++i){old[4+i*4]=1;old[4+i*4+2]=OLD_TAG_TYPE_MIFARE_1024;old[4+i*4+3]=OLD_TAG_TYPE_EM410X;}
    memcpy(&slotConfig,old,sizeof(slotConfig));tag_emulation_migrate_slot_config_v0_to_v8();
}
void host_status(unsigned slot,uint8_t *out) {uint8_t request[]={1,slot};sl1_profile_status(request,2,out);}
'''


def strip(source):
    return re.sub(r'^#include[^\n]*', '', source, flags=re.M)


class ProfileTests(unittest.TestCase):
    HOST_SHIM = SHIM
    HOST_BRIDGE = BRIDGE
    EXTRA_SOURCE = ""
    HOST_FLAGS = []
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='sl1-profile-host-')
        cls.addClassCleanup(cls.temp.cleanup)
        tmp = Path(cls.temp.name)
        prefix = '#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\n#define PACKED __attribute__((packed))\n#define ALIGN_U32 __attribute__((aligned(4)))\n#define STATIC_ASSERT(x) _Static_assert(x, #x)\n'
        tag = HF.parent / 'tag_emulation.c'
        tag_text=tag.read_text()
        defaults=tag_text[tag_text.index('#define TAG_SLOT_CONFIG_DEFAULTS'):tag_text.index('static tag_slot_config_t slotConfig ALIGN_U32 = TAG_SLOT_CONFIG_DEFAULTS')]
        source = '\n'.join([prefix, (HF.parent/'tag_base_type.h').read_text(),
            strip((HF.parent/'tag_emulation.h').read_text()), strip((HF/'nfc_14a.h').read_text()),
            strip((HF/'nfc_mf1.h').read_text()), (HF.parents[2]/'utils/fds_ids.h').read_text(),
            (HF.parents[2]/'app_status.h').read_text(), (HF/'sl1_profile.h').read_text(), cls.HOST_SHIM,defaults,
            function(tag.read_text(), 'tag_emulation_profile_save_config'),
            function(tag.read_text(), 'tag_emulation_migrate_slot_config_v0_to_v8'),
            strip((HF/'sl1_profile.c').read_text()), cls.EXTRA_SOURCE, cls.HOST_BRIDGE])
        (tmp/'profile.c').write_text(source)
        run = subprocess.run(['cc','-std=c11','-shared','-fPIC','-fshort-enums','-O1','-Wall','-Wextra',
            '-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types',
            *cls.HOST_FLAGS, str(tmp/'profile.c'),'-o',str(tmp/'profile.dylib')],capture_output=True,text=True)
        if run.returncode: raise RuntimeError(run.stderr)
        cls.lib = C.CDLL(str(tmp/'profile.dylib'))
        callback_type = C.CFUNCTYPE(None,C.c_void_p,C.c_size_t,C.c_void_p)
        def hash_callback(data,length,out):
            C.memmove(out,hashlib.sha256(C.string_at(data,length)).digest(),32)
        cls.hash_callback = callback_type(hash_callback)
        cls.lib.host_set_hash.argtypes=[callback_type]
        cls.lib.host_set_hash(cls.hash_callback)
        for name in ('begin','write','save','cancel','clear','set_enabled'):
            f=getattr(cls.lib,'sl1_profile_'+name);f.argtypes=[C.c_void_p,C.c_uint16];f.restype=C.c_uint8
        cls.lib.sl1_profile_invalidate.argtypes=[C.c_uint8];cls.lib.sl1_profile_invalidate.restype=C.c_bool
        cls.lib.host_record.argtypes=[C.c_uint,C.c_void_p]
        cls.lib.host_status.argtypes=[C.c_uint,C.c_void_p]
        cls.lib.host_read_image.argtypes=[C.c_uint,C.c_uint]
        cls.lib.host_read_image.restype=C.c_bool

    def setUp(self): self.lib.host_reset()
    def call(self,name,data): return getattr(self.lib,'sl1_profile_'+name)(data,len(data))
    def status(self,slot=0):
        out=(C.c_uint8*14)();self.lib.host_status(slot,out);return bytes(out)
    def record(self,index):
        out=(C.c_uint8*4500)();self.lib.host_record(index,out);return bytes(out[:self.lib.host_length(index)])
    def stage(self,slot=0,kind=1000,scenario=1,enabled=1,ats=b'\x02\x00'):
        count={1000:20,1001:64,1002:128,1003:256}[kind]
        key=bytes(range(16)) if scenario else bytes(16)
        prefix=bytes([1,slot])+kind.to_bytes(2,'big')+b'\x04\x01\x02\x03\x04\0\0\0\x04\x00\x08'+bytes([len(ats),scenario,enabled])+ (b'\x90\x04' if scenario else b'\0\0')+key
        self.assertEqual(len(prefix),36)
        self.assertEqual(self.call('begin',prefix+ats),0x68)
        image=bytearray((i*7+3)&255 for i in range(count*16))
        for block in range(3,count,4):
            if block<128 or block%16==15:image[block*16+6:block*16+9]=b'\xff\x07\x80'
        for first in range(0,count,8):
            part=image[first*16:min(first+8,count)*16]
            self.assertEqual(self.call('write',bytes([1,slot])+first.to_bytes(2,'big')+part),0x68)
        return bytes(image)

    def test_all_slots_and_geometries_no_selection_or_lf_change(self):
        for slot in range(8):
            kind=1000+slot%4;self.stage(slot,kind)
            self.assertEqual(self.call('save',bytes([1,slot])),0x68)
            self.assertEqual(self.lib.host_get(3),0)
            self.assertTrue(self.lib.host_lf(slot))
            self.assertEqual(self.status(slot)[2]&3,3)
            self.assertEqual(self.status(slot)[3:5],kind.to_bytes(2,'big'))

    def test_word_padded_images_save_and_reload_for_every_geometry(self):
        for kind in (1000,1001,1002,1003):
            with self.subTest(kind=kind):
                self.lib.host_reset()
                image=self.stage(kind=kind)
                offset=self.lib.host_memory_offset()
                logical=offset+len(image)
                self.assertNotEqual(logical%4,0)
                self.assertEqual(self.call('save',b'\x01\0'),0x68)
                self.assertEqual(self.lib.host_length(0),(logical+3)//4*4)
                self.assertEqual(self.record(0)[offset:offset+len(image)],image)
                self.lib.host_reboot()
                self.assertEqual(self.lib.host_get(1),1)
                self.assertEqual(self.status()[2]&0x50,0)

    def test_image_read_rejects_missing_or_extra_storage_words(self):
        for difference in (-4,4):
            with self.subTest(difference=difference):
                self.lib.host_reset();self.stage(kind=1001)
                self.assertEqual(self.call('save',b'\x01\0'),0x68)
                self.assertTrue(self.lib.host_read_image(0,1001))
                self.lib.host_resize_record(0,self.lib.host_length(0)+difference)
                self.assertFalse(self.lib.host_read_image(0,1001))

    def test_digest_and_64_byte_record_are_canonical(self):
        image=self.stage();self.assertEqual(self.call('save',b'\x01\0'),0x68)
        record=self.record(8);self.assertEqual(len(record),64);self.assertEqual(record[:4],b'SL1P')
        canonical=(1000).to_bytes(2,'big')+b'\x04\x01\x02\x03\x04\x04\0\x08\x02\x02\0'+image
        self.assertEqual(record[28:60],hashlib.sha256(canonical).digest())
        self.assertEqual(record[60:64],zlib.crc32(record[:60]).to_bytes(4,'big'))
        self.assertEqual(self.status()[2]&0x10,0)
        self.assertNotIn(bytes(range(16)),self.status())

    def test_every_persistence_failure_and_simulated_reboot_keeps_aes_off(self):
        for phase in range(1,5):
            self.lib.host_reset();self.stage();self.lib.host_set(0,phase)
            self.assertEqual(self.call('save',b'\x01\0'),0x70)
            self.assertEqual(self.status()[13],phase+1)
            self.lib.host_reboot();self.assertEqual(self.lib.host_get(1),0)

    def test_staging_and_cancel_never_change_live_slot(self):
        before=self.lib.host_get(0);self.stage(7,1003)
        self.assertEqual(self.lib.host_get(0),before);self.assertEqual(self.lib.host_get(4),0)
        self.assertEqual(self.call('cancel',b'\x01\x07'),0x68)
        self.assertEqual(self.status(7)[2]&8,0)

    def test_complete_coverage_and_access_complements_required(self):
        self.stage();self.assertEqual(self.status()[8:10],b'\0\x14')
        bad=bytes([1,0,0,3])+bytes(16)
        self.assertEqual(self.call('write',bad),0x68)
        self.assertEqual(self.call('save',b'\x01\0'),0x60)
        self.assertEqual(self.lib.host_get(0),0)
        self.lib.host_reset();self.stage();self.lib.host_corrupt(0,0)
        # BEGIN resets coverage even if prior stage was complete.
        prefix=b'\x01\0\x03\xe8\x04\x01\x02\x03\x04\0\0\0\x04\0\x08\x02\x01\x01\x90\x04'+bytes(range(16))+b'\x02\0'
        self.assertEqual(self.call('begin',prefix),0x68)
        self.assertEqual(self.call('save',b'\x01\0'),0x60)

    def test_legacy_no_sidecar_corrupt_and_read_error_records_fail_closed(self):
        self.lib.host_reboot();self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.status()[2]&3,0)
        self.stage();self.assertEqual(self.call('save',b'\x01\0'),0x68)
        self.lib.host_corrupt(8,12);self.lib.host_reboot()
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.status()[2]&16,16)
        self.lib.host_reset();self.stage();self.call('save',b'\x01\0');self.lib.host_set(4,1)
        self.assertEqual(self.status()[2]&16,16)

    def test_absent_legacy_extension_preserves_sensing_even_with_reader_or_field(self):
        for flag in (1,2):
            self.lib.host_reset();self.stage()
            self.lib.host_set(7,1);self.lib.host_set(flag,1)
            self.assertTrue(self.lib.sl1_profile_invalidate(0))
            self.assertEqual(self.lib.host_get(2),1)
            self.assertEqual(self.lib.host_get(0),0)
            self.assertEqual(self.status()[2]&8,0)
            self.assertTrue(self.lib.sl1_profile_invalidate(0))
            self.assertEqual(self.lib.host_get(0),0)

    def test_tombstoned_legacy_extension_is_idempotent_without_sensing_change(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        writes=self.lib.host_get(0)
        for flag in (1,2):
            self.lib.host_set(7,1);self.lib.host_set(flag,1)
            self.assertTrue(self.lib.sl1_profile_invalidate(0))
            self.assertEqual(self.lib.host_get(2),1)
            self.assertEqual(self.lib.host_get(0),writes)
            self.lib.host_set(flag,0)

    def test_real_extension_invalidation_keeps_classic_sensing_and_erases_aes_runtime(self):
        self.stage();self.call('save',b'\x01\0')
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(self.lib.host_get(1),1)
        image=self.record(0);writes=self.lib.host_get(0)
        self.assertTrue(self.lib.sl1_profile_invalidate(0))
        self.assertEqual(self.lib.host_get(2),1);self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.lib.host_get(0),writes+1);self.assertEqual(self.record(0),image)
        self.assertTrue(self.lib.sl1_profile_invalidate(0))
        self.assertEqual(self.lib.host_get(0),writes+1)

    def test_configured_extension_still_refuses_field_and_reader_invalidation(self):
        for flag in (1,2):
            self.lib.host_reset();self.stage();self.call('save',b'\x01\0')
            writes=self.lib.host_get(0);image=self.record(0);record=self.record(8)
            self.lib.host_set(flag,1)
            self.assertFalse(self.lib.sl1_profile_invalidate(0))
            self.assertEqual(self.lib.host_get(0),writes)
            self.assertEqual(self.record(0),image);self.assertEqual(self.record(8),record)

    def test_pending_sensing_invalidation_resumes_ordinary_classic(self):
        self.lib.host_set(5,0);self.stage();self.call('save',b'\x01\0')
        self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.lib.host_get(5),1)
        self.assertTrue(self.lib.sl1_profile_invalidate(0))
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.lib.host_get(2),1)
        self.assertEqual(self.lib.host_get(5),0)
        self.lib.sl1_profile_poll();self.assertEqual(self.lib.host_get(2),1)

    def test_live_unsaved_image_mismatch_does_not_receive_persistent_aes_key(self):
        self.stage();self.call('save',b'\x01\0')
        self.lib.host_set(8,1)
        self.lib.sl1_profile_load_active()
        self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.status()[2]&16,0,'stored binding itself is unchanged')

    def test_live_uid_and_effective_block0_policy_are_part_of_activation_binding(self):
        for flag in (9,10):
            self.lib.host_reset();self.stage();self.call('save',b'\x01\0')
            self.lib.host_set(flag,1);self.lib.sl1_profile_load_active()
            self.assertEqual(self.lib.host_get(1),0)

    def test_pending_invalidation_flash_failure_restores_ordinary_sensing_only(self):
        self.lib.host_set(5,0);self.stage();self.call('save',b'\x01\0')
        self.lib.host_set(0,self.lib.host_get(0)+1)
        self.assertFalse(self.lib.sl1_profile_invalidate(0))
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.lib.host_get(2),1)
        self.assertEqual(self.status()[2]&3,0,'failed canonical rewrite cannot revive deleted old keys')

    def test_legacy_invalidation_persists_and_does_not_revive(self):
        self.stage();self.call('save',b'\x01\0')
        self.assertTrue(self.lib.sl1_profile_invalidate(0));self.lib.host_reboot()
        self.assertEqual(self.lib.host_get(1),0);self.assertEqual(self.status()[2]&3,0)

    def test_disable_preserves_key_and_clear_preserves_classic_image(self):
        self.stage();self.call('save',b'\x01\0');image=self.record(0)
        key=self.record(8)[12:28]
        self.assertEqual(self.call('set_enabled',b'\x01\0\0'),0x68)
        self.assertEqual(self.record(8)[12:28],key);self.assertEqual(self.status()[2]&3,1)
        self.assertEqual(self.call('set_enabled',b'\x01\0\x01'),0x68)
        self.assertEqual(self.call('clear',b'\x01\0'),0x68);self.assertEqual(self.record(0),image)

    def test_reader_and_field_refuse_commits(self):
        for flag in (1,2):
            self.lib.host_reset();self.stage();self.lib.host_set(flag,1)
            self.assertEqual(self.call('save',b'\x01\0'),0x75);self.assertEqual(self.lib.host_get(0),0)

    def test_persisted_image_reload_failure_keeps_transaction_marker_at_stage_six(self):
        self.stage();self.lib.host_set(6,1)
        self.assertEqual(self.call('save',b'\x01\0'),0x71)
        self.assertEqual(self.lib.host_get(1),0)
        self.lib.host_set(4,0)
        self.assertEqual(self.status()[12:14],b'\x71\x06')
        self.lib.host_reboot();self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.status()[2]&16,16)

    def test_invalid_begin_fields_do_not_replace_valid_stage(self):
        self.stage()
        original=self.status()
        prefix=b'\x01\0\x03\xe8\x04\x01\x02\x03\x04\0\0\0\x04\0\x08\x02\x01\x01\x90\x04'+bytes(range(16))+b'\x02\0'
        for offset,value in ((0,2),(1,8),(4,5),(9,1),(15,3),(16,2),(17,2),(18,0)):
            bad=bytearray(prefix);bad[offset]=value
            self.assertEqual(self.call('begin',bytes(bad)),0x60)
            self.assertEqual(self.status()[8:12],original[8:12])

    def test_existing_config_migration_never_infers_aes_coverage(self):
        self.lib.host_migrate()
        self.assertEqual(self.lib.host_get(3),3)
        for slot in range(8):
            self.assertEqual(self.status(slot)[3:5],(1001).to_bytes(2,'big'))
            self.assertEqual(self.status(slot)[2]&3,0)
        self.lib.host_reboot();self.assertEqual(self.lib.host_get(1),0)

    def test_previous_valid_record_survives_only_failure_before_tombstone_commit(self):
        for phase in range(1,5):
            self.lib.host_reset();self.stage();self.call('save',b'\x01\0')
            self.stage();self.lib.host_set(0,self.lib.host_get(0)+phase)
            self.assertEqual(self.call('save',b'\x01\0'),0x70)
            self.assertEqual(self.lib.host_get(1),0)
            self.lib.host_reboot()
            self.assertEqual(self.lib.host_get(1),int(phase==1))

    def test_bound_image_mismatch_is_stale_and_inert(self):
        self.stage();self.call('save',b'\x01\0')
        self.lib.host_corrupt(0,self.lib.host_memory_offset()+16);self.lib.host_reboot()
        self.assertEqual(self.lib.host_get(1),0)
        self.assertEqual(self.status()[2]&16,16)

    def test_classic_only_has_no_aes_record_and_pending_sensing_waits(self):
        self.stage(scenario=0,enabled=0,ats=b'');self.call('save',b'\x01\0')
        self.assertEqual(self.status()[2]&3,0)
        self.lib.host_reset();self.lib.host_set(5,0);self.stage();self.call('save',b'\x01\0')
        self.assertEqual(self.lib.host_get(2),0);self.assertEqual(self.lib.host_get(5),1)
        self.lib.host_set(5,1);self.lib.sl1_profile_poll();self.assertEqual(self.lib.host_get(2),1)

if __name__=='__main__': unittest.main()
