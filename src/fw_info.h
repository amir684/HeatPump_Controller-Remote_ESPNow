#pragma once
#include <stdint.h>
#include <stddef.h>

// ═══════════════════════════════════════════════════════════
//  Firmware identity — embedded at a fixed offset in every .bin
//  (".rodata_custom_desc" follows esp_app_desc_t), so an uploaded
//  image can be checked for the right board before it is flashed.
// ═══════════════════════════════════════════════════════════

#define FW_VERSION      "1.1.0"

#define FW_INFO_MAGIC   "HPFWINF"
#define FW_INFO_OFFSET  0x120     // image header 0x18 + segment header 8 + app desc 0x100

#define FW_ROLE_CONTROLLER "CONTROLLER"
#define FW_ROLE_REMOTE     "REMOTE"

struct FwInfo {
    char magic[8];
    char role[12];
    char version[16];
    char built[24];
};

extern const FwInfo g_fwInfo;

// Checks the start of a firmware image (at least FW_INFO_OFFSET + sizeof(FwInfo)
// bytes) for the ESP image magic and the expected role. On failure writes a
// short reason into err.
bool fwInfoCheck(const uint8_t* img, size_t len, const char* role,
                 char* err, size_t errLen);
