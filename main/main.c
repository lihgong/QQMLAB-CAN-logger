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

esp_netif_t *sta_netif;

static void _led_msg_handle(char *buf)
{
    char param[16];
    if (httpd_query_key_value(buf, "led_op", param, sizeof(param)) == ESP_OK) {
        ESP_LOGI(TAG, "_led_msg_handle(), param=%s", param);
        if (strcmp(param, "0") == 0) {
            gpio_set_level(GPIO_LED_BREATH, LED_ON);
        } else if (strcmp(param, "1") == 0) {
            gpio_set_level(GPIO_LED_BREATH, LED_OFF);
        } else if (strcmp(param, "2") == 0) {
            uint32_t gpio_read = gpio_get_level(GPIO_LED_BREATH);
            gpio_set_level(GPIO_LED_BREATH, !gpio_read);
        }
    }
}

esp_err_t uri_index(httpd_req_t *req)
{
    // Handle GET
    if (httpd_req_get_url_query_len(req)) {
        char buf[16]; // No very long query string here, fixed size here to avoid buffer overflow
        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            ESP_LOGI(TAG, "uri_index(), GET: %s", buf);
            _led_msg_handle(buf);
        }
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(sta_netif, &ip_info);

    char resp[1024];
    snprintf(resp, sizeof(resp),
        "<h1>QQMLAB CAN LOGGER</h1>"
        "<h3>Status</h3>"
        "<p>"
        "Board: %s<br>"
        "IP: " IPSTR "<br>"
        "Netmask: " IPSTR "<br>"
        "Gateway: " IPSTR "<br>"
        "Free RAM: %lu bytes<br>"
        "Current LED Status: <b>%s</b><br>"
        "</p>"
        "<hr>"
        "<h3>URL Control Panel</h3>"
        "<a href='/led_on'>[ Turn ON ]</a><br>"
        "<a href='/led_off'>[ Turn OFF ]</a><br>"
        "<a href='/led_toggle'>[ Toggle ]</a><br>"
        "<hr>"
        "<h3>GET Control Panel</h3>"
        "<a href='/?led_op=0'>[ Turn ON ]</a><br>"
        "<a href='/?led_op=1'>[ Turn OFF ]</a><br>"
        "<a href='/?led_op=2'>[ Toggle ]</a><br>"
        "<hr>"
        "<h3>POST Control Panel</h3>"
        "<form action='/led_post' method='POST'>"
        "<button type='submit' name='led_op' value='0'>Turn ON</button>"
        "</form>"
        "<form action='/led_post' method='POST'>"
        "<button type='submit' name='led_op' value='1'>Turn OFF</button>"
        "</form>"
        "<form action='/led_post' method='POST'>"
        "<button type='submit' name='led_op' value='2'>Toggle</button>"
        "</form>"
        "<hr>",
        BOARD_NAME, IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw), esp_get_free_heap_size(),
        (gpio_get_level(GPIO_LED_BREATH) == LED_ON) ? "ON" : "OFF");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t _http_redirect_to_index(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other"); // send HTTP 303 "see other", and redirect to index
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); // send header only, no send content
    return ESP_OK;
}

static esp_err_t uri_led_op(httpd_req_t *req, uint32_t op)
{
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "led_op=%" PRId32, op);
    _led_msg_handle(cmd);
    return _http_redirect_to_index(req);
}

esp_err_t uri_led_on(httpd_req_t *req)
{
    return uri_led_op(req, 0);
}

esp_err_t uri_led_off(httpd_req_t *req)
{
    return uri_led_op(req, 1);
}

esp_err_t uri_led_toggle(httpd_req_t *req)
{
    return uri_led_op(req, 2);
}

esp_err_t uri_led_post(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri_led_post(), req->content_len=%d", req->content_len);
    if (req->content_len) { // If the requested size > 0
        char buf[512];
        if (httpd_req_recv(req, buf, sizeof(buf))) {
            buf[req->content_len] = '\0';
            ESP_LOGI(TAG, "uri_led_post(), %s", buf);
            _led_msg_handle(buf);
        }
    }
    return _http_redirect_to_index(req);
}

/* Start Web Server */

static httpd_handle_t start_webserver(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t uri_tbl[] = {
            {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL},
            {.uri = "/led_on", .method = HTTP_GET, .handler = uri_led_on, .user_ctx = NULL},
            {.uri = "/led_off", .method = HTTP_GET, .handler = uri_led_off, .user_ctx = NULL},
            {.uri = "/led_toggle", .method = HTTP_GET, .handler = uri_led_toggle, .user_ctx = NULL},
            {.uri = "/led_post", .method = HTTP_POST, .handler = uri_led_post, .user_ctx = NULL},
        };

        for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
            httpd_register_uri_handler(server, &uri_tbl[i]);
        }
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
    sta_netif = esp_netif_create_default_wifi_sta();

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
