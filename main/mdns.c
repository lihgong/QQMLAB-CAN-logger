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
        ESP_LOGE(TAG, "mDNS Init failed: %d", err);
        return;
    }

    // Configure mdns hostname, http://hostname.local/
    mdns_hostname_set(hostname);

    // Configure device instance name (showed in Bonjour explorer)
    mdns_instance_name_set(hostname);

    // Let other devices the functionalty that the ESP32 provided
    mdns_service_add(
        /*instance name*/ NULL, // null if use the instance name above
        /*service type*/ "_http",
        /*protocol*/ "_tcp",
        /*port*/ 80,
        /*TXT record*/ NULL,
        /*TXT record#*/ 0);

    ESP_LOGI(TAG, "mDNS Ready! Access via: http://%s.local", hostname);
}
