#include "device_info.h"

// version code, 1byte, max = 0 -> 255
#define BOOT_FW_VER_MAJOR 1
#define BOOT_FW_VER_MINOR 0

// Merge major and minor version code to U16 value.
#define FW_VER_NUM VER_CODE_TO_NUM(BOOT_FW_VER_MAJOR, BOOT_FW_VER_MINOR)
