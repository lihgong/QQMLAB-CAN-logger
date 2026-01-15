#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#include "esp_log.h"

#include "board.h"
#include "sdlog_service.h"

static const char *TAG = "SDLOG";
#define SDLOG_ROOT (MNT_SDCARD "/log")

// ----------
// data structure definition
// ----------
typedef struct sdlog_ctrl_ch_s {
    char *name;
    uint32_t sn;
} sdlog_ctrl_ch_t;

typedef struct sdlog_ctrl_s {
    char *root;
    uint32_t num_ch;
    sdlog_ctrl_ch_t ch[SDLOG_CH_NUM];
} sdlog_ctrl_t;

sdlog_ctrl_t sdlog_ctrl = {
    .root   = SDLOG_ROOT,
    .num_ch = SDLOG_CH_NUM,
    .ch     = {
#define SDLOG_REG(_name, _fd_name) [SDLOG_CH_##_name] = (sdlog_ctrl_ch_t){.name = _fd_name},
#include "sdlog_reg.h"
#undef SDLOG_REG
    },
};

#define SDLOG_CH(x) (&sdlog_ctrl.ch[x])

static void sdlog_service_create_fd(uint32_t ch)
{
    struct stat st;
    char full_path[64];

    // Create log folder if it doesn't exist
    snprintf(full_path, sizeof(full_path), "%s/%s", sdlog_ctrl.root, SDLOG_CH(ch)->name);
    if (stat(full_path, &st) == -1) {
        mkdir(full_path, 0700); // In FatFS, mode parameter (0700) is actually ignored, but it's a good habit to keep it
        ESP_LOGI(TAG, "Create folder %s", full_path);
    }

    // Create sn.txt if it doesn't exist
    snprintf(full_path, sizeof(full_path), "%s/%s/sn.txt", sdlog_ctrl.root, SDLOG_CH(ch)->name);
    if (stat(full_path, &st) == -1) {
        FILE *fp = fopen(full_path, "w");
        fprintf(fp, "1\n"); // serial number starts from #1, so sn=0 (default memory value) means un-init
        fclose(fp);
        ESP_LOGI(TAG, "Create file %s", full_path);
    }

    // Read and initialize the serial number
    FILE *fp = fopen(full_path, "r");
    if (fp != NULL) {
        fscanf(fp, "%" SCNu32, &SDLOG_CH(ch)->sn);
        fclose(fp);
        ESP_LOGI(TAG, "SDLOG[%s].sn = %d", SDLOG_CH(ch)->name, SDLOG_CH(ch)->sn);
    }
}

void sdlog_service_init(void)
{
    mkdir(sdlog_ctrl.root, 0700); // create root log folder unconditionally

    for (uint32_t i = 0; i < sdlog_ctrl.num_ch; i++) {
        sdlog_service_create_fd(i);
    }
}
