#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "board.h"
#include "wifi_passwd.h"
#include "http_server.h"

static const char *TAG = "WIFI_MANAGER";

static TaskHandle_t wifi_manager_task_handle;

#define WIFI_SCAN_MAX_AP (10) // maximum AP# to scan of nearby AP
#define WIFI_ENTRIES_MAX (10)

// ----------
// WIFI SSID entries
// ----------
esp_netif_t *sta_netif;

typedef struct {
    char ssid[32];
    char pass[64];
} wifi_entry_t;

wifi_entry_t known_network[WIFI_ENTRIES_MAX];
uint32_t known_network_cnt = 0;

static void wifi_manager_add_known_network(void)
{
#if defined(WIFI_PASSWD_FROM_CODE)
    if (known_network_cnt < WIFI_ENTRIES_MAX) {
        strlcpy(known_network[known_network_cnt].ssid, WIFI_SSID, sizeof(known_network[known_network_cnt].ssid));
        strlcpy(known_network[known_network_cnt].pass, WIFI_PASS, sizeof(known_network[known_network_cnt].pass));
        known_network_cnt++;
    }
#endif
}

uint32_t wifi_manager_syscfg(const char *section, const char *key, const char *value)
{
    if (strcmp(section, "wifi_known_network") == 0) {
        if (known_network_cnt < WIFI_ENTRIES_MAX) {
            if (strcmp(key, "network") == 0) {
                wifi_entry_t *p_network = &known_network[known_network_cnt];

                // set default password as empty to support open WIFI network (no password)
                memset(p_network->pass, 0, sizeof(p_network->pass));
                int matched = sscanf(value, " %31[^|]|%63s", p_network->ssid, p_network->pass); // %[^,] means read until encountering "|"
                if (matched >= 1) {
                    ESP_LOGI(TAG, "SSID loaded: [%s] [%s]", p_network->ssid, p_network->pass);
                    known_network_cnt++;
                } else {
                    ESP_LOGW(TAG, "Error: Invalid format in line: %s", value);
                }
            }
        }
    }

    return 1;
}

// ----------
// WIFI scan & connect API
// ----------
static void wifi_scan_and_connect(void)
{
    if (known_network_cnt == 0) {
        ESP_LOGW(TAG, "No known Wifi Network");
        return;
    }

    // WIFI scan
    wifi_scan_config_t scan_config = {0};                            // if no special purpose (for scan), just set to 0 is fine
    ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, /*block*/ 1)); // blocking wait

    uint16_t ap_num = WIFI_SCAN_MAX_AP; // tell the API maximum AP to scan, and got the scanned result
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_AP];
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, ap_records));

    for (int i = 0; i < ap_num; i++) {
        for (int j = 0; j < known_network_cnt; j++) {
            if (strcmp((char *)ap_records[i].ssid, known_network[j].ssid) == 0) {
                wifi_entry_t *p_network = &known_network[j];

                ESP_LOGI(TAG, "Find match SSID %s, connect...", p_network->ssid);

                wifi_config_t wifi_config = {0};
                strlcpy((char *)wifi_config.sta.ssid, p_network->ssid, 32);
                strlcpy((char *)wifi_config.sta.password, p_network->pass, 64);

                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
                ESP_ERROR_CHECK(esp_wifi_connect());
                return;
            }
        }
    }
}

// ----------
// WIFI event handler
// ----------
static void server_up_when_ip_obtained(void)
{
    http_server_start();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Start to connect AP...");
        if (wifi_manager_task_handle) {
            xTaskNotifyGive(wifi_manager_task_handle);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected. Scanning for other known networks...");
        if (wifi_manager_task_handle) {
            xTaskNotifyGive(wifi_manager_task_handle);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        server_up_when_ip_obtained();
    }
}

// ----------
// WIFI manager task
// ----------

static void wifi_manager_background_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGW(TAG, "WIFI Manager: got connection request, wait 2sec...");
        vTaskDelay(pdMS_TO_TICKS(2000));
        wifi_scan_and_connect();
        ESP_LOGI(TAG, "Connection Manager: connect completed");
    }
}

// ----------
// WIFI public API
// ----------
esp_err_t wifi_sta_init(void)
{
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA Starting...");

    // Load known network (only once)
    wifi_manager_add_known_network();

    // Start the WIFI manager background task
    xTaskCreate(wifi_manager_background_task, "wifi_mgr_task", 4096, NULL, 5, &wifi_manager_task_handle);

    // Init TCP/IP & WIFI (only once)
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta_netif, HOSTNAME);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    return ESP_OK;
}
