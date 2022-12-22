/**
 * NOTICE
 * 
 * Copyright 2020 Tile Inc.  All Rights Reserved.
 * All code or other information included in the accompanying files ("Tile Source Material")
 * is PROPRIETARY information of Tile Inc. ("Tile") and access and use of the Tile Source Material
 * is subject to these terms. The Tile Source Material may only be used for demonstration purposes,
 * and may not be otherwise distributed or made available to others, including for commercial purposes.
 * Without limiting the foregoing , you understand and agree that no production use
 * of the Tile Source Material is allowed without a Tile ID properly obtained under a separate
 * agreement with Tile.
 * You also understand and agree that Tile may terminate the limited rights granted under these terms
 * at any time in its discretion.
 * All Tile Source Material is provided AS-IS without warranty of any kind.
 * Tile does not warrant that the Tile Source Material will be error-free or fit for your purposes.
 * Tile will not be liable for any damages resulting from your use of or inability to use
 * the Tile Source Material.
 *
 * Support: firmware_support@tile.com
 */

/**
 * @file tile_gatt_db.c
 * @brief Set up the Tile GATT service
 */
#include "sdk_common.h"
#if NRF_MODULE_ENABLED(TILE_SUPPORT)
#include "tile_gatt_db.h"

#include "app_error.h"
#include "ble.h"
#include "tile_lib.h"
#include <stdint.h>

uint16_t tile_get_adv_uuid(void);

/**
 * @brief Initialize Tile GATT database
 *
 * @param[out] p_service   Service structure. Will be populated with handles.
 */
void tile_gatt_db_init(tile_gatt_db_t *p_service)
{
  uint32_t err_code;

  /* Add Tile service */
  ble_uuid_t ble_uuid;

  BLE_UUID_BLE_ASSIGN(ble_uuid, TILE_ACTIVATED_UUID);

  /* Add Tile base UUID */
  uint8_t ble_type;
  ble_uuid128_t base_uuid = TILE_SVC_BASE_UUID;
  err_code = sd_ble_uuid_vs_add(&base_uuid, &ble_type);

  APP_ERROR_CHECK(err_code);

  err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &ble_uuid, &p_service->service_handle);
  APP_ERROR_CHECK(err_code);

  /**************************
   * Tile ID characteristic *
   **************************/

  ble_uuid_t tile_id_uuid = 
  {
    .uuid = 0x0007,
    .type = ble_type
  };

  ble_gatts_char_md_t tile_id_char_md = 
  {
    .char_props = 
    {
      .read = 1,                         /* Tile ID is read only */
    }
  };

  ble_gatts_attr_md_t tile_id_attr_md = 
  {
    .read_perm  = {1,1},                 /* Sec mode open */
    .vloc       = BLE_GATTS_VLOC_STACK   /* Allocate the value in the SoftDevice */
  };

  uint8_t id[8] = {0};
  ble_gatts_attr_t tile_id_value = 
  {
    .p_uuid     = &tile_id_uuid,         /* Tile ID UUID */
    .p_attr_md  = &tile_id_attr_md,      /* Attribute metadata */
    .init_len   = TILE_ID_LEN,           /* Initial length */
    .init_offs  = 0,                     /* No offset */
    .max_len    = TILE_ID_LEN,           /* Maximum length */
    .p_value    = id                     /* Zero array as initial value */
  };

  ble_gatts_char_handles_t char_handles;
  err_code = sd_ble_gatts_characteristic_add(p_service->service_handle,
                                            &tile_id_char_md, 
                                            &tile_id_value,
                                            &char_handles);
  APP_ERROR_CHECK(err_code);

  /* Save handle */
  p_service->characteristic_handles[TILE_ID_CHAR] = char_handles.value_handle;

  /**************************
   * TOA CMD characteristic *
   **************************/

  ble_uuid_t toa_cmd_uuid = 
  {
    .uuid = 0x18,
    .type = ble_type
  };

  ble_gatts_char_md_t toa_cmd_char_md = 
  {
    .char_props = 
    {
      .write_wo_resp = 1,                /* TOA CMD is write w/o response */
    }
  };

  ble_gatts_attr_md_t toa_cmd_attr_md = 
  {
    .write_perm = {1,1},                 /* Sec mode open */
    .vlen       = 1,                     /* This is a variable length attribute */
    .vloc       = BLE_GATTS_VLOC_STACK   /* Allocate the value in the SoftDevice */
  };

  ble_gatts_attr_t toa_cmd_value = 
  {
    .p_uuid     = &toa_cmd_uuid,         /* TOA CMD UUID */
    .p_attr_md  = &toa_cmd_attr_md,      /* Attribute metadata */
    .init_len   = 0,                     /* Initially zero length */
    .init_offs  = 0,                     /* No offset */
    .max_len    = TILE_TOA_CMD_CHAR_LEN, /* Maximum length */
    .p_value    = NULL                   /* No initial value */
  };

  err_code = sd_ble_gatts_characteristic_add(p_service->service_handle,
                                            &toa_cmd_char_md, 
                                            &toa_cmd_value,
                                            &char_handles);
  APP_ERROR_CHECK(err_code);

  /* Save value handle */
  p_service->characteristic_handles[TILE_TOA_CMD_CHAR] = char_handles.value_handle;


  /**************************
   * TOA RSP characteristic *
   **************************/

  ble_uuid_t toa_rsp_uuid = 
  {
    .uuid = 0x19,
    .type = ble_type
  };

  ble_gatts_attr_md_t toa_rsp_cccd_md = 
  {
    .read_perm  = {1,1},                 /* Sec mode open */
    .write_perm = {1,1},                 /* Sec mode open */
    .vloc       = BLE_GATTS_VLOC_STACK   /* Value stored in SoftDevice */
  };

  ble_gatts_char_md_t toa_rsp_char_md = {
    .char_props = 
    {
      .notify   = 1,                     /* TOA RSP uses notifications */
    },
    .p_cccd_md  = &toa_rsp_cccd_md
  };

  ble_gatts_attr_md_t toa_rsp_attr_md = 
  {
    .read_perm  = {1,1},                 /* Sec mode open */
    .vlen       = 1,                     /* Variable length attribute */
    .vloc       = BLE_GATTS_VLOC_STACK   /* Value stored in SoftDevice */
  };

  ble_gatts_attr_t toa_rsp_value = 
  {
    .p_uuid     = &toa_rsp_uuid,         /* TOA RSP UUID */
    .p_attr_md  = &toa_rsp_attr_md,      /* Attribute metadata */
    .init_len   = 0,                     /* Initially zero length */
    .init_offs  = 0,                     /* No offset */
    .max_len    = TILE_TOA_CMD_CHAR_LEN, /* Maximum length */
    .p_value    = NULL                   /* No initial value */
  };

  err_code = sd_ble_gatts_characteristic_add(p_service->service_handle,
                                            &toa_rsp_char_md, 
                                            &toa_rsp_value,
                                            &char_handles);
  APP_ERROR_CHECK(err_code);

  p_service->characteristic_handles[TILE_TOA_RSP_CHAR] = char_handles.value_handle;
  p_service->characteristic_handles[TILE_TOA_RSP_CCCD] = char_handles.cccd_handle;
}

#endif // NRF_MODULE_ENABLED(TILE_SUPPORT)
