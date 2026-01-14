#ifndef __SD_LOG_SERVICE_H__
#define __SD_LOG_SERVICE_H__

// ----------
// enum definition
// ----------
enum sd_log_ch_e {
#define SD_LOG_CH(_name, _fd_name) SD_LOG_CH_##_name,
#include "sd_log_reg.h"
#undef SD_LOG_CH
    SD_LOG_CH_NUM,
};

#endif // __SD_LOG_SERVICE_H__
