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
#define DATA_CMD_SET_ACTIVE_SLOT                (1003)
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
#define DATA_CMD_DELETE_SLOT_TAG_NICK           (1021)

#define DATA_CMD_GET_ENABLED_SLOTS              (1023)
#define DATA_CMD_DELETE_SLOT_SENSE_TYPE         (1024)
#define DATA_CMD_GET_BATTERY_INFO               (1025)
#define DATA_CMD_GET_BUTTON_PRESS_CONFIG        (1026)
#define DATA_CMD_SET_BUTTON_PRESS_CONFIG        (1027)
#define DATA_CMD_GET_LONG_BUTTON_PRESS_CONFIG   (1028)
#define DATA_CMD_SET_LONG_BUTTON_PRESS_CONFIG   (1029)
#define DATA_CMD_SET_BLE_PAIRING_KEY            (1030)
#define DATA_CMD_GET_BLE_PAIRING_KEY            (1031)
#define DATA_CMD_DELETE_ALL_BLE_BONDS           (1032)
#define DATA_CMD_GET_DEVICE_MODEL               (1033)
#define DATA_CMD_GET_DEVICE_SETTINGS            (1034)
#define DATA_CMD_GET_DEVICE_CAPABILITIES        (1035)
#define DATA_CMD_GET_BLE_PAIRING_ENABLE         (1036)
#define DATA_CMD_SET_BLE_PAIRING_ENABLE         (1037)
#define DATA_CMD_GET_ALL_SLOT_NICKS             (1038)

//
// ******************************************************************


// ******************************************************************
//                      CMD for hf reader
//                  Range from 2000 -> 2999
// ******************************************************************
//
#define DATA_CMD_HF14A_SCAN                     (2000)
#define DATA_CMD_MF1_DETECT_SUPPORT             (2001)
#define DATA_CMD_MF1_DETECT_PRNG                (2002)
#define DATA_CMD_MF1_STATIC_NESTED_ACQUIRE      (2003)
#define DATA_CMD_MF1_DARKSIDE_ACQUIRE           (2004)
#define DATA_CMD_MF1_DETECT_NT_DIST             (2005)
#define DATA_CMD_MF1_NESTED_ACQUIRE             (2006)
#define DATA_CMD_MF1_AUTH_ONE_KEY_BLOCK         (2007)
#define DATA_CMD_MF1_READ_ONE_BLOCK             (2008)
#define DATA_CMD_MF1_WRITE_ONE_BLOCK            (2009)
#define DATA_CMD_HF14A_RAW                      (2010)
#define DATA_CMD_MF1_MANIPULATE_VALUE_BLOCK     (2011)
#define DATA_CMD_MF1_CHECK_KEYS_OF_SECTORS      (2012)
#define DATA_CMD_MF1_HARDNESTED_ACQUIRE         (2013)
#define DATA_CMD_MF1_ENC_NESTED_ACQUIRE         (2014)
#define DATA_CMD_MF1_CHECK_KEYS_ON_BLOCK        (2015)

#define DATA_CMD_HF14A_SET_FIELD_ON             (2100)
#define DATA_CMD_HF14A_SET_FIELD_OFF            (2101)

#define DATA_CMD_HF14A_GET_CONFIG               (2200)
#define DATA_CMD_HF14A_SET_CONFIG               (2201)

//
// ******************************************************************


// ******************************************************************
//                      CMD for lf reader
//                  Range from 3000 -> 3999
// ******************************************************************
//
#define DATA_CMD_EM410X_SCAN                    (3000)
#define DATA_CMD_EM410X_WRITE_TO_T55XX          (3001)
#define DATA_CMD_EM410X_ELECTRA_WRITE_TO_T55XX  (3006)
#define DATA_CMD_HIDPROX_SCAN                   (3002)
#define DATA_CMD_HIDPROX_WRITE_TO_T55XX         (3003)
#define DATA_CMD_PAC_SCAN                       (3010)
#define DATA_CMD_VIKING_SCAN                    (3004)
#define DATA_CMD_VIKING_WRITE_TO_T55XX          (3005)
#define DATA_CMD_ADC_GENERIC_READ               (3009)
#define DATA_CMD_GENERIC_READ                   (3007)
#define DATA_CMD_CORR_GENERIC_READ              (3008)

//
// ******************************************************************


// ******************************************************************
//                      CMD for hf emulator
//                  Range from 4000 -> 4999
// ******************************************************************
//
#define DATA_CMD_MF1_WRITE_EMU_BLOCK_DATA       (4000)
#define DATA_CMD_HF14A_SET_ANTI_COLL_DATA       (4001)
#define DATA_CMD_MF1_SET_DETECTION_ENABLE       (4004)
#define DATA_CMD_MF1_GET_DETECTION_COUNT        (4005)
#define DATA_CMD_MF1_GET_DETECTION_LOG          (4006)
#define DATA_CMD_MF1_GET_DETECTION_ENABLE       (4007)
#define DATA_CMD_MF1_READ_EMU_BLOCK_DATA        (4008)
#define DATA_CMD_MF1_GET_EMULATOR_CONFIG        (4009)
#define DATA_CMD_MF1_GET_GEN1A_MODE             (4010)
#define DATA_CMD_MF1_SET_GEN1A_MODE             (4011)
#define DATA_CMD_MF1_GET_GEN2_MODE              (4012)
#define DATA_CMD_MF1_SET_GEN2_MODE              (4013)
#define DATA_CMD_MF1_GET_BLOCK_ANTI_COLL_MODE   (4014)
#define DATA_CMD_MF1_SET_BLOCK_ANTI_COLL_MODE   (4015)
#define DATA_CMD_MF1_GET_WRITE_MODE             (4016)
#define DATA_CMD_MF1_SET_WRITE_MODE             (4017)
#define DATA_CMD_HF14A_GET_ANTI_COLL_DATA       (4018)
#define DATA_CMD_MF0_NTAG_GET_UID_MAGIC_MODE    (4019)
#define DATA_CMD_MF0_NTAG_SET_UID_MAGIC_MODE    (4020)
#define DATA_CMD_MF0_NTAG_READ_EMU_PAGE_DATA    (4021)
#define DATA_CMD_MF0_NTAG_WRITE_EMU_PAGE_DATA   (4022)
#define DATA_CMD_MF0_NTAG_GET_VERSION_DATA      (4023)
#define DATA_CMD_MF0_NTAG_SET_VERSION_DATA      (4024)
#define DATA_CMD_MF0_NTAG_GET_SIGNATURE_DATA    (4025)
#define DATA_CMD_MF0_NTAG_SET_SIGNATURE_DATA    (4026)
#define DATA_CMD_MF0_NTAG_GET_COUNTER_DATA      (4027)
#define DATA_CMD_MF0_NTAG_SET_COUNTER_DATA      (4028)
#define DATA_CMD_MF0_NTAG_RESET_AUTH_CNT        (4029)
#define DATA_CMD_MF0_NTAG_GET_PAGE_COUNT        (4030)
#define DATA_CMD_MF0_NTAG_RESET_AUTH_CNT        (4029)
#define DATA_CMD_MF0_NTAG_GET_PAGE_COUNT        (4030)
#define DATA_CMD_MF0_NTAG_GET_WRITE_MODE        (4031)
#define DATA_CMD_MF0_NTAG_SET_WRITE_MODE        (4032)
#define DATA_CMD_MF0_NTAG_SET_DETECTION_ENABLE  (4033)
#define DATA_CMD_MF0_NTAG_GET_DETECTION_COUNT   (4034)
#define DATA_CMD_MF0_NTAG_GET_DETECTION_LOG     (4035)
#define DATA_CMD_MF0_NTAG_GET_DETECTION_ENABLE  (4036)
#define DATA_CMD_MF0_NTAG_GET_EMULATOR_CONFIG   (4037)
#define DATA_CMD_MF1_SET_FIELD_OFF_DO_RESET     (4038)
#define DATA_CMD_MF1_GET_FIELD_OFF_DO_RESET     (4039)
//
// ******************************************************************


// ******************************************************************
//                      CMD for lf emulator
//                  Range from 5000 -> 5999
// ******************************************************************
//

//
// ******************************************************************
#define DATA_CMD_EM410X_SET_EMU_ID              (5000)
#define DATA_CMD_EM410X_GET_EMU_ID              (5001)
#define DATA_CMD_HIDPROX_SET_EMU_ID             (5002)
#define DATA_CMD_HIDPROX_GET_EMU_ID             (5003)
#define DATA_CMD_VIKING_SET_EMU_ID              (5004)
#define DATA_CMD_VIKING_GET_EMU_ID              (5005)
#define DATA_CMD_PAC_SET_EMU_ID                 (5006)
#define DATA_CMD_PAC_GET_EMU_ID                 (5007)

#endif
