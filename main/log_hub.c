#include <stdio.h>
#include <stdarg.h>
#include "esp_err.h"
#include "esp_log.h"

#include "led.h"

static const char *TAG = "LOG_HUB";

// 1. ESP-IDF log implementation located in esp-idf/components/log/src/log_print.c
// 2. esp_log_printf() calls esp_log_vprintf()
// 3. The default handling function is calling vprintf()
//    - vprintf_like_t esp_log_vprint_func = &vprintf;
// 4. Add this log_hub to have the same feature as old way, and we can hook our own implementation later

static int log_hub_vprintf_handler(const char *fmt, va_list tag)
{
    int len = vprintf(fmt, tag);

    // TODO: extension to wwbsocket, or SD card

    led_op_ext(/*led_idx*/ 1, /*led_op=2, toggle*/ 2);

    return len;
}

esp_err_t log_hub_init(void)
{
    esp_log_set_vprintf(log_hub_vprintf_handler);

    ESP_LOGI(TAG, "LOG HUB init done.");

    return ESP_OK;
}
