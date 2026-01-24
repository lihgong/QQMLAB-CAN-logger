#ifndef __SDLOG_CONV_H__
#define __SDLOG_CONV_H__

#include "sdlog_service_private.h"

enum sdlog_exporter_e {
#define SDLOG_EXPORTER_REG(_name, _bmp_fmt_supported, _fn_output, _cb) SDLOG_EXPORTER_##_name,
#include "sdlog_exporter_reg.h"
#undef SDLOG_EXPORTER_REG
    SDLOG_EXPORTER_NUM,
};

void sdlog_conv_task_init(void);
void sdlog_conv_trig(char *path);

#endif // __SDLOG_CONV_H__
