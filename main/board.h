#ifndef __BOARD_H__
#define __BOARD_H__

#define TARGET_BOARD_ESP32C3
// #define TARGET_BOARD_ESP32_CAM

#if defined(TARGET_BOARD_ESP32C3)
#define GPIO_LED_BREATH (13)
#elif defined(TARGET_BOARD_ESP32_CAM)
#define GPIO_LED_BREATH (33)
#else
#error "not support board"
#endif

#endif // __BOARD_H__
