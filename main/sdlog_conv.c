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
#define SDLOG_CONV_FILE_BUF_SZ (4096)

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
        fseek(fp_in, pad_len, SEEK_CUR);
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
        while (remaining_bytes) {
            uint32_t payload_buf[256]; // read maximum 256byte one time
            uint32_t read_bytes = (remaining_bytes >= sizeof(payload_buf)) ? sizeof(payload_buf) : remaining_bytes;
            if (fread(payload_buf, read_bytes, 1, p_para->fp_in) == 1) {
                fwrite(payload_buf, read_bytes, 1, p_para->fp_out);
                remaining_bytes -= read_bytes;
            } else {
                return ESP_FAIL;
            }
        }
        fprintf(p_para->fp_out, "\n");

        // Handle padding, 8byte align
        _sdlog_exporter_fp_in_padding(p_para->fp_in, entry.payload_len);
    }

    return ESP_OK;
}

esp_err_t sdlog_exporter_can(sdlog_exporter_para_t *p_para)
{
    // Move cursor to the begin-of-data
    if (fseek(p_para->fp_in, sizeof(sdlog_header_t), SEEK_SET) != 0) { // skip gloal header
        return ESP_FAIL;
    }

    sdlog_data_t entry;
    while (fread(&entry, sizeof(sdlog_data_t), 1, p_para->fp_in) == 1) {
        if (entry.magic != 0xA5) {
            ESP_LOGE(TAG, "CAN Exporter: Magic mismatch!");
            return ESP_FAIL;
        }

        twai_message_t can_msg;
        if (fread(&can_msg, sizeof(twai_message_t), 1, p_para->fp_in) != 1) {
            break;
        }

        // Convert the CAN bytes to the string
        char data_hex[32]; // "00 00 00 00 00 00 00 00"
        char *p = data_hex;
        for (int i = 0; i < can_msg.data_length_code; i++) {
            p += sprintf(p, "%02X ", can_msg.data[i]);
        }
        if (can_msg.data_length_code) {
            *(p - 1) = '\0'; // if we generated any byte aboves, then there would one extra space
        }

        // String format to candump.txt
        // Format: (1587129135.759626) can1 325 [8] 00 00 00 00 00 00 00 00
        uint64_t abs_us = p_para->us_epoch_time + (entry.us_sys_time - p_para->us_sys_time); // calculate absolute micro-second
        fprintf(p_para->fp_out, "(%llu.%06llu) can1 %03lX [%d] %s\n",
            (abs_us / 1000000), (abs_us % 1000000), can_msg.identifier, can_msg.data_length_code, data_hex);

        // If entry.payload is larger than twai_message_t, skip remaining bytes
        if (entry.payload_len > sizeof(twai_message_t)) {
            fseek(p_para->fp_in, entry.payload_len - sizeof(twai_message_t), SEEK_CUR);
        }

        // Handle padding, 8byte align
        _sdlog_exporter_fp_in_padding(p_para->fp_in, entry.payload_len);
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
    uint64_t conv_time;

    while (1) {
        if (xQueueReceive(sdlog_conv_task_msgq, &msg, portMAX_DELAY) == pdPASS) {
            uint32_t step = 1;
            FILE *fp_in = NULL, *fp_out = NULL;
            void *iobuf = NULL;
            do {
                sdlog_header_sys_t sdlog_header;

                // Open the binary file
                step++;
                if ((fp_in = fopen(msg.log_path, "rb")) == NULL) {
                    break;
                }

                // Read whole header image locally,
                step++;
                if (fread(&sdlog_header, 1, sizeof(sdlog_header), fp_in) != sizeof(sdlog_header)) {
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
                iobuf = malloc(SDLOG_CONV_FILE_BUF_SZ);
                if (iobuf) {
                    setvbuf(fp_out, iobuf, _IOFBF, SDLOG_CONV_FILE_BUF_SZ); // set the wbuf of the FILE*, it writes to the SD card every 4KB
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
            if (iobuf) {
                free(iobuf);
                iobuf = NULL;
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
    strncpy(msg.log_path, path, sizeof(msg.log_path));
    xQueueSend(sdlog_conv_task_msgq, &msg, 0); // block time = 0
}
