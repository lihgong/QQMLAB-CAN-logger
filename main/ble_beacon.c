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
#include "services/gatt/ble_svc_gatt.h"

#include "led.h"

static const char *TAG = "BLE_BEACON";

// ----------
// DEV name service
// ----------
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

// ----------
// GATT service
// ----------

// 128bit-UUID: notice the order is reversed in the BLE spec, so the bytes are reversed here
static const ble_uuid128_t uuid_led_svc =
    BLE_UUID128_INIT(0xf0, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

static const ble_uuid128_t uuid_led_chr =
    BLE_UUID128_INIT(0xf1, 0xde, 0xbc, 0x9a, 0x78, 0x56, 0x34, 0x12,
        0x78, 0x56, 0x34, 0x12, 0x78, 0x56, 0x34, 0x12);

// gatt_svr_chr_access_led
static int cb_led_char(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // no matter which operation, we just do the toggle operation for testing
        led_op(LED_OP_TOGGLE2);
        ESP_LOGI(TAG, "LED toggle");

        return 0; // success

    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        uint8_t led_status = led_is_on(0) ? 1 : 0; // only check LED0 status

        int rc = os_mbuf_append(ctxt->om, &led_status, sizeof(led_status));
        if (rc != 0) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        ESP_LOGI(TAG, "Received BLE read: LED status=0x%02X", led_status);
        return 0; // success
    }

    return BLE_ATT_ERR_UNLIKELY;
}

// Define GATT service table
// TODO: refactor the following code into a structure
static uint16_t s_led_val_handle; // NimBLE will fill the value when registering the service
static bool s_led_notify_en   = false;
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;

void ble_beacon_notify_led(uint8_t state)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE || !s_led_notify_en) {
        return; // return if not connected or notify not enabled
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&state, sizeof(state));
    if (om == NULL) {
        ESP_LOGW(TAG, "No mbuf for LED notify");
        return;
    }

    int rc = ble_gatts_notify_custom(s_conn_handle, s_led_val_handle, om);
    if (rc != 0) {
        ESP_LOGW(TAG, "Failed to notify LED state, rc=%d", rc);
    }
}

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = &uuid_led_svc.u,
        .characteristics = (struct ble_gatt_chr_def[]){
            {
                .uuid       = &uuid_led_chr.u,
                .access_cb  = cb_led_char,
                .val_handle = &s_led_val_handle,
                .flags      = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
            },
            {0},
        },
    },
    {0},
};

// ----------
// NimBLE HOST/CONTROLLER callback when they're sync
static void ble_beacon_adv_start(void);

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    if (event->type == BLE_GAP_EVENT_CONNECT) { // this is connect event "result"
        ESP_LOGI(TAG, "BLE %s (status=%d)", event->connect.status == 0 ? "connected" : "connect failed", event->connect.status);
        if (event->connect.status == 0) { // connection established
            s_conn_handle = event->connect.conn_handle;
        } else {
            ble_beacon_adv_start(); // if connection failed, restart advertising
        }

    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGI(TAG, "BLE disconnected (reason=%d), restarting adv...", event->disconnect.reason);
        s_conn_handle   = BLE_HS_CONN_HANDLE_NONE;
        s_led_notify_en = false; // reset notify status
        ble_beacon_adv_start();  // resume advertising after disconnection

    } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) {
        if (event->subscribe.attr_handle == s_led_val_handle) {
            s_led_notify_en = (event->subscribe.cur_notify != 0);
            ESP_LOGI(TAG, "LED notify %s", s_led_notify_en ? "enabled" : "disabled");
        }
    }

    return 0;
}

static void ble_beacon_adv_start(void)
{
    ble_beacon_devname_apply(); // set advertisement devname

    int rc = ble_gap_adv_start(
        BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
        &(struct ble_gap_adv_params){
            .conn_mode = BLE_GAP_CONN_MODE_UND,
            .disc_mode = BLE_GAP_DISC_MODE_GEN, // discoverable
            .itvl_min  = ble_adv_interval,
            .itvl_max  = ble_adv_interval,
        },
        ble_gap_event_cb, NULL);

    if (rc != 0 && rc != BLE_HS_EALREADY) {
        ESP_LOGE(TAG, "Failed to start advertising, rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "BLE advertising started! Broadcasting...");
    }
}

static void on_sync(void)
{
    int32_t rc = ble_hs_util_ensure_addr(0);
    if (rc) {
        ESP_LOGE(TAG, "Failed to ensure device address, rc=%d", rc);
        return;
    }
    ble_beacon_adv_start();
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
    ble_svc_gap_init();           // init GAP service
    ble_hs_cfg.sync_cb = on_sync; // set the sync callback
    ble_svc_gatt_init();

    int rc;
    rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    nimble_port_freertos_init(nimble_host_task); // start the FreeRTOS task to start Numble stack

    return ESP_OK;
}

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
