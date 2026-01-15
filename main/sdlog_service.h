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

#endif // __SDLOG_SERVICE_H__
