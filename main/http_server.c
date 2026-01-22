#include <dirent.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "./board.h"

#include "sdlog_service.h"

#define SDLOG_HTTP_BUF_SZ (128)

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
static void http_server_sdlog(char *fmt, ...)
{
    char buf[SDLOG_HTTP_BUF_SZ];
    va_list args;
    va_start(args, fmt);
    int32_t ret = vsprintf(buf, fmt, args);
    if (ret > 0 && ret < sizeof(buf)) {
        sdlog_write(SDLOG_SOURCE_HTTP, SDLOG_FMT_TEXT__STRING, ret, buf);
    }
}

static void _led_msg_handle(char *buf)
{
    char param[16];
    if (httpd_query_key_value(buf, "led_op", param, sizeof(param)) == ESP_OK) {
        ESP_LOGI(TAG, "_led_msg_handle(), param=%s", param);
        http_server_sdlog("_led_msg_handle, param=%s", buf);
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

static esp_err_t _http_redirect_to_index(httpd_req_t *req, char *uri_redirect)
{
    httpd_resp_set_status(req, "303 See Other"); // send HTTP 303 "see other", and redirect to index
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", uri_redirect);
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

    char resp[384];
    snprintf(resp, sizeof(resp),
        "<html>"
        "<head><title>QQMLAB CAN Logger</title></head>"
        "<h1>QQMLAB CAN LOGGER</h1>"
        "<h3>Status</h3>"
        "<p>Board: %s | IP: " IPSTR " | Free RAM: %lu bytes</p>"
        "<p>Current LED Status: <b>%s</b></p>"
        "<hr>",
        BOARD_NAME, IP2STR(&ip_info.ip), esp_get_free_heap_size(), led_is_on() ? "ON" : "OFF");
    httpd_resp_send_chunk(req, resp, HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<h3>SD Logging Control</h3>"
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
                               "  const ts = BigInt(Date.now()) * 1000n;" // retrieve us in computer
                               "  location.href = `/?sdlog_start=${ch}&epoch_time=${ts.toString()}`;"
                               "}"

                               "async function doStop(ch) {"
                               "  location.href = `/?sdlog_stop=${ch}`;"
                               "}"
                               "</script>"

                               "<hr>"
                               "<h3>Log Download</h3>"
                               "<a href='/browser_log?admin=0'>[ Browse Log ]</a><br>"
                               "<a href='/browser_log?admin=1'>[ Browse Log (admin) ]</a><br>"

                               "<hr>"
                               "<h3>LED Control Panel</h3>"
                               "<a href='/?led_op=0'>[ Turn ON ]</a><br>"
                               "<a href='/?led_op=1'>[ Turn OFF ]</a><br>"
                               "<a href='/?led_op=2'>[ Toggle ]</a><br>"

                               "</html>",
        HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, NULL, 0); // end-of-transmission

    http_server_sdlog("index page");

    return ESP_OK;
}

// ----------
// URI: /led_post
// ----------
esp_err_t uri_led_post(httpd_req_t *req)
{
    ESP_LOGI(TAG, "uri_led_post(), req->content_len=%d", req->content_len);
    http_server_sdlog("uri_led_post, req_len=%d", req->content_len);
    if (req->content_len) { // If the requested size > 0
        char buf[256];
        if (httpd_req_recv(req, buf, sizeof(buf))) {
            buf[req->content_len] = '\0';
            ESP_LOGI(TAG, "uri_led_post(), %s", buf);
            _led_msg_handle(buf);
        }
    }
    return _http_redirect_to_index(req, "/");
}

// ----------
// URI: /browse
// ----------
static esp_err_t uri_browse_log_recursive(httpd_req_t *req, const char *dir_path, uint32_t admin_mode)
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
                uri_browse_log_recursive(req, entry_path, admin_mode); // if folder, run the scan recursively
            } else {
                struct stat entry_stat;
                stat(entry_path, &entry_stat);

                httpd_resp_send_chunk(req, "<tr>", HTTPD_RESP_USE_STRLEN);

                char row_buf[192];
                ret = snprintf(row_buf, sizeof(row_buf), "<td>%s</td><td>%" PRId32 " KB</td>", entry_path, (entry_stat.st_size + 1023) / 1024);
                if (ret >= 0 && ret < sizeof(row_buf)) {
                    httpd_resp_send_chunk(req, row_buf, HTTPD_RESP_USE_STRLEN);
                } else {
                    ESP_LOGW(TAG, "row_buf[] not sufficient");
                }

                uint32_t view0_download1;
                if (strstr(entry->d_name, ".txt") || strstr(entry->d_name, ".log")) {
                    view0_download1 = 0;
                } else {
                    view0_download1 = 1;
                }

                ret = snprintf(row_buf, sizeof(row_buf), "<td><a href='/download_log?path=%s' target='_blank'>%s</a></td>",
                    entry_path, (view0_download1 == 0) ? "View" : "Download");
                if (ret >= 0 && ret < sizeof(row_buf)) {
                    httpd_resp_send_chunk(req, row_buf, HTTPD_RESP_USE_STRLEN);
                } else {
                    ESP_LOGW(TAG, "row_buf[] not sufficient");
                }

                if (admin_mode) {
                    ret = snprintf(row_buf, sizeof(row_buf), "<td><a href='/remove_log?path=%s'>Remove</a></td>", entry_path);
                    if (ret >= 0 && ret < sizeof(row_buf)) {
                        httpd_resp_send_chunk(req, row_buf, HTTPD_RESP_USE_STRLEN);
                    } else {
                        ESP_LOGW(TAG, "row_buf[] not sufficient");
                    }
                } else {
                    httpd_resp_send_chunk(req, "<td></td>", HTTPD_RESP_USE_STRLEN);
                }
                httpd_resp_send_chunk(req, "</tr>", HTTPD_RESP_USE_STRLEN);
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
    char buf[32];

    // From URL query, extract path parameter
    // Eg: /browse_log?admin=1
    uint32_t admin_mode = 0;
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char buf_admin[8];
        if (httpd_query_key_value(buf, "admin", buf_admin, sizeof(buf_admin)) == ESP_OK) {
            if (strcmp(buf_admin, "1") == 0) {
                admin_mode = 1;
            }
        }
    }

    http_server_sdlog("/browse_log");
    // Send HTTP header
    httpd_resp_send_chunk(req, "<html><body style='font-family:sans-serif;'>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<h2>QQMLAB Logger - Browse Files</h2>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<table border='1' cellpadding='5' style='border-collapse:collapse;'>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<tr bgcolor='#ddd'>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "<th>File Path</th> <th>Size</th> <th>Action</th> <th>Remove</th>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, "</tr>", HTTPD_RESP_USE_STRLEN);

    // Execute recursive folder scan
    uri_browse_log_recursive(req, MNT_SDCARD "/log", admin_mode);

    // Send footer
    httpd_resp_send_chunk(req, "</table><br><a href='/'>Back to Home</a></body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0); // end-of-transmission
    return ESP_OK;
}

// ----------
// URI: /download_log
// ----------
static esp_err_t _log_op(httpd_req_t *req, uint32_t op_0download_1delete)
{
    char buf[128];
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

    if (strncmp(path, MNT_SDCARD "/log", 11) != 0) { // ensure the path is always started with correct path
        ESP_LOGW(TAG, "Access denied: %s", path);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access Denied");
        return ESP_FAIL;
    }

    http_server_sdlog("/uri_download_log, path=%s, op_0download_1delete=%d", path, op_0download_1delete);

    if (op_0download_1delete == 0) {
        if (strstr(path, ".txt") || strstr(path, ".log")) {
            httpd_resp_set_type(req, "text/plain; charset=utf-8"); // set to pure text to let brower display it directly
            httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        } else {
            const char *filename = strrchr(path, '/');
            filename             = (filename) ? (filename + 1) : path;

            char header_val[64];
            int ret = snprintf(header_val, sizeof(header_val), "attachment; filename=\"%s\"", filename);
            if (ret >= 0 && ret < sizeof(header_val)) {
            } else {
                return ESP_FAIL;
            }
            httpd_resp_set_type(req, "application/octet-stream"); // browser will download it
            httpd_resp_set_hdr(req, "Content-Disposition", header_val);
        }

        FILE *f = fopen(path, "rb");
        if (!f) {
            ESP_LOGE(TAG, "Failed to open file : %s", path);
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "File not found");
            return ESP_FAIL;
        }

        size_t n;
        do {
            uint8_t chunk[512]; // stream the file content
            n = fread(chunk, 1, sizeof(chunk), f);
            if (n > 0) {
                if (httpd_resp_send_chunk(req, (const char *)chunk, n) != ESP_OK) {
                    fclose(f);
                    httpd_resp_send_chunk(req, NULL, 0); // for terminated if fail
                    return ESP_FAIL;
                }
            }
        } while (n > 0);
        fclose(f);

        httpd_resp_send_chunk(req, NULL, 0); // end-of-transmission
        return ESP_OK;

    } else {
        if (remove(path) == 0) {
            ESP_LOGI(TAG, "Deleted: %s", path);
        } else {
            ESP_LOGE(TAG, "Delete failed: %s", path);
        }
        return _http_redirect_to_index(req, "/browser_log?admin=1");
    }
}

esp_err_t uri_download_log(httpd_req_t *req)
{
    return _log_op(req, 0); // download
}

esp_err_t uri_remove_log(httpd_req_t *req)
{
    return _log_op(req, 1); // remove
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
        config.stack_size     = 4096; // enlarge the stack size to avoid buffer overflow
        if (httpd_start(&http_server_h, &config) == ESP_OK) {
            httpd_uri_t uri_tbl[] = {
                {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL},
                {.uri = "/led_post", .method = HTTP_POST, .handler = uri_led_post, .user_ctx = NULL},
                {.uri = "/browser_log", .method = HTTP_GET, .handler = uri_browse_log, .user_ctx = NULL},
                {.uri = "/download_log", .method = HTTP_GET, .handler = uri_download_log, .user_ctx = NULL},
                {.uri = "/remove_log", .method = HTTP_GET, .handler = uri_remove_log, .user_ctx = NULL},
            };

            for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
                httpd_register_uri_handler(http_server_h, &uri_tbl[i]);
            }
        }
    }
}
