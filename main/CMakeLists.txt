idf_component_register(
    SRCS "mdns_service.c" "syscfg.c" "ini.c" "log_hub.c" "sdlog_conv.c" "twai.c" "sdlog_service.c" "http_server.c" "led.c" "wifi_manager.c" "sdcard.c" "main.c" "nvs_flash.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_server esp_wifi esp_netif nvs_flash driver fatfs sdmmc esp_timer mdns)