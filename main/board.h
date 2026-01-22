#ifndef __BOARD_H__
#define __BOARD_H__

// Some top-level settings can be kept here, maybe it's not truly "board" level
// I can refactor this part later
#define HOSTNAME "QQMLAB-LOGGER"

// SD card mount position
#define MNT_SDCARD "/sdcard"

// #define TARGET_BOARD_ESP32C3
#define TARGET_BOARD_ESP32_CAM

#if defined(TARGET_BOARD_ESP32C3)
#define BOARD_NAME "ESP32-C3-AIRM2M"
#define LED_PIN (13)

#elif defined(TARGET_BOARD_ESP32_CAM)
#define BOARD_NAME "ESP32-CAM"
#define LED_PIN (33)
#define LED_POLARITY_INV

#else
#error "not support board"

#endif

#endif // __BOARD_H__
