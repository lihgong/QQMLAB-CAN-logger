#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "./board.h"

#define MAIN_INIT_HOOK_FILE "main_hook.h"

#define APP_MAIN_INIT_FUNC(fp) extern esp_err_t(fp)(void);
#include MAIN_INIT_HOOK_FILE
#undef APP_MAIN_INIT_FUNC

void app_main(void)
{
    // Reserve time for USB to identify the device
    // If the program crasheds before this delay, the device may not be recognized
    vTaskDelay(pdMS_TO_TICKS(2000));

    typedef esp_err_t (*fp_init_t)(void);
    fp_init_t fp_init[] = {
#define APP_MAIN_INIT_FUNC(fp) fp,
#include MAIN_INIT_HOOK_FILE
#undef APP_MAIN_INIT_FUNC
    };

    for (uint32_t i = 0; i < sizeof(fp_init) / sizeof(fp_init_t); i++) {
        ESP_ERROR_CHECK((fp_init[i])());
    }
}
