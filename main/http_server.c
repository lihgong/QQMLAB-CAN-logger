#include <dirent.h>
#include <sys/stat.h>

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
        "<h3>Log Download</h3>"
        "<a href='/browser_log'>[ Browse Log]</a><br>"
        "</html>",
        BOARD_NAME, IP2STR(&ip_info.ip), esp_get_free_heap_size(),
        led_is_on() ? "ON" : "OFF");

    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    char http_log[] = "http req";
    sdlog_write(SDLOG_SOURCE_HTTP, SDLOG_FMT_TEXT__STRING, sizeof(http_log) - 1 /*avoid \0 writes to the buffer*/, http_log);
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
// URI: /browse
// ----------
static esp_err_t uri_browse_log_recursive(httpd_req_t *req, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        return ESP_OK; // return if the folder can't open
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') { // skip hidden file, "." & ".."
            continue;
        }

        char entry_path[128];
        int ret = snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry->d_name);
        if (ret >= 0 && ret < sizeof(entry_path)) {
            if (entry->d_type == DT_DIR) {
                uri_browse_log_recursive(req, entry_path); // if folder, run the scan recursively
            } else {
                struct stat entry_stat;
                stat(entry_path, &entry_stat);

                char row_buf[256];
                ret = snprintf(row_buf, sizeof(row_buf), "<tr><td>%s</td><td>%" PRId32 " KB</td>", entry_path, (entry_stat.st_size + 1023) / 1024);
                if (ret >= 0 && ret < sizeof(row_buf)) {
                    httpd_resp_send_chunk(req, row_buf, HTTPD_RESP_USE_STRLEN);
                } else {
                    ESP_LOGW(TAG, "row_buf[] not sufficient");
                }

                ret = snprintf(row_buf, sizeof(row_buf), "<td><a href='/download_log?path=%s' target='_blank'>View</a></td></tr>", entry_path);
                if (ret >= 0 && ret < sizeof(row_buf)) {
                    httpd_resp_send_chunk(req, row_buf, HTTPD_RESP_USE_STRLEN);
                } else {
                    ESP_LOGW(TAG, "row_buf[] not sufficient");
                }
            }
        } else {
            ESP_LOGW(TAG, "entry_path[] not sufficient");
        }
    }

    closedir(dir);
    return ESP_OK;
}

esp_err_t uri_browse_log(httpd_req_t *req)
{
    // Send HTTP header
    httpd_resp_send_chunk(req, "<html><body style='font-family:sans-serif;'>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<h2>QQMLAB Logger - Browse Files</h2>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<table border='1' cellpadding='5' style='border-collapse:collapse;'>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<tr bgcolor='#ddd'><th>File Path</th><th>Size</th><th>Action</th></tr>", HTTPD_RESP_USE_STRLEN);

    // Execute recursive folder scan
    uri_browse_log_recursive(req, "/sdcard/log");

    // Send footer
    httpd_resp_send_chunk(req, "</table><br><a href='/'>Back to Home</a></body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0); // end-of-transmission
    return ESP_OK;
}

// ----------
// URI: /download_log
// ----------
esp_err_t uri_download_log(httpd_req_t *req)
{
    char buf[192];
    char path[128];

    // From URL query, extract path parameter
    // Eg: /download?path=/sdcard/log/http/000023/log.txt
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Query String");
        return ESP_FAIL;
    }

    if (httpd_query_key_value(buf, "path", path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path parameter");
        return ESP_FAIL;
    }

    // Extract the filename
    const char *filename = strrchr(path, '/');
    filename             = (filename) ? (filename + 1) : path;

    char header_val[128];
    int ret = snprintf(header_val, sizeof(header_val), "attachment; filename=\"%s\"", filename);
    if (ret > 0 && ret <= sizeof(header_val)) {
        // bypass
    } else {
        return ESP_FAIL;
    }

    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file : %s", path);
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
        return ESP_FAIL;
    }

    if (strstr(path, ".txt") || strstr(path, ".log")) {
        httpd_resp_set_type(req, "text/plain; charset=utf-8"); // set to pure text to let brower display it directly
        httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
    } else {
        httpd_resp_set_type(req, "application/octet-stream"); // browser will download it
        httpd_resp_set_hdr(req, "Content-Disposition", header_val);
    }

    // 4. 串流發送檔案內容 (每次讀取 1KB)
    char *chunk = malloc(1024);
    if (!chunk) {
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t n;
    do {
        n = fread(chunk, 1, 1024, f);
        if (n > 0) {
            if (httpd_resp_send_chunk(req, chunk, n) != ESP_OK) {
                fclose(f);
                free(chunk);
                httpd_resp_send_chunk(req, NULL, 0); // 發生錯誤時強制結束
                return ESP_FAIL;
            }
        }
    } while (n > 0);

    free(chunk);
    fclose(f);

    // 5. 發送空 Chunk 代表結束
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
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
                {.uri = "/browser_log", .method = HTTP_GET, .handler = uri_browse_log, .user_ctx = NULL},
                {.uri = "/download_log", .method = HTTP_GET, .handler = uri_download_log, .user_ctx = NULL},
            };

            for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
                httpd_register_uri_handler(http_server_h, &uri_tbl[i]);
            }
        }
    }
}
