#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sd_log_service.h"

#include "board.h"

#define SD_LOG_ROOT (MNT_SDCARD "/log")

// ----------
// data structure definition
// ----------
typedef struct sd_log_ctrl_ch_s {
    char *name;
    uint16_t sn;
} sd_log_ctrl_ch_t;

typedef struct sd_log_ctrl_s {
    char *root;
    uint32_t num_ch;
    sd_log_ctrl_ch_t ch[SD_LOG_CH_NUM];
} sd_log_ctrl_t;

sd_log_ctrl_t sd_log_ctrl = {
    .root   = SD_LOG_ROOT,
    .num_ch = SD_LOG_CH_NUM,
    .ch     = {
#define SD_LOG_CH(_name, _fd_name) [SD_LOG_CH_##_name] = (sd_log_ctrl_ch_t){.name = _fd_name},
#include "sd_log_reg.h"
#undef SD_LOG_CH
    },
};

static void sd_log_service_create_fd(uint32_t ch)
{
    struct stat st;
    char full_path[64];
    snprintf(full_path, sizeof(full_path), "%s/%s", sd_log_ctrl.root, sd_log_ctrl.ch[ch].name);
    if (stat(full_path, &st) == -1) {
        mkdir(full_path, 0700); // In FatFS, mode parameter (0700) is actually ignored, but it's a good habit to keep it
    }
}

void sd_log_service_init(void)
{
    mkdir(sd_log_ctrl.root, 0700); // create root log folder unconditionally

    for (uint32_t i = 0; i < sd_log_ctrl.num_ch; i++) {
        sd_log_service_create_fd(i);
    }
}
