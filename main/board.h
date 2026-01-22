#ifndef __BOARD_H__
#define __BOARD_H__

// Some top-level settings can be kept here, maybe it's not truly "board" level
// I can refactor this part later

#define HOSTNAME "QQMLAB-LOGGER" // HOST-NAME

#define MNT_SDCARD "/sdcard" // SD card mount position

// ----------
// BOARD SELECT
// ----------
#define TARGET_BOARD_ESP32C3
// #define TARGET_BOARD_ESP32_CAM

#if defined(TARGET_BOARD_ESP32C3) && !defined(TARGET_BOARD_ESP32_CAM)
#elif !defined(TARGET_BOARD_ESP32C3) && defined(TARGET_BOARD_ESP32_CAM)
#else
#error "Board configuration is one-hot"
#endif

// ----------
// BOARD detailed configuration
// ----------

#if defined(TARGET_BOARD_ESP32C3)
#define BOARD_NAME "ESP32-C3-AIRM2M"

#define LED_PIN (13)
// #define LED_POLARITY_INV

#define SDCARD_IN_SDSPI
#define SDCARD_SPI_MOSI_PIN (7)
#define SDCARD_SPI_MISO_PIN (2)
#define SDCARD_SPI_CLK_PIN (6)
#define SDCARD_SPI_CS_PIN (10)

#elif defined(TARGET_BOARD_ESP32_CAM)
#define BOARD_NAME "ESP32-CAM"
#define LED_PIN (33)
#define LED_POLARITY_INV

#define SDCARD_IN_SDMMC
// In ESP32, the SDMMC interface is hardwired to dedicated pins, so no further configuration here

#else
#error "not support board"

#endif

#endif // __BOARD_H__
