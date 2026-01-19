#ifndef __SDLOG_SERVICE_H__
#define __SDLOG_SERVICE_H__

// ----------
// SDLOG_FMT
// ----------
enum { // declare what the FMT that the producer is using
    SDLOG_FMT_TEXT = 0,
    SDLOG_FMT_CAN  = 1,
    SDLOG_FMT_ADC  = 2,
};

// ----------
// SDLOG_DATA_TYPE
// ----------

enum {
    SDLOG_DATA_TYPE_STRING = 0, // common for all channels
    // I think these data types were actually per-module dependent, and should not be fully expanded here...
};

// ----------
// SDLOG_SOURCE
// ----------
enum sdlog_source_e {
#define SDLOG_SOURCE_REG(_name, _fd_name, _type_ch) SDLOG_SOURCE_##_name,
#include "sdlog_source_reg.h"
#undef SDLOG_SOURCE_REG
    SDLOG_SOURCE_NUM,
};

// ----------
// Common API
// ----------
void sdlog_start(uint32_t source, uint64_t epoch_time);
void sdlog_stop(uint32_t source);
void sdlog_write(uint32_t source, uint32_t type_data, uint32_t len, const void *payload);

#endif // __SDLOG_SERVICE_H__
