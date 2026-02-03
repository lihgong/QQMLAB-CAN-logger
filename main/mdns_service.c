#include "esp_err.h"
#include "esp_log.h"
#include "mdns.h"
#include "syscfg.h"

static const char *TAG = "MDNS";

void start_mdns_service(void)
{
    const char *hostname = syscfg_system_p()->hostname;

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        if (err == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "mDNS already running.");
        } else {
            ESP_LOGE(TAG, "mDNS Init failed: 0x%x", err);
            return;
        }
    }

    // print something if these messages failed
    if (mdns_hostname_set(hostname) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set mDNS hostname");
    }

    mdns_instance_name_set(hostname);
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);

    ESP_LOGI(TAG, "mDNS setup done for http://%s.local", hostname);
}

// TODO: AI suggested to add meta status over mDNS broadcast info
// For example, bus=load, packets=1200
