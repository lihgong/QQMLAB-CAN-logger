#include "esp_log.h"
#include "nimble/ble.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/util/util.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "BLE_BEACON";

char ble_beacon_devname[32];
static void ble_beacon_devname_set(char *ip_str)
{
    if (ip_str == NULL) {
        snprintf(ble_beacon_devname, sizeof(ble_beacon_devname), "CAN-OFFLINE");
    } else {
        snprintf(ble_beacon_devname, sizeof(ble_beacon_devname), "CAN-%s", ip_str);
    }
}

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
}

void ble_beacon_update_ip_name(char *ip_str) // called by WIFI manager to change the BLE device name
{
    ble_beacon_devname_set(ip_str);
    ble_beacon_devname_apply();
    ESP_LOGI(TAG, "BLE name updated to: %s", ble_beacon_devname);
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
            .itvl_min  = 2400,                  // 2400*0.625 = 1500 ms, minimize the Wi-Fi interference
            .itvl_max  = 2400,                  // 2400*0.625 = 1500 ms
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
    ble_beacon_devname_set(NULL);
    nimble_port_init();
    ble_svc_gap_init();                          // init GAP service
    ble_hs_cfg.sync_cb = on_sync;                // set the sync callback
    nimble_port_freertos_init(nimble_host_task); // start the FreeRTOS task to start Numble stack

    return ESP_OK;
}
