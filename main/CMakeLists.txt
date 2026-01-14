idf_component_register(SRCS "sd_log_service.c" "http_server.c" "led.c" "wifi_manager.c" "sdcard.c" "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES esp_http_server esp_wifi esp_netif nvs_flash driver fatfs sdmmc esp_timer)