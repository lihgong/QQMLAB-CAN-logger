#ifndef __SDLOG_SERVICE_H__
#define __SDLOG_SERVICE_H__

// ----------
// SDLOG_CH_TYPE
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
// enum definition
// ----------
enum sdlog_ch_e {
#define SDLOG_REG(_name, _fd_name, _type_ch) SDLOG_CH_##_name,
#include "sdlog_reg.h"
#undef SDLOG_REG
    SDLOG_CH_NUM,
};

// ----------
// Common API
// ----------
void sdlog_start(uint32_t ch, uint64_t epoch_time);
void sdlog_stop(uint32_t ch);
void sdlog_write(uint32_t ch, uint32_t type_data, uint32_t len, const void *payload);

#endif // __SDLOG_SERVICE_H__
