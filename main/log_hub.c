#include <stdio.h>
#include <stdarg.h>
#include "esp_err.h"
#include "esp_log.h"

#include "led.h"
#include "sdlog_service.h"

static const char *TAG = "LOG_HUB";

// 1. ESP-IDF log implementation located in esp-idf/components/log/src/log_print.c
// 2. esp_log_printf() calls esp_log_vprintf()
// 3. The default handling function is calling vprintf()
//    - vprintf_like_t esp_log_vprint_func = &vprintf;
// 4. Add this log_hub to have the same feature as old way, and we can hook our own implementation later

#define SDLOG_CONSOLE_BUF_SZ (128)

static int log_hub_vprintf_handler(const char *fmt, va_list args)
{
    va_list args_copy;
    va_copy(args_copy, args);

    // Call the original vprintf() which directs to standard UART
    int len = vprintf(fmt, args);

    // Check whether SDLOG init completed, if so, then direct info to sdlog_service
    if (sdlog_source_ready(SDLOG_SOURCE_CONSOLE)) {
        char buf[SDLOG_CONSOLE_BUF_SZ];
        int32_t ret = vsnprintf(buf, sizeof(buf), fmt, args_copy);
        if (ret > 0) {
            uint32_t write_len = (ret < sizeof(buf)) ? ret : sizeof(buf) - 1;
            sdlog_write(SDLOG_SOURCE_CONSOLE, SDLOG_FMT_TEXT__STRING, write_len, buf);
        }
    }

    led_op_ext(/*led_idx*/ 1, /*led_op=2, toggle*/ 2);

    return len;
}

esp_err_t log_hub_init(void)
{
    esp_log_set_vprintf(log_hub_vprintf_handler);

    ESP_LOGI(TAG, "LOG HUB init done.");

    return ESP_OK;
}
