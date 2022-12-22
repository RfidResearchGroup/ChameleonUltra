#ifndef DATA_CMD_H
#define DATA_CMD_H


// ******************************************************************
//                      CMD for device
//                  Range from 1000 -> 1999
#define DATA_CMD_GET_APP_VERSION            (1000)
#define DATA_CMD_CHANGE_DEVICE_MODE         (1001)
#define DATA_CMD_GET_DEVICE_MODE            (1002)
#define DATA_CMD_SET_SLOT_ACTIVATED         (1003)
#define DATA_CMD_SET_SLOT_TAG_TYPE          (1004)
#define DATA_CMD_SET_SLOT_DATA_DEFAULT      (1005)
//
// ******************************************************************


// ******************************************************************
//                      CMD for hf reader
//                  Range from 2000 -> 2999
// #define DATA_CMD_XXXX    (xxxx)
//
// ******************************************************************
#define DATA_CMD_SCAN_14A_TAG               (2000)
#define DATA_CMD_MF1_SUPPORT_DETECT         (2001)
#define DATA_CMD_MF1_NT_LEVEL_DETECT        (2002)
#define DATA_CMD_MF1_DARKSIDE_DETECT        (2003)
#define DATA_CMD_MF1_DARKSIDE_ACQUIRE       (2004)
#define DATA_CMD_MF1_NT_DIST_DETECT         (2005)
#define DATA_CMD_MF1_NESTED_ACQUIRE         (2006)
#define DATA_CMD_MF1_CHECK_ONE_KEY_BLOCK    (2007)
#define DATA_CMD_MF1_READ_ONE_BLOCK         (2008)
#define DATA_CMD_MF1_WRITE_ONE_BLOCK        (2009)

// ******************************************************************
//                      CMD for lf reader
//                  Range from 3000 -> 3999
// #define DATA_CMD_XXXX    (xxxx)
//
// ******************************************************************
#define DATA_CMD_SCAN_EM410X_TAG            (3000)
#define DATA_CMD_WRITE_EM410X_TO_T5577      (3001)

#endif
