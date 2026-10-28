"""Execute actual command dispatch/capabilities and guarded legacy handlers.

Profile persistence, FDS and tag owners are controlled doubles. The real command
map is compiled separately for Ultra and Lite, retaining its preprocessor guards.
No hardware, transport connection or firmware build is used.
"""
import ctypes as C
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

from test_cu_sl1_native_hooks import function

ROOT = Path(__file__).resolve().parents[3]
APP = ROOT / 'firmware/application/src'
HF = APP / 'rfid/nfctag/hf'

HANDLERS = (
    'cmd_processor_set_slot_tag_type',
    'cmd_processor_delete_slot_sense_type',
    'cmd_processor_set_slot_data_default',
    'cmd_processor_mf1_write_emu_block_data',
    'cmd_processor_hf14a_set_anti_coll_data',
    'cmd_processor_set_slot_tag_nick',
    'cmd_processor_mf1_set_gen1a_mode',
    'cmd_processor_mf1_set_gen2_mode',
    'cmd_processor_mf1_set_block_anti_coll_mode',
    'cmd_processor_delete_slot_tag_nick',
    'cmd_processor_wipe_fds',
)

SHIM = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#define PACKED __attribute__((packed))
#define TAG_MAX_SLOT_NUM 8
#define ARRAYLEN(a) (sizeof(a)/sizeof((a)[0]))
#define ARRAY_SIZE(a) ARRAYLEN(a)
#define U16NTOHS(x) __builtin_bswap16(x)
#define U16HTONS(x) __builtin_bswap16(x)
#define NRF_LOG_INFO(...)
typedef struct {uint16_t length;uint8_t *buffer;uint16_t *crc;} tag_data_buffer_t;
typedef struct {uint16_t cmd,status,length;uint8_t buffer[8192];} data_frame_tx_t;
typedef data_frame_tx_t *(*cmd_processor)(uint16_t,uint16_t,uint16_t,uint8_t *);
typedef struct {uint16_t cmd;cmd_processor cmd_before,cmd_processor,cmd_after;} cmd_data_map_t;
static data_frame_tx_t host_response;
static int host_pending_slot;
static unsigned host_generic_calls,host_wipe_calls,host_reset_calls;
static bool sl1_profile_hf_incomplete(uint8_t slot){return host_pending_slot==-2||slot==host_pending_slot;}
static bool fds_wipe(void){++host_wipe_calls;return true;}
static void delayed_reset(unsigned delay){(void)delay;++host_reset_calls;}
static unsigned host_profile_result,host_profile_called,host_profile_length;
static uint8_t host_profile_input[8192];
static unsigned host_invalidate_ok,host_invalidations,host_invalidated_slot,host_mutations,host_order,host_invalidate_order,host_mutation_order;
static int host_delete_count;
static int fds_delete_sync(uint16_t id,uint16_t key){(void)id;(void)key;return host_delete_count;}
static unsigned host_runtime,host_gen1,host_gen2,host_use_block0,host_nick_writes,host_factory_ok;
static nfc_tag_mf1_information_t host_image;
static nfc_tag_14a_coll_res_entity_t host_identity;
static nfc_tag_14a_coll_res_reference_t host_identity_ref;
static unsigned host_initial_fingerprint,host_fingerprint_at_invalidation;
static unsigned fingerprint(void){
    unsigned value=2166136261U;const uint8_t *p=(const uint8_t*)&host_image;
    for(size_t i=0;i<sizeof(host_image);++i)value=(value^p[i])*16777619U;
    p=(const uint8_t*)&host_identity;for(size_t i=0;i<sizeof(host_identity);++i)value=(value^p[i])*16777619U;
    return value ^ host_gen1 ^ (host_gen2<<1) ^ (host_use_block0<<2) ^ (host_mutations<<4);
}
static data_frame_tx_t *data_frame_make(uint16_t cmd,uint16_t status,uint16_t length,uint8_t *data){
    memset(&host_response,0,sizeof(host_response));host_response.cmd=cmd;host_response.status=status;host_response.length=length;
    if(length)memcpy(host_response.buffer,data,length);return &host_response;
}
static data_frame_tx_t *host_stub(uint16_t c,uint16_t s,uint16_t n,uint8_t *p){(void)s;(void)n;(void)p;++host_generic_calls;return data_frame_make(c,STATUS_SUCCESS,0,NULL);}
static void auto_response_data(data_frame_tx_t *response){(void)response;}
static uint8_t profile_call(unsigned index,const uint8_t *data,uint16_t length){
    host_profile_called=index;host_profile_length=length;if(data&&length)memcpy(host_profile_input,data,length);return host_profile_result;
}
static uint8_t sl1_profile_status(const uint8_t *data,uint16_t length,uint8_t out[14]){
    for(unsigned i=0;i<14;++i)out[i]=i+1;return profile_call(1,data,length);
}
#define PROFILE_DOUBLE(name,index) static uint8_t sl1_profile_##name(const uint8_t *p,uint16_t n){return profile_call(index,p,n);}
PROFILE_DOUBLE(begin,2)
PROFILE_DOUBLE(write,3)
PROFILE_DOUBLE(save,4)
PROFILE_DOUBLE(cancel,5)
PROFILE_DOUBLE(clear,6)
PROFILE_DOUBLE(set_enabled,7)
static bool sl1_profile_invalidate(uint8_t slot){
    ++host_invalidations;host_invalidated_slot=slot;host_invalidate_order=++host_order;
    host_fingerprint_at_invalidation=fingerprint();return host_invalidate_ok;
}
static uint8_t tag_emulation_get_slot(void){return 3;}
static bool is_tag_specific_type_valid(tag_specific_type_t type){return type==TAG_TYPE_EM410X||(type>=1000&&type<=1003);}
static tag_sense_type_t get_sense_type_from_tag_type(tag_specific_type_t type){return type==TAG_TYPE_EM410X?TAG_SENSE_LF:TAG_SENSE_HF;}
static void mutate(void){++host_mutations;host_mutation_order=++host_order;}
static void tag_emulation_change_type(uint8_t slot,tag_specific_type_t type){(void)slot;(void)type;mutate();}
static bool tag_emulation_delete_data(uint8_t slot,tag_sense_type_t sense){(void)slot;(void)sense;if(host_delete_count<0)return false;mutate();return true;}
static bool tag_emulation_factory_data(uint8_t slot,tag_specific_type_t type){(void)slot;(void)type;if(!host_factory_ok)return false;mutate();return true;}
static tag_data_buffer_t *get_buffer_by_tag_type(tag_specific_type_t type){(void)type;static tag_data_buffer_t b;b.buffer=(uint8_t*)&host_image;b.length=sizeof(host_image);return &b;}
bool is_valid_uid_size(uint8_t size){return size==4||size==7||size==10;}
static nfc_tag_14a_coll_res_reference_t *get_coll_res_data(bool saved){(void)saved;return &host_identity_ref;}
typedef struct {uint16_t id,key;} fds_slot_record_map_t;
static void get_fds_map_by_slot_sense_type_for_nick(uint8_t slot,tag_sense_type_t sense,fds_slot_record_map_t *map){map->id=0x1200+slot;map->key=sense;}
static bool fds_write_sync(uint16_t id,uint16_t key,uint16_t length,void *buffer){(void)id;(void)key;(void)length;(void)buffer;++host_nick_writes;return true;}
bool nfc_tag_mf1_sl1_is_enabled(void){return host_runtime;}
void nfc_tag_mf1_set_gen1a_magic_mode(bool enabled){mutate();host_gen1=enabled;}
void nfc_tag_mf1_set_gen2_magic_mode(bool enabled){mutate();host_gen2=enabled;}
bool nfc_tag_mf1_is_use_mf1_coll_res(void){return host_use_block0;}
void nfc_tag_mf1_set_use_mf1_coll_res(bool enabled){mutate();host_use_block0=enabled;}
data_frame_tx_t *cmd_processor_get_device_capabilities(uint16_t,uint16_t,uint16_t,uint8_t *);
'''

BRIDGE = r'''
void host_reset(void){
    host_pending_slot=-1;host_generic_calls=host_wipe_calls=host_reset_calls=0;host_profile_result=STATUS_SUCCESS;host_profile_called=host_profile_length=0;
    host_invalidate_ok=1;host_invalidations=host_invalidated_slot=host_mutations=host_order=host_invalidate_order=host_mutation_order=0;
    host_delete_count=1;host_runtime=host_gen1=host_gen2=host_use_block0=host_nick_writes=0;host_factory_ok=1;
    memset(&host_response,0,sizeof(host_response));memset(&host_image,0xA5,sizeof(host_image));
    memset(&host_identity,0,sizeof(host_identity));host_identity.size=4;memset(host_identity.uid,0x11,4);
    host_identity_ref=(nfc_tag_14a_coll_res_reference_t){.size=&host_identity.size,.uid=host_identity.uid,.atqa=host_identity.atqa,.sak=host_identity.sak,.ats=&host_identity.ats};
    host_initial_fingerprint=fingerprint();host_fingerprint_at_invalidation=0;
}
void host_call(uint16_t cmd,uint8_t *data,uint16_t length){on_data_frame_received(cmd,0,length,data);}
void host_capabilities(void){cmd_processor_get_device_capabilities(1035,0,0,NULL);}
unsigned host_get(unsigned which){
    switch(which){case 0:return host_response.status;case 1:return host_response.length;case 2:return host_profile_called;
    case 3:return host_invalidations;case 4:return host_mutations;case 5:return host_invalidate_order;case 6:return host_mutation_order;
    case 7:return host_invalidated_slot;case 8:return fingerprint()==host_initial_fingerprint;
    case 9:return host_fingerprint_at_invalidation==host_initial_fingerprint;case 10:return host_nick_writes;
    case 11:return host_profile_length;case 12:return host_generic_calls;case 13:return host_wipe_calls+host_reset_calls;default:return 0;}
}
void host_set(unsigned which,unsigned value){if(which==0)host_profile_result=value;if(which==1)host_invalidate_ok=value;if(which==2)host_runtime=value;if(which==3)host_factory_ok=value;if(which==4)host_delete_count=(int)value;if(which==5)host_pending_slot=(int)value;}
const uint8_t *host_output(void){return host_response.buffer;}
const uint8_t *host_input(void){return host_profile_input;}
unsigned host_command(unsigned index){
    const unsigned values[]={DATA_CMD_SET_SLOT_TAG_TYPE,DATA_CMD_DELETE_SLOT_SENSE_TYPE,DATA_CMD_SET_SLOT_DATA_DEFAULT,
    DATA_CMD_MF1_WRITE_EMU_BLOCK_DATA,DATA_CMD_HF14A_SET_ANTI_COLL_DATA,DATA_CMD_SET_SLOT_TAG_NICK,
    DATA_CMD_MF1_SET_GEN1A_MODE,DATA_CMD_MF1_SET_GEN2_MODE,DATA_CMD_MF1_SET_BLOCK_ANTI_COLL_MODE,DATA_CMD_DELETE_SLOT_TAG_NICK,DATA_CMD_WIPE_FDS};return values[index];
}
'''


def strip(source):
    return re.sub(r'^#include[^\n]*', '', source, flags=re.M)


class ProfileCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='sl1-command-host-')
        cls.addClassCleanup(cls.temp.cleanup)
        tmp = Path(cls.temp.name)
        source = (APP/'app_cmd.c').read_text()
        start = source.index('static cmd_data_map_t m_data_cmd_map[] = {')
        end = source.index('\n};',start)+3
        table = source[start:end]
        defined = set(HANDLERS) | {'cmd_processor_sl1_profile','cmd_processor_get_device_capabilities'}
        # Retain the entire actual table and its #if guards; only unrelated
        # callbacks become a generic command double.
        callbacks=set(re.findall(r'\b(?:cmd_processor_\w+|before_\w+|after_\w+)\b',table))
        aliases='\n'.join('#define '+name+' host_stub' for name in sorted(callbacks-defined))
        declarations = '#include <stdint.h>\n#include <stdbool.h>\n#include <stddef.h>\n#include <string.h>\n#define PACKED __attribute__((packed))\ntypedef struct {uint16_t length;uint8_t *buffer;uint16_t *crc;} tag_data_buffer_t;\n'
        # The native types retain the firmware's short-enum ABI.
        type_headers='\n'.join([(HF.parent/'tag_base_type.h').read_text(),strip((HF/'nfc_14a.h').read_text()),strip((HF/'nfc_mf1.h').read_text())])
        shim=SHIM.replace('typedef struct {uint16_t length;uint8_t *buffer;uint16_t *crc;} tag_data_buffer_t;','')
        common='\n'.join([declarations,(APP/'app_status.h').read_text(),(APP/'data_cmd.h').read_text(),
            '#define TAG_MAX_SLOT_NUM 8',type_headers,shim,aliases,function(source,'invalidate_sl1_profile'),
            '#if defined(PROJECT_CHAMELEON_ULTRA)',function(source,'cmd_processor_sl1_profile'),'#endif',
            *(function(source,name) for name in HANDLERS),table,
            function(source,'cmd_processor_get_device_capabilities'),function(source,'command_uses_live_hf'),function(source,'on_data_frame_received'),BRIDGE])
        (tmp/'commands.c').write_text(common)
        cls.libraries={}
        for model in ('ultra','lite'):
            path=tmp/(model+'.dylib')
            command=['cc','-std=c11','-shared','-fPIC','-fshort-enums','-O1','-Wall','-Wextra',
                '-Werror=implicit-function-declaration','-Werror=incompatible-pointer-types']
            if model=='ultra':command+=['-DPROJECT_CHAMELEON_ULTRA=1']
            run=subprocess.run(command+[str(tmp/'commands.c'),'-o',str(path)],capture_output=True,text=True)
            if run.returncode:raise RuntimeError(model+': '+run.stderr)
            lib=C.CDLL(str(path));lib.host_call.argtypes=[C.c_uint16,C.c_void_p,C.c_uint16]
            lib.host_output.restype=lib.host_input.restype=C.POINTER(C.c_uint8)
            cls.libraries[model]=lib

    def setUp(self):
        self.lib=self.libraries['ultra'];self.lib.host_reset()
    def call(self,cmd,data):
        buffer=C.create_string_buffer(data)
        self.lib.host_call(cmd,buffer,len(data))
        return self.lib.host_get(0),bytes(buffer.raw[:len(data)])
    def output(self):return bytes(self.lib.host_output()[:self.lib.host_get(1)])
    def command(self,index):return self.lib.host_command(index)

    def test_actual_ultra_capabilities_map_4045_to_4051_lite_omits_them(self):
        for model,lib in self.libraries.items():
            lib.host_reset();lib.host_capabilities()
            output=bytes(lib.host_output()[:lib.host_get(1)])
            commands=[int.from_bytes(output[i:i+2],'big') for i in range(0,len(output),2)]
            for command in range(4045,4052):self.assertEqual(commands.count(command),int(model=='ultra'))
        self.lib=self.libraries['lite']
        for command in range(4045,4052):
            self.assertEqual(self.call(command,b'\x01\0')[0],0x67)
            self.assertEqual(self.lib.host_get(2),0)

    def test_real_dispatch_mapping_exact_status_and_completed_payload_wiping(self):
        credential=b'NEVER_ECHO_AES!!!'+bytes(range(32))
        for command in range(4045,4052):
            for status in (0x68,0x60,0x70,0x75):
                self.lib.host_reset();self.lib.host_set(0,status)
                self.assertEqual(self.call(command,credential),(status,bytes(len(credential))))
                self.assertEqual(self.lib.host_get(2),command-4044)
                self.assertEqual(self.lib.host_get(11),len(credential))
                self.assertEqual(bytes(self.lib.host_input()[:len(credential)]),credential)
                expected=bytes(range(1,15)) if command==4045 and status==0x68 else b''
                self.assertEqual(self.output(),expected)
                self.assertNotIn(credential[:16],self.output())

    def test_completed_empty_profile_request_is_safe(self):
        self.assertEqual(self.call(4046,b''),(0x68,b''))
        self.assertEqual(self.lib.host_get(2),2)

    def test_legacy_image_identity_default_type_delete_invalidate_before_mutation(self):
        cases=((0,b'\x02\x03\xe9'),(1,b'\x02\x02'),(2,b'\x02\x03\xe9'),
            (3,b'\x01'+bytes(range(16))),(4,b'\x04\x01\x02\x03\x04\x04\0\x08\x02\x02\0'))
        for index,payload in cases:
            self.lib.host_reset();self.assertEqual(self.call(self.command(index),payload)[0],0x68)
            self.assertEqual(self.lib.host_get(3),1)
            self.assertEqual(self.lib.host_get(9),1,'invalidation must observe the original image/config')
            self.assertEqual(self.lib.host_get(8),0)
            self.assertEqual(self.lib.host_get(7),2 if index<3 else 3)
            if index<3:self.assertLess(self.lib.host_get(5),self.lib.host_get(6))

    def test_failed_legacy_invalidation_leaves_image_and_config_unchanged(self):
        cases=((0,b'\x02\x03\xe9'),(1,b'\x02\x02'),(2,b'\x02\x03\xe9'),
            (3,b'\x01'+bytes(range(16))),(4,b'\x04\x01\x02\x03\x04\x04\0\x08\0'),(8,b'\x01'))
        for index,payload in cases:
            self.lib.host_reset();self.lib.host_set(1,0)
            self.assertEqual(self.call(self.command(index),payload)[0],0x70)
            self.assertEqual(self.lib.host_get(8),1)
            self.assertEqual(self.lib.host_get(4),0)
            self.assertEqual(self.lib.host_get(3),1)

    def test_default_operation_failure_returns_owner_status_after_invalidation(self):
        self.lib.host_set(3,0)
        self.assertEqual(self.call(self.command(2),b'\x02\x03\xe9')[0],0x69)
        self.assertEqual(self.lib.host_get(3),1)
        self.assertEqual(self.lib.host_get(4),0)
        self.assertEqual(self.lib.host_get(8),1)

    def test_lf_and_nickname_changes_do_not_invalidate_hf_profile(self):
        for index,payload in ((0,b'\x02\0\x64'),(1,b'\x02\x01'),(2,b'\x02\0\x64'),(5,b'\x02\x02Name')):
            self.lib.host_reset();self.assertEqual(self.call(self.command(index),payload)[0],0x68)
            self.assertEqual(self.lib.host_get(3),0)
            if index==5:self.assertEqual(self.lib.host_get(10),1)

    def test_malformed_legacy_requests_do_not_invalidate_or_mutate(self):
        cases={0:(b'',b'\x08\x03\xe9',b'\x01\xff\xff'),1:(b'',b'\x01\0',b'\x08\x02'),
            2:(b'',b'\x08\x03\xe9'),3:(b'',b'\0',b'\0\x01',b'\xff'+bytes(32),b'\0'+bytes(8192)),
            4:(b'',b'\x05',b'\x04'+bytes(7)),6:(b'',b'\x02'),7:(b'',b'\x02'),8:(b'',b'\x02')}
        for index,payloads in cases.items():
            for payload in payloads:
                self.lib.host_reset();self.assertEqual(self.call(self.command(index),payload)[0],0x60)
                self.assertEqual(self.lib.host_get(3),0);self.assertEqual(self.lib.host_get(8),1)

    def test_gen1_gen2_rejected_only_when_enabling_with_active_sl1(self):
        for index in (6,7):
            self.lib.host_reset();self.lib.host_set(2,1)
            self.assertEqual(self.call(self.command(index),b'\x01')[0],0x75)
            self.assertEqual(self.lib.host_get(4),0);self.assertEqual(self.lib.host_get(3),0)
            self.assertEqual(self.call(self.command(index),b'\0')[0],0x68)
            self.lib.host_reset();self.assertEqual(self.call(self.command(index),b'\x01')[0],0x68)

    def test_slot_delete_owner_failure_returns_error_without_config_mutation(self):
        self.lib.host_set(4,-1)
        self.assertEqual(self.call(self.command(1),b"\x01\x02")[0],0x70)
        self.assertEqual(self.lib.host_get(4),0)
        self.lib.host_reset();self.lib.host_set(4,0)
        self.assertEqual(self.call(self.command(1),b"\x01\x02")[0],0x68)

    def test_nickname_delete_requires_positive_signed_count(self):
        for count,expected in ((-1,0x70),(0,0x70),(1,0x68),(2,0x68)):
            self.lib.host_reset();self.lib.host_set(4,count)
            self.assertEqual(self.call(self.command(9),b"\x01\x02")[0],expected)

    def test_quarantine_dispatch_blocks_hf_mutators_and_unloaded_handler_getters(self):
        for command in (4000,4001,4008,4018,4020,4021,4022,4024,4032,4042,4043,4044,6000,6001,6002,6003,6010,2020):
            self.lib.host_reset();self.lib.host_set(5,3)
            self.assertEqual(self.call(command,b"\x01")[0],0x75)
            self.assertEqual(self.lib.host_get(12),0);self.assertEqual(self.lib.host_get(4),0)

    def test_quarantine_dispatch_preserves_recovery_lf_nickname_status_and_slot_selection(self):
        self.lib.host_set(5,3)
        for command in range(4045,4052):
            self.assertEqual(self.call(command,b"\x01\0")[0],0x68)
        for command in (1000,1003,5000,6004,6005):
            self.assertEqual(self.call(command,b"\x01")[0],0x68)
        self.assertEqual(self.call(self.command(5),b"\x01\x02Name")[0],0x68)
        self.assertEqual(self.lib.host_get(10),1)
        self.assertEqual(self.call(self.command(0),b"\x01\0\x64")[0],0x68)

    def test_factory_wipe_rejects_pending_or_unknown_any_slot_without_reset(self):
        for pending in (0,7,-2):
            self.lib.host_reset();self.lib.host_set(5,pending)
            self.assertEqual(self.call(self.command(10),b'')[0],0x75)
            self.assertEqual(self.lib.host_get(13),0)
        self.lib.host_reset();self.assertEqual(self.call(self.command(10),b'')[0],0x68)
        self.assertEqual(self.lib.host_get(13),2)

    def test_changed_block0_identity_policy_invalidates_unchanged_does_not(self):
        self.assertEqual(self.call(self.command(8),b'\0')[0],0x68)
        self.assertEqual(self.lib.host_get(3),0)
        self.lib.host_reset();self.assertEqual(self.call(self.command(8),b'\x01')[0],0x68)
        self.assertEqual(self.lib.host_get(3),1);self.assertLess(self.lib.host_get(5),self.lib.host_get(6))

if __name__=='__main__':unittest.main()
