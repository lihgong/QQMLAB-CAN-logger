#include <stdio.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_http_server.h"

#include "./wifi_passwd.h"
#include "./board.h"

static const char *TAG = "QQMLAB_LOG";

#define GPIO_LED_BREATH_PERIOD (2000)
#define HOSTNAME "QQMLAB-LOGGER"

#define LED_ON (0)
#define LED_OFF (1)

// HTTP Get Handler
esp_err_t uri_index(httpd_req_t *req)
{
    uint32_t is_on = gpio_get_level(GPIO_LED_BREATH) == LED_ON;

    char resp[256];
    snprintf(resp, sizeof(resp), 
             "<h1>QQMLAB CAN LOGGER</h1>"
             "<p>Board: %s</p>"
             "<p>Current LED Status: <b>%s</b></p>"
             "<a href='/led_on'>[ Turn ON ]</a><br>"
             "<a href='/led_off'>[ Turn OFF ]</a>"
             "<p>Free RAM: %lu bytes</p>"
             "<p>gpio_read=0x%08" PRIx32 "</p>",
             BOARD_NAME, (gpio_read == LED_ON) ? "ON" : "OFF", esp_get_free_heap_size(), gpio_read);

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t _http_return(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other"); // send HTTP 303 "see other", and redirect to index
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); // send header only, no send content
    return ESP_OK;
}

esp_err_t uri_led_on(httpd_req_t *req)
{
    gpio_set_level(GPIO_LED_BREATH, LED_ON);
    return _http_return(req);
}

esp_err_t uri_led_off(httpd_req_t *req)
{
    gpio_set_level(GPIO_LED_BREATH, LED_OFF);
    return _http_return(req);
}

/* Start Web Server */
static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri      = "/",
            .method   = HTTP_GET,
            .handler  = uri_index,
            .user_ctx = NULL});

        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri      = "/led_on",
            .method   = HTTP_GET,
            .handler  = uri_led_on,
            .user_ctx = NULL});

        httpd_register_uri_handler(server, &(httpd_uri_t){
            .uri      = "/led_off",
            .method   = HTTP_GET,
            .handler  = uri_led_off,
            .user_ctx = NULL});
        
        return server;
    }
    return NULL;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        ESP_LOGI(TAG, "Retrying connection to AP...");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        start_webserver();
    }
}

void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();

    esp_netif_set_hostname(sta_netif, HOSTNAME);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void app_main(void)
{
    // Reserve time for USB to identify the device
    // If the program crasheds before this delay, the device may not be recognized
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Init NVS (Non-Volatile Storage) flash storage to store WIFI credentials, and other stuff (it just needed it)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Init LED
    gpio_reset_pin(GPIO_LED_BREATH);
    gpio_set_direction(GPIO_LED_BREATH, GPIO_MODE_INPUT_OUTPUT); // use INPUT_OUTPUT to allow read back the status

    // Start WIFI
    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA Starting...");
    wifi_init_sta();
}
