#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_ota_ops.h"

#include "sd_ota.h"
#include "led.h"

static const char *TAG = "SD_OTA";

#define SD_OTA_FILE_PATH "/sdcard/update.bin"
#define SD_OTA_DONE_PATH "/sdcard/update.bin.done"
#define OTA_BUFF_SIZE (4096)
#define OTA_REBOOT_TIMEOUT_MS (1000)

esp_err_t sd_ota_init(void)
{
    FILE *f = NULL;

    ESP_LOGI(TAG, "SD_OTA init...");
    do {
        esp_err_t err;

        // Check whether the update.bin exists
        struct stat st;
        if (stat(SD_OTA_FILE_PATH, &st) != 0) {
            ESP_LOGI(TAG, "No update package (%s) detected. Continue normal boot.", SD_OTA_FILE_PATH);
            break;
        }
        ESP_LOGI(TAG, "Detected %s (Size: %ld bytes). Starting OTA update process...", SD_OTA_FILE_PATH, st.st_size);

        f = fopen(SD_OTA_FILE_PATH, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Cannot open %s for reading!", SD_OTA_FILE_PATH);
            break;
        }

        // Get the next target ota partition
        const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
        if (!update_partition) {
            ESP_LOGE(TAG, "Passive OTA partition not found!");
            break;
        }
        ESP_LOGI(TAG, "Target partition: %s (Address: 0x%08lx, Size: 0x%lx)", update_partition->label, update_partition->address, update_partition->size);

        esp_ota_handle_t ota_handle = 0;

        err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle); // OTA_WITH_SEQUENTIAL_WRITES: erase Flash with write, rather than full erase
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
            break;
        }

        char *buf = malloc(OTA_BUFF_SIZE);
        if (!buf) {
            ESP_LOGE(TAG, "Failed to allocate memory for OTA buffer!");
            esp_ota_abort(ota_handle);
            break;
        }

        size_t bytes_read    = 0;
        size_t total_written = 0;
        while ((bytes_read = fread(buf, 1, OTA_BUFF_SIZE, f)) > 0) {
            err = esp_ota_write(ota_handle, (const void *)buf, bytes_read);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
                break;
            }
            total_written += bytes_read;
            int cur_pct = (int)((total_written * 100) / st.st_size);
            ESP_LOGI(TAG, "OTA Progress: %d%% (%u / %ld bytes)", cur_pct, (unsigned)total_written, st.st_size);
            led_op(LED_OP_TOGGLE2); // toggle LED
        }

        free(buf);

        if (err != ESP_OK) {
            esp_ota_abort(ota_handle);
            ESP_LOGE(TAG, "OTA aborted due to write errors.");
            break;
        }

        // validate SHA-256 & Image structure
        err = esp_ota_end(ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_end failed! Checksum or image validation error: %s", esp_err_to_name(err));
            break;
        }

        // Update otadata
        err = esp_ota_set_boot_partition(update_partition);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update boot partition: %s", esp_err_to_name(err));
            break;
        }

        // Rename FW to indicate updating completed
        unlink(SD_OTA_DONE_PATH);
        if (rename(SD_OTA_FILE_PATH, SD_OTA_DONE_PATH) == 0) {
            ESP_LOGI(TAG, "Renamed update package to %s", SD_OTA_DONE_PATH);
        } else {
            ESP_LOGW(TAG, "Failed to rename update file, removing it instead.");
            unlink(SD_OTA_FILE_PATH);
        }

        ESP_LOGI(TAG, "Firmware updated successfully! System will restart in %d ms...", OTA_REBOOT_TIMEOUT_MS);
        vTaskDelay(pdMS_TO_TICKS(OTA_REBOOT_TIMEOUT_MS));
        esp_restart();
    } while (0);

    // Clean up procedure
    if (f) { // close opened file
        fclose(f);
    }

    return ESP_OK;
}
