#ifndef __SDLOG_SERVICE_H__
#define __SDLOG_SERVICE_H__

// ----------
// SDLOG_FMT_TEST__DATA_TYPE
// ----------
// FMT_TEXT is officially supported by the framework, so we defined its enum here
enum sdlog_fmt_text__data_type {
    SDLOG_FMT_TEXT__STRING = 0,
};

// ----------
// SDLOG_SOURCE
// ----------
enum sdlog_source_e {
#define SDLOG_SOURCE_REG(_name, _fd_name, _fmt) SDLOG_SOURCE_##_name,
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
uint32_t sdlog_source_ready(uint32_t source);

// ----------
// WEBUI API
// ----------
typedef struct sdlog_webui_status_s {
    const char *name;
    uint32_t is_logging;
    uint32_t bytes_written;
} sdlog_webui_status_t;

uint32_t sdlog_webui_query(uint32_t source, sdlog_webui_status_t *p_status);

#endif // __SDLOG_SERVICE_H__
