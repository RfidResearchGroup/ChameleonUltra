"""Actual FDS synchronous wrapper: matched terminal failures and read ownership.

The injected event scheduler aborts after 100 steps, so a regression reports a
failed subprocess instead of hanging the test runner. No Nordic SDK/device used.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[3]
SHIM=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <setjmp.h>
typedef uint32_t ret_code_t;
#define NRF_SUCCESS 0
#define FDS_ERR_NO_SPACE_IN_FLASH 2
#define FDS_ERR_NOT_FOUND 3
typedef enum {FDS_LOOKUP_ERROR=-1,FDS_LOOKUP_ABSENT=0,FDS_LOOKUP_PRESENT=1} fds_record_lookup_result_t;
#define NRF_LOG_MODULE_REGISTER(...)
static unsigned host_expected_app_error,host_reported_app_error;
#define APP_ERROR_CHECK(x) do {unsigned rc=(x);if(rc){if(host_expected_app_error)host_reported_app_error=rc;else abort();}}while(0)
enum {FDS_EVT_INIT,FDS_EVT_WRITE,FDS_EVT_UPDATE,FDS_EVT_DEL_RECORD,FDS_EVT_GC};
typedef struct {unsigned record_id;} fds_record_desc_t;
typedef struct {unsigned unused;} fds_find_token_t;
typedef struct {uint16_t length_words;} record_header;
typedef struct {record_header *p_header;const void *p_data;} fds_flash_record_t;
typedef struct {uint16_t file_id,key;struct {void *p_data;uint16_t length_words;} data;} fds_record_t;
typedef struct {unsigned id,result;struct {uint16_t file_id,record_key;} write;struct {uint16_t file_id,record_key;unsigned record_id;} del;} fds_evt_t;
static void (*callback)(const fds_evt_t *);
static unsigned exists,opens,closes,open_error,close_error,submit_error,async_error;
static unsigned gc_async_error,wrong_first,steps,writes,gcs,deleted;
static unsigned watchdog_feeds,drop_events,iterate_error,lookup_error;
static unsigned first_record_id=7;
static jmp_buf watchdog_reset;
static const void *submitted;
static fds_evt_t queued;
static bool pending;
static record_header header={2};static uint32_t stored[2]={0x11223344,0x55667788};
static ret_code_t fds_record_find(uint16_t id,uint16_t key,fds_record_desc_t *d,fds_find_token_t *t){(void)id;(void)key;if(lookup_error)return lookup_error;if(t->unused>=exists)return FDS_ERR_NOT_FOUND;d->record_id=first_record_id+t->unused++;return 0;}
static ret_code_t fds_record_open(fds_record_desc_t *d,fds_flash_record_t *r){(void)d;++opens;if(open_error)return 1;r->p_header=&header;r->p_data=stored;return 0;}
static ret_code_t fds_record_close(fds_record_desc_t *d){(void)d;++closes;return close_error;}
static ret_code_t submit(const fds_record_t *r,unsigned kind){
    ++writes;if(submit_error){unsigned e=submit_error;submit_error=0;return e;}
    submitted=r->data.p_data;queued=(fds_evt_t){.id=kind,.result=async_error,.write={r->file_id,r->key}};pending=true;return 0;
}
static ret_code_t fds_record_update(fds_record_desc_t *d,const fds_record_t *r){(void)d;return submit(r,FDS_EVT_UPDATE);}
static ret_code_t fds_record_write(fds_record_desc_t *d,const fds_record_t *r){(void)d;return submit(r,FDS_EVT_WRITE);}
static ret_code_t fds_record_id_from_desc(const fds_record_desc_t *d,uint32_t *id){*id=d->record_id;return 0;}
static ret_code_t fds_record_delete(fds_record_desc_t *d){if(submit_error)return submit_error;queued=(fds_evt_t){.id=FDS_EVT_DEL_RECORD,.result=async_error,.del={.record_id=d->record_id}};pending=true;return 0;}
static ret_code_t fds_record_iterate(fds_record_desc_t *d,fds_find_token_t *t){(void)t;if(iterate_error)return iterate_error;d->record_id=7;return exists?0:FDS_ERR_NOT_FOUND;}
static ret_code_t fds_gc(void){++gcs;queued=(fds_evt_t){.id=FDS_EVT_GC,.result=gc_async_error};pending=true;return 0;}
static ret_code_t fds_register(void (*cb)(const fds_evt_t *)){callback=cb;return 0;}
static ret_code_t fds_init(void){return 0;}
#define bsp_wdt_feed() (++watchdog_feeds)
static void tick(void){
    if(++steps>100)exit(97);
    if(drop_events&&steps==5&&!watchdog_feeds)longjmp(watchdog_reset,1);
    if(pending&&!drop_events){
        if(wrong_first){wrong_first=0;fds_evt_t wrong=queued;if(queued.id==FDS_EVT_DEL_RECORD)wrong.del.record_id^=1;else wrong.write.file_id^=1;callback(&wrong);return;}
        pending=false;if(queued.id==FDS_EVT_DEL_RECORD&&!queued.result){--exists;++first_record_id;++deleted;}
        callback(&queued);
    }
}
#define __NOP() tick()
'''
DRIVER=r'''
int main(int argc,char **argv){
    if(argc!=2)return 99;unsigned test=atoi(argv[1]);fds_util_init();
    uint32_t data[2]={1,2};uint16_t length=8;
    switch(test){
    case 1:exists=1;return !(fds_read_sync(1,1,&length,(uint8_t*)data)&&length==8&&opens==1&&closes==1);
    case 2:exists=1;length=4;return !(!fds_read_sync(1,1,&length,(uint8_t*)data)&&length==0&&closes==1);
    case 3:exists=1;open_error=1;return !(!fds_read_sync(1,1,&length,(uint8_t*)data)&&closes==0);
    case 4:submit_error=1;return !(!fds_write_sync(1,1,8,data)&&!steps);
    case 5:async_error=1;return !(!fds_write_sync(1,1,8,data)&&steps==1&&submitted==data);
    case 6:wrong_first=1;return !(fds_write_sync(1,1,8,data)&&steps==2&&submitted==data);
    case 7:submit_error=FDS_ERR_NO_SPACE_IN_FLASH;return !(fds_write_sync(1,1,8,data)&&gcs==1&&writes==2);
    case 8:submit_error=FDS_ERR_NO_SPACE_IN_FLASH;gc_async_error=1;return !(!fds_write_sync(1,1,8,data)&&gcs==1&&writes==1);
    case 9:return !(!fds_write_sync(1,1,0,data)&&fds_write_sync(1,1,8,data));
    case 10:async_error=1;if(fds_write_sync(1,1,8,data))return 1;async_error=0;return !fds_write_sync(1,1,8,data);
    case 11:exists=1;async_error=1;return !(fds_delete_sync(1,1)==-1&&steps==1);
    case 12:gc_async_error=1;return !(!fds_wipe()&&gcs==1);
    case 13:exists=1;close_error=1;return !(!fds_read_sync(1,1,&length,(uint8_t*)data)&&length==0&&closes==1);
    case 14:exists=1;return !(fds_delete_sync(1,1)==1&&deleted==1);
    case 15:{host_expected_app_error=1;fds_evt_t event={.id=FDS_EVT_INIT,.result=37};callback(&event);return host_reported_app_error!=37;}
    case 16:{fds_evt_t event={.id=FDS_EVT_INIT,.result=NRF_SUCCESS};callback(&event);return host_reported_app_error!=0;}
    case 17:exists=1;submit_error=1;return !(fds_delete_sync(1,1)==-1&&exists==1&&!steps);
    case 18:drop_events=1;if(!setjmp(watchdog_reset)){fds_write_sync(1,1,8,data);return 1;}
        return !(pending&&submitted==data&&fds_operation_info.waiting&&!fds_operation_info.done&&!watchdog_feeds);
    case 19:exists=1;wrong_first=1;return !(fds_delete_sync(1,1)==1&&deleted==1&&steps==2);
    case 20:exists=1;iterate_error=17;return !(!fds_wipe()&&exists&&!deleted&&!gcs);
    case 21:exists=1;if(fds_record_lookup_sync(1,2)!=FDS_LOOKUP_PRESENT)return 1;
        exists=0;if(fds_record_lookup_sync(1,2)!=FDS_LOOKUP_ABSENT)return 2;
        lookup_error=17;return !(fds_record_lookup_sync(1,2)==FDS_LOOKUP_ERROR&&!fds_is_exists(1,2));
    case 22:lookup_error=17;return !(fds_delete_sync(1,1)==-1&&!steps);
    case 23:lookup_error=17;return !(!fds_write_sync(1,1,8,data)&&!writes&&!steps);
    case 24:if(fds_record_count_sync(1,1)!=0)return 1;
        exists=2;if(fds_record_count_sync(1,1)!=2)return 2;
        lookup_error=17;return !(fds_record_count_sync(1,1)==-1&&!steps);
    case 25:exists=2;return !(fds_delete_sync(1,1)==2&&deleted==2&&!exists&&steps==2);
    default:return 98;
    }
}
'''

class FdsTerminalTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp=tempfile.TemporaryDirectory(prefix='fds-terminal-');cls.addClassCleanup(cls.temp.cleanup)
        tmp=Path(cls.temp.name)
        source=(ROOT/'firmware/application/src/utils/fds_util.c').read_text()
        source=re.sub(r'^#include[^\n]*','',source,flags=re.M)
        (tmp/'fds.c').write_text(SHIM+source+DRIVER)
        run=subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(tmp/'fds.c'),'-o',str(tmp/'fds')],capture_output=True,text=True)
        if run.returncode:raise RuntimeError(run.stderr)
        cls.binary=tmp/'fds'
    def test_terminal_ownership_and_failure_matrix(self):
        for case in [*range(1,18),19,20,21,22,23,24,25]:
            with self.subTest(case=case):
                result=subprocess.run([str(self.binary),str(case)],capture_output=True,text=True,timeout=3)
                self.assertEqual(result.returncode,0,result.stderr)

    def test_lost_callback_keeps_buffer_owned_until_watchdog_boundary(self):
        # The external watchdog is modeled at tick5. This checks the actual
        # wait neither feeds it nor returns its caller-owned buffer beforehand;
        # physical watchdog timing/reset and flash recovery remain untested.
        result=subprocess.run([str(self.binary),'18'],capture_output=True,text=True,timeout=3)
        self.assertEqual(result.returncode,0,result.stderr)

if __name__=='__main__':unittest.main()
