#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "./board.h"

#include "sdlog_service.h"

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

static void _sdlog_msg_handle(char *buf)
{
    char val_str[32];

    if (httpd_query_key_value(buf, "sdlog_start", val_str, sizeof(val_str)) == ESP_OK) {
        uint32_t ch = atoi(val_str);
        if (ch < SDLOG_SOURCE_NUM) {
            if (httpd_query_key_value(buf, "epoch_time", val_str, sizeof(val_str)) == ESP_OK) {
                uint64_t epoch = strtoull(val_str, NULL, 10);
                sdlog_start(ch, epoch);
                ESP_LOGI(TAG, "Start CH %d, epoch: %llu", ch, epoch);
            }
        }
    }

    if (httpd_query_key_value(buf, "sdlog_stop", val_str, sizeof(val_str)) == ESP_OK) {
        int ch = atoi(val_str);
        if (ch >= 0 && ch < SDLOG_SOURCE_NUM) {
            sdlog_stop(ch);
            ESP_LOGI(TAG, "Stop CH %d", ch);
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
        char buf[128]; // No very long query string here, fixed size here to avoid buffer overflow
        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            ESP_LOGI(TAG, "uri_index(), GET: %s", buf);
            _led_msg_handle(buf);
            _sdlog_msg_handle(buf);
        }
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(wifi_manager_get_sta_netif(), &ip_info);

    char resp[1536];
    snprintf(resp, sizeof(resp),
        "<html>"
        "<head><title>QQMLAB CAN Logger</title></head>"
        "<h1>QQMLAB CAN LOGGER</h1>"
        "<h3>Status</h3>"
        "<p>Board: %s | IP: " IPSTR " | Free RAM: %lu bytes</p>"
        "<p>Current LED Status: <b>%s</b></p>"
        "<hr>"

        "<h3>SD Logging Control</h3>"
        "<p>"
        "  Channel 0 (HTTP): "
        "  <button onclick='doStart(0)'>START</button> "
        "  <button onclick='doStop(0)'>STOP</button><br>"
        "  Channel 1 (CAN): "
        "  <button onclick='doStart(1)'>START</button> "
        "  <button onclick='doStop(1)'>STOP</button>"
        "</p>"

        "<script>"
        "async function doStart(ch) {"
        "  const ts = BigInt(Date.now()) * 1000n;" // 取得電腦微秒時間
        "  location.href = `/?sdlog_start=${ch}&epoch_time=${ts.toString()}`;"
        "}"

        "async function doStop(ch) {"
        "  location.href = `/?sdlog_stop=${ch}`;"
        "}"
        "</script>"

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
        "<hr>"
        "</html>",
        BOARD_NAME, IP2STR(&ip_info.ip), esp_get_free_heap_size(),
        led_is_on() ? "ON" : "OFF");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    char http_log[] = "http req";
    sdlog_write(SDLOG_SOURCE_HTTP, SDLOG_FMT_TEXT__STRING, sizeof(http_log), http_log);
    return ESP_OK;
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
        config.stack_size     = 8192; // enlarge the stack size to avoid buffer overflow
        if (httpd_start(&http_server_h, &config) == ESP_OK) {
            httpd_uri_t uri_tbl[] = {
                {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL},
                {.uri = "/led_post", .method = HTTP_POST, .handler = uri_led_post, .user_ctx = NULL},
            };

            for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
                httpd_register_uri_handler(http_server_h, &uri_tbl[i]);
            }
        }
    }
}
