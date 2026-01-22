#include "driver/twai.h"
#include "esp_log.h"
#include "sdlog_service.h"
#include "board.h"

static const char *TAG = "TWAI";

static void twai_rx_task(void *arg)
{
    twai_message_t msg;
    ESP_LOGI(TAG, "TWAI RX Task started");

    while (1) {
        if (twai_receive(&msg, portMAX_DELAY) == ESP_OK) { // Wait for CAN packet arriving
            sdlog_write(SDLOG_SOURCE_CAN, /*data_type*/ 0, sizeof(msg), &msg);
            // Observe CAN packet in Console
            // ESP_LOGI(TAG, "ID: 0x%03lX DLC:%d Data: %02x %02x...", msg.identifier, msg.data_length_code, msg.data[0], msg.data[1]);
        }
    }
}

esp_err_t twai_service_init(void)
{
    if (TWAI_EN) {
        // 1. Init CAN transceiver to standby mode (high), do NOT send out any dominate signal
        gpio_reset_pin(TWAI_PIN_STANDBY);
        gpio_set_direction(TWAI_PIN_STANDBY, GPIO_MODE_OUTPUT);
        gpio_set_level(TWAI_PIN_STANDBY, 1);

        // 2. Init TWAI driver
        twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TWAI_PIN_TX, TWAI_PIN_RX, TWAI_MODE_NORMAL);
        g_config.rx_queue_len          = TWAI_RXBUF;
        twai_timing_config_t t_config;
        if (TWAI_SPEED == 0) {
            t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_125KBITS();
        } else {
            t_config = (twai_timing_config_t)TWAI_TIMING_CONFIG_500KBITS();
        }
        twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

        // 3. Start the TWAI driver
        ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
        ESP_ERROR_CHECK(twai_start());
        ESP_LOGI(TAG, "TWAI bus init, tx_pin=%d, rx_pin=%d, standby=%d, speed=%s", TWAI_PIN_TX, TWAI_PIN_RX, TWAI_PIN_STANDBY, (TWAI_SPEED == 0) ? "125K" : "500K");

        // 4. Start the TWAI task, whose priority is #6, which is higher than HTTP (5)
        xTaskCreate(twai_rx_task, "twai_rx", 4096, NULL, 6, NULL);

        // 5. Set the CAN transceiver to normal mode (low)
        gpio_set_level(TWAI_PIN_STANDBY, 0);
    }
    return ESP_OK;
}