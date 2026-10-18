#include "fw_info.h"
#include <string.h>
#include <stdio.h>

#if defined(ROLE_CONTROLLER)
  #define FW_ROLE FW_ROLE_CONTROLLER
#else
  #define FW_ROLE FW_ROLE_REMOTE
#endif

__attribute__((used, section(".rodata_custom_desc")))
const FwInfo g_fwInfo = {
    FW_INFO_MAGIC,
    FW_ROLE,
    FW_VERSION,
    __DATE__ " " __TIME__,
};

bool fwInfoCheck(const uint8_t* img, size_t len, const char* role,
                 char* err, size_t errLen) {
    if (len < FW_INFO_OFFSET + sizeof(FwInfo)) {
        snprintf(err, errLen, "file too small");
        return false;
    }
    if (img[0] != 0xE9) {
        snprintf(err, errLen, "not an ESP32 firmware image");
        return false;
    }
    FwInfo info;
    memcpy(&info, img + FW_INFO_OFFSET, sizeof(info));
    if (memcmp(info.magic, FW_INFO_MAGIC, sizeof(FW_INFO_MAGIC)) != 0) {
        snprintf(err, errLen, "not a HeatPump firmware");
        return false;
    }
    info.role[sizeof(info.role) - 1] = '\0';
    if (strcmp(info.role, role) != 0) {
        snprintf(err, errLen, "wrong board: file is for %s", info.role);
        return false;
    }
    return true;
}
