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
#define DATA_CMD_LOAD_MF1_BLOCK_DATA            (4000)
#define DATA_CMD_SET_MF1_ANTI_COLLISION_RES     (4001)
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
#define DATA_CMD_SET_MF1_ANTICOLLISION_INFO     (5001)
#define DATA_CMD_SET_MF1_ATS_RESOURCE           (5002)
#define DATA_CMD_SET_MF1_DETECTION_ENABLE       (5003)
#define DATA_CMD_GET_MF1_DETECTION_COUNT        (5004)
#define DATA_CMD_GET_MF1_DETECTION_RESULT       (5005)


#endif
