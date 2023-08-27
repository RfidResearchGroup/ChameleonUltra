#ifndef DATA_CMD_H
#define DATA_CMD_H


// ******************************************************************
//                      CMD for device
//                  Range from 1000 -> 1999
// ******************************************************************
//
#define DATA_CMD_GET_APP_VERSION                (1000)
#define DATA_CMD_CHANGE_DEVICE_MODE             (1001)
#define DATA_CMD_GET_DEVICE_MODE                (1002)
#define DATA_CMD_SET_SLOT_ACTIVATED             (1003)
#define DATA_CMD_SET_SLOT_TAG_TYPE              (1004)
#define DATA_CMD_SET_SLOT_DATA_DEFAULT          (1005)
#define DATA_CMD_SET_SLOT_ENABLE                (1006)
#define DATA_CMD_SET_SLOT_TAG_NICK              (1007)
#define DATA_CMD_GET_SLOT_TAG_NICK              (1008)
#define DATA_CMD_SLOT_DATA_CONFIG_SAVE          (1009)
#define DATA_CMD_ENTER_BOOTLOADER               (1010)
#define DATA_CMD_GET_DEVICE_CHIP_ID             (1011)
#define DATA_CMD_GET_DEVICE_ADDRESS             (1012)
#define DATA_CMD_SAVE_SETTINGS                  (1013)
#define DATA_CMD_RESET_SETTINGS                 (1014)
#define DATA_CMD_SET_ANIMATION_MODE             (1015)
#define DATA_CMD_GET_ANIMATION_MODE             (1016)
#define DATA_CMD_GET_GIT_VERSION                (1017)
#define DATA_CMD_GET_ACTIVE_SLOT                (1018)
#define DATA_CMD_GET_SLOT_INFO                  (1019)
#define DATA_CMD_WIPE_FDS                       (1020)
#define DATA_CMD_GET_ENABLED_SLOTS              (1023)
#define DATA_CMD_DELETE_SLOT_SENSE_TYPE         (1024)
#define DATA_CMD_GET_BATTERY_INFO               (1025)
#define DATA_CMD_GET_BUTTON_PRESS_CONFIG        (1026)
#define DATA_CMD_SET_BUTTON_PRESS_CONFIG        (1027)
#define DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG   (1028)
#define DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG   (1029)
#define DATA_CMD_SET_BLE_CONNECT_KEY_CONFIG     (1030)
#define DATA_CMD_GET_BLE_CONNECT_KEY_CONFIG     (1031)
#define DATA_CMD_DELETE_ALL_BLE_BONDS           (1032)

//
// ******************************************************************


// ******************************************************************
//                      CMD for hf reader
//                  Range from 2000 -> 2999
// ******************************************************************
//
#define DATA_CMD_SCAN_14A_TAG                   (2000)
#define DATA_CMD_MF1_SUPPORT_DETECT             (2001)
#define DATA_CMD_MF1_NT_LEVEL_DETECT            (2002)
#define DATA_CMD_MF1_DARKSIDE_DETECT            (2003)
#define DATA_CMD_MF1_DARKSIDE_ACQUIRE           (2004)
#define DATA_CMD_MF1_NT_DIST_DETECT             (2005)
#define DATA_CMD_MF1_NESTED_ACQUIRE             (2006)
#define DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK        (2007)
#define DATA_CMD_MF1_READ_ONE_BLOCK             (2008)
#define DATA_CMD_MF1_WRITE_ONE_BLOCK            (2009)
//
// ******************************************************************


// ******************************************************************
//                      CMD for lf reader
//                  Range from 3000 -> 3999
// ******************************************************************
//
#define DATA_CMD_SCAN_EM410X_TAG                (3000)
#define DATA_CMD_WRITE_EM410X_TO_T5577          (3001)
//
// ******************************************************************


// ******************************************************************
//                      CMD for hf emulator
//                  Range from 4000 -> 4999
// ******************************************************************
//
#define DATA_CMD_LOAD_MF1_EMU_BLOCK_DATA        (4000)
#define DATA_CMD_SET_MF1_ANTI_COLLISION_RES     (4001)
#define DATA_CMD_SET_MF1_ANTICOLLISION_INFO     (4002)
#define DATA_CMD_SET_MF1_ATS_RESOURCE           (4003)
#define DATA_CMD_SET_MF1_DETECTION_ENABLE       (4004)
#define DATA_CMD_GET_MF1_DETECTION_COUNT        (4005)
#define DATA_CMD_GET_MF1_DETECTION_RESULT       (4006)
#define DATA_CMD_GET_MF1_DETECTION_STATUS       (4007)
#define DATA_CMD_READ_MF1_EMU_BLOCK_DATA        (4008)
#define DATA_CMD_GET_MF1_EMULATOR_CONFIG        (4009)
#define DATA_CMD_GET_MF1_GEN1A_MODE             (4010)
#define DATA_CMD_SET_MF1_GEN1A_MODE             (4011)
#define DATA_CMD_GET_MF1_GEN2_MODE              (4012)
#define DATA_CMD_SET_MF1_GEN2_MODE              (4013)
#define DATA_CMD_GET_MF1_USE_FIRST_BLOCK_COLL   (4014)
#define DATA_CMD_SET_MF1_USE_FIRST_BLOCK_COLL   (4015)
#define DATA_CMD_GET_MF1_WRITE_MODE             (4016)
#define DATA_CMD_SET_MF1_WRITE_MODE             (4017)
//
// ******************************************************************


// ******************************************************************
//                      CMD for lf emulator
//                  Range from 5000 -> 5999
// ******************************************************************
//

//
// ******************************************************************
#define DATA_CMD_SET_EM410X_EMU_ID              (5000)
#define DATA_CMD_GET_EM410X_EMU_ID              (5001)

#endif
