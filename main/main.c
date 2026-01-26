#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_timer.h"

#include "./board.h"

extern esp_err_t sd_card_init(void);
extern esp_err_t led_init(void);
extern esp_err_t wifi_sta_init(void);
extern esp_err_t sdlog_service_init(void);
extern esp_err_t twai_service_init(void);
extern esp_err_t nvs_init(void);

typedef esp_err_t (*fp_init_t)(void);

void app_main(void)
{
    // Reserve time for USB to identify the device
    // If the program crasheds before this delay, the device may not be recognized
    vTaskDelay(pdMS_TO_TICKS(2000));

    fp_init_t fp_init[] = {
        nvs_init,
        led_init,
        sd_card_init,
        sdlog_service_init,
        twai_service_init,
        wifi_sta_init,
    };

    for (uint32_t i = 0; i < sizeof(fp_init) / sizeof(fp_init_t); i++) {
        ESP_ERROR_CHECK((fp_init[i])());
    }
}
