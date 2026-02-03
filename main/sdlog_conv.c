#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
// #include <dirent.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "driver/twai.h" // for CAN PACKET analyzing, further spliting this to another file later

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_check.h"

#include "board.h"
#include "sdlog_header.h"
#include "sdlog_conv.h"

static const char *TAG = "SDLOG_CONV";

#define SDLOG_CONV_QUEUE_DEPTH (8)
#define SDLOG_CONV_FILE_BUF_SZ (8192)

QueueHandle_t sdlog_conv_task_msgq;

#define SDLOG_SOURCE(x) (&sdlog_ctrl.source[x])

// ----------
// Interface between SDLOG_TASK/ SDLOG_CONV_TASK
// ----------
typedef struct sdlog_conv_msg_s {
    char log_path[64];
    // in the future, we can extend this interface to allow users specifying the desired converter
} sdlog_conv_task_msg_t;

// ----------
// SDLOG CONV TASK IMPLEMENTATION
// ----------
typedef struct sdlog_exporter_para_s {
    FILE *fp_in;
    FILE *fp_out;
    uint64_t us_epoch_time;
    uint64_t us_sys_time;
} sdlog_exporter_para_t;

typedef struct sdlog_exporter_s {
    uint32_t bmp_fmt_supported;
    esp_err_t (*cb)(sdlog_exporter_para_t *p_para);
    char *fn_output;
} sdlog_exporter_t;

#define SDLOG_EXPORTER_REG(_name, _bmp_fmt_supported, _fn_output, _cb) extern esp_err_t(_cb)(sdlog_exporter_para_t * p_para);
#include "sdlog_exporter_reg.h"
#undef SDLOG_EXPORTER_REG

sdlog_exporter_t sdlog_exporter[SDLOG_EXPORTER_NUM] = {
#define SDLOG_EXPORTER_REG(_name, _bmp_fmt_supported, _fn_output, _cb) [SDLOG_EXPORTER_##_name] = (sdlog_exporter_t){ \
                                                                           .bmp_fmt_supported = (_bmp_fmt_supported), \
                                                                           .cb                = (_cb),                \
                                                                           .fn_output         = (_fn_output),         \
                                                                       },
#include "sdlog_exporter_reg.h"
#undef SDLOG_EXPORTER_REG
};

static void _sdlog_exporter_fp_in_padding(FILE *fp_in, uint32_t payload_len)
{
    uint32_t pad_len = (payload_len + 7) / 8 * 8 - payload_len;
    if (pad_len) {
        char buf[8];
        fread(buf, pad_len, 1, fp_in);
    }
}

esp_err_t sdlog_exporter_text(sdlog_exporter_para_t *p_para)
{
    // Move cursor to the begin-of-data
    if (fseek(p_para->fp_in, sizeof(sdlog_header_t), SEEK_SET) != 0) { // skip gloal header
        return ESP_FAIL;
    }

    sdlog_data_t entry;
    while (fread(&entry, sizeof(sdlog_data_t), 1, p_para->fp_in) == 1) {
        if (entry.magic != 0xA5) { // ensure the magic byte sync
            ESP_LOGI(TAG, "magic_mismatch()");
            return ESP_FAIL; // we don't expect this happened
        }

        // calculate abs time, and write to file
        fprintf(p_para->fp_out, "[%" PRIu64 "] ", p_para->us_epoch_time + (entry.us_sys_time - p_para->us_sys_time));

        // write to the file
        uint32_t remaining_bytes = entry.payload_len;
        uint32_t last_char       = 0;
        while (remaining_bytes) {
            uint8_t payload_buf[256]; // read maximum 256byte one time

            uint32_t read_bytes = (remaining_bytes >= sizeof(payload_buf)) ? sizeof(payload_buf) : remaining_bytes;
            size_t n            = fread(payload_buf, 1, read_bytes, p_para->fp_in);

            if (n > 0) {
                fwrite(payload_buf, read_bytes, 1, p_para->fp_out);
                remaining_bytes -= read_bytes;
                last_char = payload_buf[read_bytes - 1];
            } else {
                return ESP_FAIL;
            }
        }
        if (last_char != '\n') {
            fputc(p_para->fp_out, '\n');
        }

        // Handle padding, 8byte align
        _sdlog_exporter_fp_in_padding(p_para->fp_in, entry.payload_len);
    }

    return ESP_OK;
}

// ----------
// EXPORTER: CAN
// ----------

// Declare a data structure to merge data/payload read at the same time
// - sdlog_data_t
// - twai_message_t
// - padding

struct twai_data_entry_payload_s {
    sdlog_data_t h;
    twai_message_t can_msg;
};

struct twai_data_entry_s {
    struct twai_data_entry_payload_s payload;
    uint8_t padding[sizeof(struct twai_data_entry_payload_s) % 8];
};

esp_err_t sdlog_exporter_can(sdlog_exporter_para_t *p_para)
{
    // Move cursor to the begin-of-data
    if (fseek(p_para->fp_in, sizeof(sdlog_header_t), SEEK_SET) != 0) { // skip gloal header
        return ESP_FAIL;
    }

    struct twai_data_entry_s buf;
    sdlog_data_t *p_h     = &buf.payload.h;
    twai_message_t *p_can = &buf.payload.can_msg;

    while (fread(&buf, sizeof(buf), 1, p_para->fp_in) == 1) {
        if (p_h->magic != 0xA5) {
            ESP_LOGE(TAG, "CAN Exporter: Magic mismatch!");
            return ESP_FAIL;
        }

        char line_buf[128];

        uint64_t abs_us = p_para->us_epoch_time + (p_h->us_sys_time - p_para->us_sys_time); // calculate absolute micro-second
        uint32_t len    = snprintf(line_buf, sizeof(line_buf), "(%llu.%06llu) can1 %03lX [%d] ",
               (abs_us / 1000000), (abs_us % 1000000), p_can->identifier, p_can->data_length_code);

        char *p = line_buf + len;
        for (uint32_t i = 0; i < p_can->data_length_code; i++) {
            static const char hex_table[] = "0123456789ABCDEF";

            uint8_t b = p_can->data[i];
            *p++      = hex_table[(b >> 4) & 0xF];
            *p++      = hex_table[(b >> 0) & 0xF];
            *p++      = ' ';
        }

        if (p_can->data_length_code) {
            p--; // the code above generated one redundant space, if any bytes available, remove it
        }

        *p++ = '\n';
        fwrite(line_buf, p - line_buf, 1, p_para->fp_out);
    }

    return ESP_OK;
}

static uint8_t sdlog_conv_def_exporter[] = {
    [SDLOG_FMT_TEXT] = SDLOG_EXPORTER_TEXT,
    [SDLOG_FMT_CAN]  = SDLOG_EXPORTER_CAN,
    [SDLOG_FMT_ADC]  = SDLOG_EXPORTER_TEXT, // actually not supported yet
};

static void sdlog_conv_task(void *param)
{
    sdlog_conv_task_msg_t msg;

    while (1) {
        if (xQueueReceive(sdlog_conv_task_msgq, &msg, portMAX_DELAY) == pdPASS) {
            uint32_t step      = 1;
            uint64_t conv_time = 0;
            FILE *fp_in        = NULL;
            FILE *fp_out       = NULL;
            void *iobuf_in     = NULL;
            void *iobuf_out    = NULL;
            do {
                sdlog_header_sys_t sdlog_header;

                // Open the binary file
                step++;
                if ((fp_in = fopen(msg.log_path, "rb")) == NULL) {
                    break;
                }
                iobuf_in = malloc(SDLOG_CONV_FILE_BUF_SZ);
                if (iobuf_in) {
                    setvbuf(fp_in, iobuf_in, _IOFBF, SDLOG_CONV_FILE_BUF_SZ); // set the wbuf of the FILE*, it writes to the SD card every 4KB
                }

                // Read whole header image locally,
                step++;
                if (fread(&sdlog_header, 1, sizeof(sdlog_header), fp_in) != sizeof(sdlog_header)) {
                    break;
                }

                if (strcmp(sdlog_header.magic, "QQMLAB")) {
                    ESP_LOGW(TAG, "LOG header check fail"); // TODO: strengthen the log binary checker
                    break;
                }

                // Read fmt, retrieve the exporter pointer, and check whether the format is supported
                step++;
                uint32_t fmt                 = sdlog_header.fmt;
                sdlog_exporter_t *p_exporter = &sdlog_exporter[sdlog_conv_def_exporter[fmt]];
                if ((p_exporter->bmp_fmt_supported & (1 << fmt)) == 0) {
                    break;
                }

                // Generate the output filename
                step++;
                char *last_slash = strrchr(msg.log_path, '/');
                if (last_slash == NULL) {
                    break;
                }
                char full_path[256];
                size_t dir_len = last_slash - msg.log_path + 1; // calculate the dir length (including last '/')
                if (dir_len < sizeof(msg.log_path)) {
                    memcpy(full_path, msg.log_path, dir_len); // copy the directory path
                    full_path[dir_len] = '\0';
                    strcat(full_path, p_exporter->fn_output); // append the filename
                }

                // Open the output file & allocate file buffer
                step++;
                if ((fp_out = fopen(full_path, "wb")) == NULL) {
                    break;
                }

                // set the output file buffer
                step++;
                iobuf_out = malloc(SDLOG_CONV_FILE_BUF_SZ);
                if (iobuf_out) {
                    setvbuf(fp_out, iobuf_out, _IOFBF, SDLOG_CONV_FILE_BUF_SZ); // set the wbuf of the FILE*, it writes to the SD card every 4KB
                }

                // Call the converter API
                step++;
                uint64_t conv_begin = esp_timer_get_time();

                esp_err_t conv_result = p_exporter->cb(&(sdlog_exporter_para_t){
                    .fp_in         = fp_in,
                    .fp_out        = fp_out,
                    .us_epoch_time = sdlog_header.us_epoch_time,
                    .us_sys_time   = sdlog_header.us_sys_time,
                });

                conv_time = esp_timer_get_time() - conv_begin;
                if (conv_result != ESP_OK) {
                    break;
                }

                step = 0; // success, set step to 0
            } while (0);

            // clean up resources
            if (fp_in) {
                fclose(fp_in);
                fp_in = NULL;
            }
            if (fp_out) {
                fclose(fp_out);
                fp_out = NULL;
            }
            if (iobuf_in) {
                free(iobuf_in);
                iobuf_in = NULL;
            }
            if (iobuf_out) {
                free(iobuf_out);
                iobuf_out = NULL;
            }
            ESP_LOGI(TAG, "sdlog_conv_task(), fn=%s, status=%s(%d) conv_time=%lld", msg.log_path, (step == 0) ? "Success" : "Fail", step, conv_time);
        }
    }
}

void sdlog_conv_task_init(void)
{
    sdlog_conv_task_msgq = xQueueCreate(SDLOG_CONV_QUEUE_DEPTH, sizeof(sdlog_conv_task_msg_t));
    assert(sdlog_conv_task_msgq);

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

void sdlog_conv_trig(char *path)
{
    sdlog_conv_task_msg_t msg;
    strlcpy(msg.log_path, path, sizeof(msg.log_path));
    xQueueSend(sdlog_conv_task_msgq, &msg, 0); // block time = 0
}
