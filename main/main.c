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

static esp_err_t nvs_init(void)
{
    // Init NVS (Non-Volatile Storage) flash storage to store WIFI credentials, and other stuff (it just needed it)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

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
