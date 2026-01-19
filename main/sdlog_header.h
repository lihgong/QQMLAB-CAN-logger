#ifndef __SDLOG_HEADER_H__
#define __SDLOG_HEADER_H__

#include <stdint.h>
#include <stddef.h>

// this ensure the data structure is aligned with 1byte, where compiler doesn't add padding
// if we write PC tool to examine the data structure, it guaranteed no difference between target/host
// However, to ensure the efficiency, ensure all the fields aligned correctly
#pragma pack(push, 1)

// The log header is split into two parts
// - part#1: System generic header (512bytes)
// - part#2: User metainfo header (512byte)

typedef struct sdlog_header_sys_s {
    union {
        uint8_t container[508]; // reserved 4bytes for crc32
        struct {
            char magic[8];      // FIXED TO "QQMLAB\0"
            uint32_t version;   // 1
            uint32_t header_sz; // 512

            uint64_t us_epoch_time; // The epoch time that the file creates
            uint64_t us_sys_time;   // The system time that the file creates

            uint32_t fmt; // 0:HTTP, 1:CAN, 2:ADC, ...
            char board_name[32];
            char firmware_ver[16];

            uint32_t offset_meta; // default: 512
            uint32_t offset_data; // 1024
        };
    };
    uint32_t crc32;
} sdlog_header_sys_t;

// header_fmt = "<8sIIQQI32s16sII" # 對應你的 sys.payload 結構

static_assert(sizeof(sdlog_header_sys_t) == 512, "System header size mismatch!");
static_assert(offsetof(sdlog_header_sys_t, us_epoch_time) == 16, "Offset mismatch!");

typedef struct {
    char description[512]; // eg. "CAN bus log, XC60, 20260117"
} sdlog_header_meta_t;

typedef struct sdlog_header_s {
    sdlog_header_sys_t sys;
    sdlog_header_meta_t meta;
} sdlog_header_t;

// ----------
// USER DATA header
// ----------

#pragma pack(push, 1)

typedef struct sdlog_data_s {
    uint8_t magic;        // 0xA5, the sync magic word
    uint8_t type_data;    // type, maybe TEXT as 1, CAN as 2, ...
    uint8_t reserved[2];  // reserved for future extension
    uint32_t payload_len; // Payload length, padding size = (payload_len + 7)/8*8 - payload_len
    uint64_t us_sys_time; // time-stamp, the field must align 8byte
} sdlog_data_t;

#pragma pack(pop)

#endif // __SDLOG_HEADER_H__
