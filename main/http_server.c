#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "./board.h"

// TOP-level HTTP server handle
httpd_handle_t http_server_h;
static const char *TAG = "HTTP_SERVER";

// ExternaL functions
extern esp_netif_t *wifi_manager_get_sta_netif(void);
extern void led_op(uint32_t op_0on_1off_2toggle);
extern uint32_t led_is_on(void);

// ----------
// UTILITY FUNCTIONS
// ----------
static void _led_msg_handle(char *buf)
{
    char param[16];
    if (httpd_query_key_value(buf, "led_op", param, sizeof(param)) == ESP_OK) {
        ESP_LOGI(TAG, "_led_msg_handle(), param=%s", param);
        if (strcmp(param, "0") == 0) {
            led_op(0);
        } else if (strcmp(param, "1") == 0) {
            led_op(1);
        } else if (strcmp(param, "2") == 0) {
            led_op(2);
        }
    }
}

static esp_err_t _http_redirect_to_index(httpd_req_t *req)
{
    httpd_resp_set_status(req, "303 See Other"); // send HTTP 303 "see other", and redirect to index
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0); // send header only, no send content
    return ESP_OK;
}

// ----------
// URI: /
// ----------
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
    esp_netif_get_ip_info(wifi_manager_get_sta_netif(), &ip_info);

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
        led_is_on() ? "ON" : "OFF");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// ----------
// URI: /led_on, /led_off, /led_toggle
// ----------
static esp_err_t _uri_led_op_off_toggle(httpd_req_t *req, uint32_t op)
{
    led_op(op);
    return _http_redirect_to_index(req);
}

esp_err_t uri_led_on(httpd_req_t *req)
{
    return _uri_led_op_off_toggle(req, 0);
}

esp_err_t uri_led_off(httpd_req_t *req)
{
    return _uri_led_op_off_toggle(req, 1);
}

esp_err_t uri_led_toggle(httpd_req_t *req)
{
    return _uri_led_op_off_toggle(req, 2);
}

// ----------
// URI: /led_post
// ----------
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

// ----------
// HTTP server start body
// ----------
void http_server_start(void)
{
    static uint8_t init = 0;
    if (init) {
        ESP_LOGW(TAG, "Webserver already started!");

    } else {
        init = 1;

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        if (httpd_start(&http_server_h, &config) == ESP_OK) {
            httpd_uri_t uri_tbl[] = {
                {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL},
                {.uri = "/led_on", .method = HTTP_GET, .handler = uri_led_on, .user_ctx = NULL},
                {.uri = "/led_off", .method = HTTP_GET, .handler = uri_led_off, .user_ctx = NULL},
                {.uri = "/led_toggle", .method = HTTP_GET, .handler = uri_led_toggle, .user_ctx = NULL},
                {.uri = "/led_post", .method = HTTP_POST, .handler = uri_led_post, .user_ctx = NULL},
            };

            for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
                httpd_register_uri_handler(http_server_h, &uri_tbl[i]);
            }
        }
    }
}
