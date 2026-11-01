#ifndef CCID_TRANSPORT_TEST_APP_USBD_CORE_H
#define CCID_TRANSPORT_TEST_APP_USBD_CORE_H

#include "app_usbd.h"

#define APP_USBD_STATE_Default 0U
#define APP_USBD_STATE_Configured 1U
#define APP_USBD_SETUP_REQREC_INTERFACE 1U
#define APP_USBD_SETUP_REQTYPE_CLASS 1U
#define APP_USBD_SETUP_REQDIR_IN 1U
#define APP_USBD_SETUP_REQDIR_OUT 0U
#define app_usbd_setup_req_rec(value) ((value) & 0x1FU)
#define app_usbd_setup_req_typ(value) (((value) >> 5U) & 0x03U)
#define app_usbd_setup_req_dir(value) (((value) >> 7U) & 0x01U)

uint8_t app_usbd_core_state_get(void);
ret_code_t app_usbd_core_setup_rsp(const app_usbd_setup_t *setup, const void *data, size_t size);

#endif
