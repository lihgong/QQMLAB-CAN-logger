#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "./board.h"

static const char *TAG = "QQMLAB_LOG";

extern esp_err_t sd_card_init(void);
extern void led_init(void);

static void nvs_init(void)
{
    // Init NVS (Non-Volatile Storage) flash storage to store WIFI credentials, and other stuff (it just needed it)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

void sd_card_test(void)
{
    ESP_LOGI(TAG, "SD Card mounted successfully!\n");

    FILE *f = fopen(MNT_SDCARD "/hello.txt", "w");
    if (f != NULL) {
        fprintf(f, "Hello World from QQMLAB CAN LOGGER!\n");
        fprintf(f, "Timestamp: %lld\n", (long long)esp_timer_get_time());
        fclose(f);
        ESP_LOGI(TAG, "File written and closed.\n");
    } else {
        ESP_LOGI(TAG, "Failed to open file for writing.\n");
    }
}

extern esp_err_t sd_card_init(void);
extern void wifi_init_sta(void);

void app_main(void)
{
    // Reserve time for USB to identify the device
    // If the program crasheds before this delay, the device may not be recognized
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Init LED
    nvs_init();
    ESP_ERROR_CHECK(sd_card_init());
    led_init();

    // wifi init
    wifi_init_sta();

    sd_card_test();
}
