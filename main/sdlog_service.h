#ifndef __SDLOG_SERVICE_H__
#define __SDLOG_SERVICE_H__

// ----------
// enum definition
// ----------
enum sdlog_ch_e {
#define SDLOG_REG(_name, _fd_name) SDLOG_CH_##_name,
#include "sdlog_reg.h"
#undef SDLOG_REG
    SDLOG_CH_NUM,
};

// ----------
// Common API
// ----------
void sdlog_start(uint32_t ch, uint64_t epoch_time);
void sdlog_stop(uint32_t ch);
void sdlog_write(uint32_t ch, uint32_t len, const void *payload);

// ----------
// Aliasing API for users to used conveniently
// ----------
#define SDLOG_HTTP_START(epoch_time) sdlog_start(SDLOG_CH_HTTP, (epoch_time))
#define SDLOG_HTTP_STOP() sdlog_stop(SDLOG_CH_HTTP)
#define SDLOG_HTTP_WRITE(len, payload) sdlog_write(SDLOG_CH_HTTP, (len), (payload))

#endif // __SDLOG_SERVICE_H__
