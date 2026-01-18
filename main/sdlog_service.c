#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#include "board.h"
#include "sdlog_service.h"
#include "sdlog_header.h"

static const char *TAG = "SDLOG";

#define SDLOG_ROOT (MNT_SDCARD "/log")
#define SDLOG_TASK_INBUF_SZ (32768)
#define SDLOG_FILE_BUF_SZ (4096)

// FIXME: in the sdlog service, we may encounter that sdcard service is not ready
// Or we may encounter the SD card inserted (currently, it will reboot forever)
// Handle them in the future. For example, if the SD card not inserted, I expect not hang
// and the service can handle it gracefully

// ----------
// data structure definition
// ----------
typedef struct sdlog_ctrl_ch_s {
    char *name;
    uint8_t type_ch;
    uint8_t reserved[3];
    uint32_t sn;
    FILE *fp;
    void *wbuf; // for setvbuf() to hold wbuf to avoid frequently writing to SD card
} sdlog_ctrl_ch_t;

typedef struct sdlog_conv_msg_s {
    uint8_t ch;
    uint8_t reserved[3];
    uint32_t sn;
} sdlog_conv_task_msg_t;

typedef struct sdlog_ctrl_s {
    char *root;
    uint32_t num_ch;
    sdlog_ctrl_ch_t ch[SDLOG_CH_NUM];

    // sdlog_task
    RingbufHandle_t sdlog_task_inbuf;

    // sdlog_conv_task
    QueueHandle_t sdlog_conv_task_msgq;

} sdlog_ctrl_t;

sdlog_ctrl_t sdlog_ctrl = {
    .root   = SDLOG_ROOT,
    .num_ch = SDLOG_CH_NUM,
    .ch     = {
#define SDLOG_REG(_name, _fd_name, _type_ch) [SDLOG_CH_##_name] = (sdlog_ctrl_ch_t){ \
                                                 .name    = (_fd_name),              \
                                                 .type_ch = (_type_ch),              \
                                             },
#include "sdlog_reg.h"
#undef SDLOG_REG
    },
};

#define SDLOG_CH(x) (&sdlog_ctrl.ch[x])

// ----------
// Operate API
// ----------
enum {
    SDLOG_CMD_START = 0,
    SDLOG_CMD_STOP,
    SDLOG_CMD_WRITE,
};

typedef struct sdlog_cmd_s {
    uint8_t ch;
    uint8_t cmd;
    uint8_t type_data; // only valid in write cmd
    uint8_t reserved[1];
    uint32_t length;
    uint64_t us_sys_time;
} sdlog_cmd_t;

void sdlog_start(uint32_t ch, uint64_t epoch_time)
{
    uint64_t current_us = esp_timer_get_time();
    uint32_t total_len  = sizeof(sdlog_cmd_t) + sizeof(current_us);

    void *p_buf;
    BaseType_t res = xRingbufferSendAcquire(sdlog_ctrl.sdlog_task_inbuf, &p_buf, total_len, 0);

    if (res == pdTRUE && p_buf) {
        sdlog_cmd_t *p_cmd = p_buf;
        p_cmd->ch          = ch;
        p_cmd->cmd         = SDLOG_CMD_START;
        p_cmd->length      = sizeof(current_us);
        p_cmd->us_sys_time = current_us;

        uint64_t *p_epoch_time = (uint64_t *)(p_buf + sizeof(sdlog_cmd_t));
        *p_epoch_time          = epoch_time;

        xRingbufferSendComplete(sdlog_ctrl.sdlog_task_inbuf, p_buf); // notify rbuf to read
    } else {
        ESP_LOGE(TAG, "RB Acquire failed, buffer full?");
    }
}

void sdlog_stop(uint32_t ch)
{
    void *p_buf;
    BaseType_t res = xRingbufferSendAcquire(sdlog_ctrl.sdlog_task_inbuf, &p_buf, sizeof(sdlog_cmd_t), 0);

    if (res == pdTRUE && p_buf) {
        sdlog_cmd_t *p_cmd = p_buf;
        p_cmd->ch          = ch;
        p_cmd->cmd         = SDLOG_CMD_STOP;
        p_cmd->length      = 0;
        p_cmd->us_sys_time = esp_timer_get_time();

        xRingbufferSendComplete(sdlog_ctrl.sdlog_task_inbuf, p_buf); // notify rbuf to read
    } else {
        ESP_LOGE(TAG, "RB Acquire failed, buffer full?");
    }
}

void sdlog_write(uint32_t ch, uint32_t type_data, uint32_t len, const void *payload)
{
    void *p_buf;
    BaseType_t res = xRingbufferSendAcquire(sdlog_ctrl.sdlog_task_inbuf, &p_buf, sizeof(sdlog_cmd_t) + len, 0);

    if (res == pdTRUE && p_buf) {
        sdlog_cmd_t *p_cmd = (sdlog_cmd_t *)p_buf;
        p_cmd->ch          = ch;
        p_cmd->cmd         = SDLOG_CMD_WRITE;
        p_cmd->type_data   = type_data;
        p_cmd->length      = len;
        p_cmd->us_sys_time = esp_timer_get_time();

        memcpy(p_buf + sizeof(sdlog_cmd_t), payload, len);

        xRingbufferSendComplete(sdlog_ctrl.sdlog_task_inbuf, p_buf); // notify rbuf to read
    } else {
        ESP_LOGE(TAG, "RB Acquire failed, buffer full?");
    }
}

// ----------
// SDLOG TASK IMPLEMENTATION
// ----------
static void _sdlog_task_openfile(sdlog_cmd_t *p_cmd, void *p_payload)
{
    sdlog_ctrl_ch_t *p_ch = SDLOG_CH(p_cmd->ch);
    if (p_ch->fp) {
        ESP_LOGI(TAG, "ch %s already opened", p_ch->name);
        return;
    }

    // create the output folder
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s/%s/%06" PRIu32, sdlog_ctrl.root, p_ch->name, p_ch->sn);
    mkdir(full_path, 0700);

    // open log file
    strcat(full_path, "/log.txt");
    ESP_LOGI(TAG, "Opened %s", full_path);
    p_ch->fp = fopen(full_path, "w");

    if (p_ch->fp == NULL) { // check whether file open success
        ESP_LOGE(TAG, "ch %s file open error", p_ch->name);
        return;
    }

    p_ch->wbuf = malloc(SDLOG_FILE_BUF_SZ);
    if (p_ch->wbuf) {
        setvbuf(p_ch->fp, p_ch->wbuf, _IOFBF, SDLOG_FILE_BUF_SZ); // set the wbuf of the FILE*, it writes to the SD card every 4KB
    }

    sdlog_header_t sdlog_header = {0};

    // sdlog_header.sys
    strncpy(sdlog_header.sys.magic, "QQMLAB", sizeof(sdlog_header.sys.magic));
    sdlog_header.sys.version       = 1;
    sdlog_header.sys.header_sz     = 512;
    sdlog_header.sys.us_epoch_time = *(uint64_t *)(p_payload);
    sdlog_header.sys.us_sys_time   = p_cmd->us_sys_time;
    sdlog_header.sys.type_ch       = p_ch->type_ch;
    strncpy(sdlog_header.sys.board_name, BOARD_NAME, sizeof(sdlog_header.sys.board_name));
    strncpy(sdlog_header.sys.firmware_ver, "20260107", sizeof(sdlog_header.sys.firmware_ver));
    sdlog_header.sys.offset_meta = 512;
    sdlog_header.sys.offset_data = 1024;

    // sdlog_header.meta
    snprintf(sdlog_header.meta.description, sizeof(sdlog_header.meta.description), "Channel: %d, Name: %s", p_cmd->ch, p_ch->name);

    // write to the file
    if (fwrite(&sdlog_header, sizeof(sdlog_header), 1, p_ch->fp) != 1) {
        return;
    }
}

static void _sdlog_task_closefile(sdlog_cmd_t *p_cmd, void *p_payload)
{
    sdlog_ctrl_ch_t *p_ch = SDLOG_CH(p_cmd->ch);

    if (p_ch->fp) {
        fclose(p_ch->fp);
        p_ch->fp = NULL;
        if (p_ch->wbuf) {
            free(p_ch->wbuf);
            p_ch->wbuf = NULL;
        }
        ESP_LOGI(TAG, "CH %s logging stopped", p_ch->name);

        sdlog_conv_task_msg_t msg = {
            .ch = p_cmd->ch,
            .sn = p_ch->sn,
        };
        xQueueSend(sdlog_ctrl.sdlog_conv_task_msgq, &msg, 0); // block time = 0
        p_ch->sn++;
    }
}

static void _sdlog_task_write(sdlog_cmd_t *p_cmd, void *p_payload)
{
    sdlog_ctrl_ch_t *p_ch = SDLOG_CH(p_cmd->ch);
    if (p_ch->fp) {
        // header
        sdlog_data_t sdlog_data = {
            .magic       = 0xA5, // magic word
            .type_data   = p_cmd->type_data,
            .reserved    = {0, 0},
            .payload_len = p_cmd->length,
            .us_sys_time = p_cmd->us_sys_time,
        };
        fwrite(&sdlog_data, sizeof(sdlog_data), 1, p_ch->fp);

        // Body
        if (p_cmd->length) {
            fwrite(p_payload, 1, p_cmd->length, p_ch->fp);
        }

        // padding
        uint32_t pad_len = (p_cmd->length + 7) / 8 * 8 - p_cmd->length;
        if (pad_len) {
            static const uint8_t padding_zeros[8] = {0};
            fwrite(padding_zeros, 1, pad_len, p_ch->fp);
        }
    }
}

void sdlog_task(void *param)
{
    while (1) {
        size_t buf_size;
        void *p_buf = xRingbufferReceive(sdlog_ctrl.sdlog_task_inbuf, &buf_size, portMAX_DELAY);
        if (p_buf) {
            sdlog_cmd_t *p_cmd = (sdlog_cmd_t *)p_buf;
            void *p_payload    = p_buf + sizeof(sdlog_cmd_t);

            if (p_cmd->cmd == SDLOG_CMD_WRITE) { // put the common case in the beginning
                _sdlog_task_write(p_cmd, p_payload);
            } else if (p_cmd->cmd == SDLOG_CMD_START) {
                _sdlog_task_openfile(p_cmd, p_payload);
            } else if (p_cmd->cmd == SDLOG_CMD_STOP) {
                _sdlog_task_closefile(p_cmd, p_payload);
            }

            vRingbufferReturnItem(sdlog_ctrl.sdlog_task_inbuf, p_buf);
        }
    }
}

void sdlog_task_init(void)
{
    sdlog_ctrl.sdlog_task_inbuf = xRingbufferCreate(SDLOG_TASK_INBUF_SZ, RINGBUF_TYPE_NOSPLIT);
    assert(sdlog_ctrl.sdlog_task_inbuf);

    BaseType_t xReturned = xTaskCreate(
        sdlog_task, // Function pointer
        "SDLOG",    // Task name
        4096,       // 4096 words (16KB), we will have a lot of large data transfer in the task, enlarge it
        (void *)0,  // Parameter passed into the task
        5,          // Priority (FIXME: where can I find the priority table)
        NULL);      // Task Hanlde, if no need, passes NULL

    if (xReturned != pdPASS) {
        ESP_LOGE("SDLOG TASK", "Failed to create task!");
    }
}

// ----------
// SDLOG CONV TASK IMPLEMENTATION
// ----------

void sdlog_conv_task(void *param)
{
    sdlog_conv_task_msg_t msg;

    while (1) {
        if (xQueueReceive(sdlog_ctrl.sdlog_conv_task_msgq, &msg, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG, "sdlog_conv_task(), ch=%d, sn=%d", msg.ch, msg.sn);
            ESP_LOGI(TAG, "sdlog_conv_task(), ch=%d, sn=%d", msg.ch, msg.sn);
            ESP_LOGI(TAG, "sdlog_conv_task(), ch=%d, sn=%d", msg.ch, msg.sn);
        }
    }
}

void sdlog_conv_task_init(void)
{
    sdlog_ctrl.sdlog_conv_task_msgq = xQueueCreate(16, sizeof(sdlog_conv_task_msg_t));
    assert(sdlog_ctrl.sdlog_conv_task_msgq);

    // sdlog_conv_task_msg_t
    BaseType_t xReturned = xTaskCreate(
        sdlog_conv_task, // Function pointer
        "SDLOG_CONV",    // Task name
        4096,            // 4096 words (16KB), we will have a lot of large data transfer in the task, enlarge it
        (void *)0,       // Parameter passed into the task
        2,               // Priority (FIXME: where can I find the priority table)
        NULL);           // Task Hanlde, if no need, passes NULL

    if (xReturned != pdPASS) {
        ESP_LOGE("SDLOG CONV TASK", "Failed to create task!");
    }
}

// ----------
// INIT API
// ----------
static uint32_t sdlog_find_max_sn(const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    assert(dir); // ensure this is not NULL

    struct dirent *entry;
    uint32_t max_sn = 0;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) {
            uint32_t current_sn = (uint32_t)strtoul(entry->d_name, NULL, 10);
            if (current_sn > max_sn) {
                max_sn = current_sn;
            }
        }
    }
    closedir(dir);
    return max_sn;
}

static void sdlog_service_create_fd(uint32_t ch)
{
    struct stat st;
    char full_path[128];

    // Create log folder if it doesn't exist
    snprintf(full_path, sizeof(full_path), "%s/%s", sdlog_ctrl.root, SDLOG_CH(ch)->name);
    if (stat(full_path, &st) == -1) {
        mkdir(full_path, 0700); // In FatFS, mode parameter (0700) is actually ignored, but it's a good habit to keep it
        ESP_LOGI(TAG, "Create folder %s", full_path);
    }

    uint32_t max_sn = sdlog_find_max_sn(full_path);
    ESP_LOGI(TAG, "%s max_sn=%" PRIu32, full_path, max_sn);

    SDLOG_CH(ch)->sn = max_sn + 1;
}

void sdlog_service_init(void)
{
    mkdir(sdlog_ctrl.root, 0700); // create root log folder unconditionally

    for (uint32_t i = 0; i < sdlog_ctrl.num_ch; i++) {
        sdlog_service_create_fd(i);
    }

    sdlog_task_init();
    sdlog_conv_task_init();
}
