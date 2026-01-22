#include "esp_vfs_fat.h"
#include "esp_log.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "board.h"

static sdmmc_card_t *sdcard = NULL; // keep global reference to the card
static const char *TAG      = "SDCARD";

void sd_card_test(void)
{
    ESP_LOGI(TAG, "SD Card mounted successfully!\n");

    FILE *f = fopen(MNT_SDCARD "/hello.txt", "w");
    if (f != NULL) {
        fprintf(f, "Hello World from QQMLAB CAN LOGGER!\n");
        fclose(f);
        ESP_LOGI(TAG, "File written and closed.\n");
    } else {
        ESP_LOGI(TAG, "Failed to open file for writing.\n");
    }
}

esp_err_t sd_card_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // no auto format
        .max_files              = 5,
        .allocation_unit_size   = 16 * 1024};

#if defined(SDCARD_IN_SDMMC)
    sdmmc_host_t host               = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width               = 1; // SDMMC 1-bit mode, because pins were occupied by other functions

    esp_err_t ret = esp_vfs_fat_sdmmc_mount(MNT_SDCARD, &host, &slot_config, &mount_config, &sdcard);

#if defined(TARGET_BOARD_ESP32_CAM)
    // In SD card driver init function, it initializes the GPIO4 as SDMMC and pull-up no matter used or not
    // We add code here to reset it back to normal GPIO output mode (and set low)
    gpio_reset_pin(GPIO_NUM_4);
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_4, 0);
#endif

    return ret;

#elif defined(SDCARD_IN_SDSPI)
    sdmmc_host_t host        = SDSPI_HOST_DEFAULT();
    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = SDCARD_SPI_MOSI_PIN,
        .miso_io_num     = SDCARD_SPI_MISO_PIN,
        .sclk_io_num     = SDCARD_SPI_CLK_PIN,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };
    esp_err_t ret = spi_bus_initialize(host.slot, &bus_cfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK)
        return ret;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = SDCARD_SPI_CS_PIN;
    slot_config.host_id               = host.slot;
    return esp_vfs_fat_sdspi_mount(MNT_SDCARD, &host, &slot_config, &mount_config, &sdcard);
#endif

    // once the SD card init, test whether we can write it
    sd_card_test();
}
