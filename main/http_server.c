#include <dirent.h>
#include <sys/stat.h>
#include <stdarg.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "board.h"
#include "led.h"
#include "sdlog_service.h"
#include "sdlog_conv.h"
#include "twai.h"

static const char *TAG = "HTTP_SERVER";

#define SDLOG_HTTP_BUF_SZ (128)

httpd_handle_t http_server_h; // TOP-level HTTP server handle

// ----------
// SYSCFG HOOK
// ----------
#define CAN_TX_LIST_NUM (8)

typedef struct can_tx_list_entry_s {
    char desc[32];
    char id[9];    // 8char (if EXT-CAN-ID, + NULL)
    char data[17]; // 8char + NULL
} can_tx_list_entry_t;

typedef struct can_tx_list_s {
    uint8_t num;
    can_tx_list_entry_t list[CAN_TX_LIST_NUM];
} can_tx_list_t;

can_tx_list_t can_tx_list;

uint32_t http_syscfg(const char *section, const char *key, const char *value)
{
    if (strcmp(section, "http_server_can_tx") == 0) {
        if (can_tx_list.num < CAN_TX_LIST_NUM) {
            if (strcmp(key, "cmd") == 0) {
                can_tx_list_entry_t *p_entry = &can_tx_list.list[can_tx_list.num];

                int matched = sscanf(value, " %31[^,], %8[^,], %16s", p_entry->desc, p_entry->id, p_entry->data); // %[^,] means read until encountering ","
                if (matched == 3) {
                    ESP_LOGI(TAG, "Success, desc=%s, id=%s, data=%s", p_entry->desc, p_entry->id, p_entry->data);
                    can_tx_list.num++;
                } else {
                    ESP_LOGW(TAG, "Error: Invalid format in line: %s", value);
                }
            }
        }
    }

    return 1; // means OK
}

// ----------
// HTTP SERVER LOG API
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

// ----------
// UTILITY FUNCTIONS
// ----------
static esp_err_t _http_redirect_to_index(httpd_req_t *req, char *uri_redirect)
{
    httpd_resp_set_status(req, "303 See Other"); // send HTTP 303 "see other", and redirect to index
    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Location", uri_redirect);
    httpd_resp_send(req, NULL, 0); // send header only, no send content
    return ESP_OK;
}

static void http_server_send_resp_chunk_f(httpd_req_t *req, char *fmt, ...)
{
    char buf[384];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    buf[sizeof(buf) - 1] = '\0';
    va_end(args);
    httpd_resp_send_chunk(req, buf, HTTPD_RESP_USE_STRLEN);
}

// ----------
// URI: /
// led_op=0(on), led_op=1(off), led_op=2(toggle)
// sdlog_start=ch(0/1/2)&epoch_time=time
// sdlog_stop=ch(0/1/2)
// ----------

static void uri_index_led_msg_handle(char *buf)
{
    char param[16];
    if (httpd_query_key_value(buf, "led_op", param, sizeof(param)) == ESP_OK) {
        if (strcmp(param, "0") == 0) {
            led_op(0);
        } else if (strcmp(param, "1") == 0) {
            led_op(1);
        } else if (strcmp(param, "2") == 0) {
            led_op(2);
        }
    }
}

static void uri_index_sdlog_msg_handle(char *buf)
{
    char val_str[32];

    if (httpd_query_key_value(buf, "sdlog_start", val_str, sizeof(val_str)) == ESP_OK) {
        uint32_t ch = atoi(val_str);
        if (ch < SDLOG_SOURCE_NUM) {
            if (httpd_query_key_value(buf, "epoch_time", val_str, sizeof(val_str)) == ESP_OK) {
                uint64_t epoch = strtoull(val_str, NULL, 10);
                sdlog_start(ch, epoch);
            }
        }
    }

    if (httpd_query_key_value(buf, "sdlog_stop", val_str, sizeof(val_str)) == ESP_OK) {
        int ch = atoi(val_str);
        if (ch >= 0 && ch < SDLOG_SOURCE_NUM) {
            sdlog_stop(ch);
            http_server_sdlog("sdlog_stop, ch=%d", ch);
        }
    }
}

esp_err_t uri_index(httpd_req_t *req)
{
    // Handle GET
    char buf[128]; // No very long query string here, fixed size here to avoid buffer overflow
    if (httpd_req_get_url_query_len(req)) {
        http_server_sdlog("/?%s", buf);

        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            uri_index_led_msg_handle(buf);
            uri_index_sdlog_msg_handle(buf);
        }
    } else {
        http_server_sdlog("/");
    }

    twai_webui_status_t twai_status;
    twai_webui_query(&twai_status);

    uint32_t led_stat     = led_is_on_bmp();
    char led_stat_buf[32] = {0};
    for (uint32_t i = 0; i < LED_PIN_NUM; i++) {
        strcat(led_stat_buf, (led_stat & (1 << i)) ? "ON, " : "OFF, ");
    }

    http_server_send_resp_chunk_f(req,
        "<html>"
        "<head><title>QQMLAB CAN Logger</title></head>"
        "<h1>QQMLAB CAN LOGGER</h1>"
        "<h3>Status</h3>"
        "<p>Board: %s | Free RAM: %lu bytes</p>"
        "<p>LED Status: <b>%s</b></p>"
        "<p>CAN RX:%lu TX:%lu</p>"
        "<hr>",
        BOARD_NAME, esp_get_free_heap_size(), led_stat_buf, twai_status.rx_pkt, twai_status.tx_pkt);

    http_server_send_resp_chunk_f(req, "<h3>SD Logging Control</h3><p>");

    for (int i = 0; i < SDLOG_SOURCE_NUM; i++) {
        const char *ch_names[] = {"HTTP", "CAN"}; // maybe we can place these name setting in sdlog_service later
        sdlog_webui_status_t status;
        sdlog_webui_query(i, &status);

        http_server_send_resp_chunk_f(req,
            "  Channel %d (%s): %s "
            "  <button onclick='doStart(%d)' %s>START</button> "
            "  <button onclick='doStop(%d)' %s>STOP</button> "
            "  <i>(Written: %" PRIu32 " bytes)</i><br>",
            i, ch_names[i], status.is_logging ? "&#128308; <b style='color:red;'>[REC]</b>" : "&#9898; IDLE",
            i, status.is_logging ? "disabled" : "", // Recording, no press START
            i, status.is_logging ? "" : "disabled", // IDLE, no press STOP
            status.bytes_written);
    }

    httpd_resp_send_chunk(req,
        "</p>"
        "<script>"
        "async function doStart(ch) {"
        "  const ts = BigInt(Date.now()) * 1000n;" // retrieve us in computer
        "  fetch(`/?sdlog_start=${ch}&epoch_time=${ts.toString()}`).then(() => {"
        "    setTimeout(() => { location.href = '/'; }, 500);});" // once fetch got response, then wait 0.5ms to refresh
        "}"

        "async function doStop(ch) {"
        "  fetch(`/?sdlog_stop=${ch}`).then(() => {"
        "    setTimeout(() => { location.href = '/'; }, 500);});" // once fetch got response, then wait 0.5ms to refresh
        "}"
        "</script>"

        "<hr>"
        "<h3>Log Download</h3>"
        "<a href='/log_browse?admin=0'>[ Browse Log ]</a><br>"
        "<a href='/log_browse?admin=1'>[ Browse Log (admin) ]</a><br>"

        "<hr>"
        "<h3>LED Control Panel</h3>"
        "<a href='/?led_op=0'>[ Turn ON ]</a><br>"
        "<a href='/?led_op=1'>[ Turn OFF ]</a><br>"
        "<a href='/?led_op=2'>[ Toggle ]</a><br>",

        HTTPD_RESP_USE_STRLEN);

    if (can_tx_list.num) {
        http_server_send_resp_chunk_f(req, "<hr><h3>VOLVO TAILGATE operate command</h3>");
        for (uint32_t i = 0; i < can_tx_list.num; i++) {
            can_tx_list_entry_t *p_entry = &can_tx_list.list[i];
            http_server_send_resp_chunk_f(req, "<a href='/can_tx?id=%s&data=%s'>[ %s ]</a><br>", p_entry->id, p_entry->data, p_entry->desc);
        }
    }

    httpd_resp_send_chunk(req, "</html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0); // end-of-transmission

    return ESP_OK;
}

// ----------
// URI: /browse
// ----------
static esp_err_t uri_browse_log_recursive(httpd_req_t *req, const char *dir_path, uint32_t admin_mode)
{
    DIR *dir = opendir(dir_path);
    if (dir == NULL) {
        httpd_resp_send_chunk(req, "<tr><td colspan='5' style='color:grey; text-align:center;'>No data in this category</td></tr>", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') { // skip hidden file, "." & ".."
            continue;
        }

        char entry_path[128];
        int ret = snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry->d_name);
        if (ret >= 0 && ret < sizeof(entry_path)) {
        } else {
            continue;
        }

        if (entry->d_type == DT_DIR) {
            uri_browse_log_recursive(req, entry_path, admin_mode); // if folder, run the scan recursively
        } else {
            struct stat entry_stat;
            stat(entry_path, &entry_stat);

            // Shorten the display path
            // ORIG: /sdcard/log/can/000015/log.txt -> SHOW: 000015/log.txt
            const char *display_path = entry_path;
            if (strlen(entry_path) > 17) { // "/sdcard/log/xxxx/" ~= 17 chars
                char *p = strstr(entry_path, "/log/");
                if (p) {
                    p = strchr(p + 5, '/'); // skip 5-chars ("/log/"), and find the next "/"
                    if (p) {
                        display_path = p + 1;
                    }
                }
            }

            char *action_text = (strstr(entry->d_name, ".txt") || strstr(entry->d_name, ".log")) ? "View" : "Download";

            http_server_send_resp_chunk_f(req,
                "<tr><td>%s</td><td>%" PRId32 " KB</td>"
                "<td><a href='/log_download?path=%s' target='_blank'>%s</a></td>",
                display_path, (entry_stat.st_size + 1023) / 1024, entry_path, action_text);

            if (admin_mode == 0) {
                http_server_send_resp_chunk_f(req, "<td></td><td></td>");
            } else {
                http_server_send_resp_chunk_f(req,
                    "<td><a href='/log_conv?path=%s'>Conv</a></td>"
                    "<td><a href='/log_remove?path=%s'>Remove</a></td>",
                    entry_path, entry_path);
            }

            http_server_send_resp_chunk_f(req, "</tr>", HTTPD_RESP_USE_STRLEN);
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
        http_server_sdlog("/browse_log?%s", buf);
        if (httpd_query_key_value(buf, "admin", buf_admin, sizeof(buf_admin)) == ESP_OK) {
            admin_mode = (strcmp(buf_admin, "1") == 0);
        }
    } else {
        http_server_sdlog("/browse_log");
    }

    // Send HTTP header
    httpd_resp_send_chunk(req,
        "<html><head><style>"
        // 回歸第一版最喜歡的字體設定，並稍微調大一點點 (14px)
        "body{margin:15px; background-color:#f8f9fa; font-family:sans-serif; font-size:14px; color:#333;}"
        // 寬度自適應 (窄版)，置左
        "table{border-collapse:collapse; width:auto; min-width:480px; max-width:850px; margin-bottom:25px; background-color:white; box-shadow: 0 1px 3px rgba(0,0,0,0.1);}"
        // 維持矮高度 (4px)，並稍微增加一點點 line-height 讓字體呼吸
        "th,td{border:1px solid #ddd; padding:4px 12px; text-align:left; line-height:1.4;}"
        "th{background-color:#eee; color:#555; font-weight:600; font-size:13px;}"
        // 這裡改回 sans-serif，不再使用 Consolas，視覺會變得很柔和
        "td{white-space:nowrap; font-family:inherit;}"
        "tr:nth-child(even){background-color:#fafafa;}"
        "tr:hover{background-color:#f1f1f1;}"
        // 標題也維持精緻窄版
        "h3{background-color:#444; color:white; padding:5px 15px; border-radius:3px; margin:20px 0 8px 0; display:inline-block; font-size:15px; font-weight:500;}"
        ".size-col{text-align:right; font-size:12px; color:#777;}"
        "a{text-decoration:none; color:#007bff; font-weight:500;}"
        "a:hover{text-decoration:underline;}"
        "</style></head><body>", // 移除 body inline style，改用 style tag 定義
        HTTPD_RESP_USE_STRLEN);

    httpd_resp_send_chunk(req, "<h2>QQMLAB Logger - File Explorer</h2>", HTTPD_RESP_USE_STRLEN);

    // Define the category that we want to display. Static first, dynamic later
    const char *categories[]  = {"CAN", "HTTP", "CONSOLE"};
    const char *sub_folders[] = {"/can", "/http", "/console"};
    for (uint32_t i = 0; i < sizeof(categories) / sizeof(char *); i++) {
        // TITLE
        http_server_send_resp_chunk_f(req, "<h3>[ %s ]</h3>", categories[i]);

        // BEGIN OF TABLE
        httpd_resp_send_chunk(req, "<table><tr><th>File Path</th><th>Size</th><th>Action</th><th>Conv</th><th>Remove</th></tr>", HTTPD_RESP_USE_STRLEN);

        // Generate target path, and scan
        char target_path[64];
        snprintf(target_path, sizeof(target_path), MNT_SDCARD "/log%s", sub_folders[i]);
        uri_browse_log_recursive(req, target_path, admin_mode);

        // END OF TABLE
        httpd_resp_send_chunk(req, "</table>", HTTPD_RESP_USE_STRLEN);
    }

    httpd_resp_send_chunk(req, "<br><a href='/'>Back to Home</a></body></html>", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// ----------
// URI: /log_download
// ----------
static esp_err_t _log_op(httpd_req_t *req, uint32_t op_0download_1remove_2conv)
{
    // From URL query, extract path parameter
    // Eg: /download?path=/sdcard/log/http/000023/log.txt
    char buf[128];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing Query String");
        return ESP_FAIL;
    }

    char *uri_dict[] = {"log_download", "log_remove", "log_conv"};
    http_server_sdlog("/%s?%s", uri_dict[op_0download_1remove_2conv], buf);

    char path[128];
    if (httpd_query_key_value(buf, "path", path, sizeof(path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing path parameter");
        return ESP_FAIL;
    }

    char *log_root = MNT_SDCARD "/log";
    if (strncmp(path, log_root, strlen(log_root))) { // ensure correct path
        ESP_LOGW(TAG, "Access denied: %s", path);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Access Denied");
        return ESP_FAIL;
    }

    if (op_0download_1remove_2conv == 0) {
        char header_val[64]; // the header formating is sent when httpd_resp_send_chunk() is firstly called

        if (strstr(path, ".txt") || strstr(path, ".log")) {
            httpd_resp_set_type(req, "text/plain; charset=utf-8"); // set to pure text to let brower display it directly
            httpd_resp_set_hdr(req, "X-Content-Type-Options", "nosniff");
        } else {
            const char *filename = strrchr(path, '/');
            filename             = (filename) ? (filename + 1) : path;

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

    } else if (op_0download_1remove_2conv == 1) {
        if (remove(path) == 0) {
            ESP_LOGI(TAG, "Deleted: %s", path);
        } else {
            ESP_LOGE(TAG, "Delete failed: %s", path);
        }
        return _http_redirect_to_index(req, "/log_browse?admin=1");

    } else if (op_0download_1remove_2conv == 2) {
        sdlog_conv_trig(path);
        return _http_redirect_to_index(req, "/log_browse?admin=1");
    } else {
        return _http_redirect_to_index(req, "/log_browse");
    }
}

esp_err_t uri_log_download(httpd_req_t *req)
{
    return _log_op(req, 0); // download
}

esp_err_t uri_log_remove(httpd_req_t *req)
{
    return _log_op(req, 1); // remove
}

esp_err_t uri_log_conv(httpd_req_t *req)
{
    return _log_op(req, 2); // conversion
}

// ----------
// URI: /can_tx
// id=123&data=AABBCC
// ----------
static uint8_t hex_to_byte(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return 0;
}

// convert "AABBCC" into uint8_t array, 0xAA, 0xBB, 0xCC
static void hex_to_bytes(const char *hex, uint8_t *dest, int byte_len)
{
    for (int i = 0; i < byte_len; i++) {
        dest[i] = (hex_to_byte(hex[i * 2]) << 4) | hex_to_byte(hex[i * 2 + 1]);
    }
}

esp_err_t uri_can_tx(httpd_req_t *req)
{
    char buf[128];
    if (httpd_req_get_url_query_len(req)) {
        if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
            http_server_sdlog("/can_tx/?%s", buf);
            char str_id[16]   = {0};
            char str_data[32] = {0};
            httpd_query_key_value(buf, "id", str_id, sizeof(str_id));
            httpd_query_key_value(buf, "data", str_data, sizeof(str_data));

            if ((strlen(str_id) == 0) || (strlen(str_data) == 0)) {
                httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing ID or Data (e.g. /can_tx?id=123&data=1122)");
                return ESP_FAIL;
            }

            uint32_t can_id   = strtol(str_id, NULL, 16);
            uint32_t data_len = strlen(str_data) / 2;
            data_len          = data_len > 8 ? 8 : data_len;
            uint8_t data[8];
            hex_to_bytes(str_data, data, data_len);
            esp_err_t res = twai_webui_transmit(can_id, data_len, data);

            if (res != ESP_OK) {
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "TWAI Transmit Failed");
            }
        }
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "CAN TX without valid parameter");
    }
    return _http_redirect_to_index(req, "/");
}

// ----------
// HTTP server start body
// ----------
void http_server_start(void)
{
    static uint8_t init = 0;
    if (init == 0) {
        init = 1;

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.stack_size     = 4096; // enlarge the stack size to avoid buffer overflow
        if (httpd_start(&http_server_h, &config) == ESP_OK) {
            httpd_uri_t uri_tbl[] = {
                {.uri = "/", .method = HTTP_GET, .handler = uri_index, .user_ctx = NULL},
                {.uri = "/log_browse", .method = HTTP_GET, .handler = uri_browse_log, .user_ctx = NULL},
                {.uri = "/log_download", .method = HTTP_GET, .handler = uri_log_download, .user_ctx = NULL},
                {.uri = "/log_remove", .method = HTTP_GET, .handler = uri_log_remove, .user_ctx = NULL},
                {.uri = "/log_conv", .method = HTTP_GET, .handler = uri_log_conv, .user_ctx = NULL},
                {.uri = "/can_tx", .method = HTTP_GET, .handler = uri_can_tx, .user_ctx = NULL},
            };

            for (uint32_t i = 0; i < sizeof(uri_tbl) / sizeof(httpd_uri_t); i++) {
                httpd_register_uri_handler(http_server_h, &uri_tbl[i]);
            }
        }
    }
}
