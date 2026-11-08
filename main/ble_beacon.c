#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "BLE_BEACON";

char ble_beacon_devname[32]      = "CAN-uninit"; // default name, will be updated by WIFI manager when IP obtained
static uint16_t ble_adv_interval = 2400;         // 0.625 ms units; 7200 = 4500 ms

static void ble_beacon_devname_apply(void)
{
    int32_t rc = ble_gap_adv_set_fields(&(struct ble_hs_adv_fields){
        .flags            = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,
        .name             = (uint8_t *)ble_beacon_devname,
        .name_len         = strlen(ble_beacon_devname),
        .name_is_complete = 1, // 0x09: Complete Local Name
    });

    if (rc) {
        ESP_LOGE(TAG, "Failed to set advertising data, rc=%d", rc);
    }
    ESP_LOGI(TAG, "BLE name updated to: %s", ble_beacon_devname);
}

void ble_beacon_update_ip_name(char *ip_str) // called by WIFI manager to change the BLE device name
{
    snprintf(ble_beacon_devname, sizeof(ble_beacon_devname), "CAN-%s", ip_str);
    ble_beacon_devname_apply();
}

// NimBLE HOST/CONTROLLER callback when they're sync
static void on_sync(void)
{
    int32_t rc = ble_hs_util_ensure_addr(0);
    if (rc) {
        ESP_LOGE(TAG, "Failed to ensure device address, rc=%d", rc);
        return;
    }

    ble_beacon_devname_apply(); // set advertisement devname

    rc = ble_gap_adv_start( // start advertisement
        BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
        &(struct ble_gap_adv_params){
            .conn_mode = BLE_GAP_CONN_MODE_NON,
            .disc_mode = BLE_GAP_DISC_MODE_GEN, // discoverable
            .itvl_min  = ble_adv_interval,
            .itvl_max  = ble_adv_interval,
        },
        NULL, NULL);
    if (rc) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
        return;
    }

    ESP_LOGI(TAG, "BLE advertising started! Broadcasting...");
}

// NimBLE background task
static void nimble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task running");
    nimble_port_run(); // The event-loop running within
    nimble_port_freertos_deinit();
}

esp_err_t ble_beacon_init(void)
{
    ESP_LOGI(TAG, "Initializing NimBLE...");
    nimble_port_init();
    ble_svc_gap_init();                          // init GAP service
    ble_hs_cfg.sync_cb = on_sync;                // set the sync callback
    nimble_port_freertos_init(nimble_host_task); // start the FreeRTOS task to start Numble stack

    return ESP_OK;
}

// [ble_beacon]
// interval = 7200
uint32_t ble_beacon_syscfg(const char *section, const char *key, const char *value)
{
    if (strcmp(section, "ble_beacon") == 0 && strcmp(key, "interval") == 0) {
        if (value == NULL) {
            ESP_LOGW(TAG, "Invalid BLE advertising interval: (null)");
            return 1;
        }

        char *endptr           = NULL;
        unsigned long interval = strtoul(value, &endptr, 10);
        if (endptr == value || *endptr != '\0' || interval < 0x20 || interval > 0x4000) {
            ESP_LOGW(TAG, "Invalid BLE advertising interval: %s (expected 32..16384)", value);
            return 1;
        }

        ble_adv_interval = (uint16_t)interval;
        ESP_LOGI(TAG, "BLE advertising interval configured to %u (0.625 ms units)", (unsigned)ble_adv_interval);
    }

    return 1;
}
