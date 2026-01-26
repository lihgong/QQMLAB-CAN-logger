#ifndef __LED_H__
#define __LED_H__

#include "board.h"

#if !defined(LED_PIN0) && !defined(LED_PIN1)
#define LED_PIN_NUM (0)
#elif defined(LED_PIN0) && !defined(LED_PIN1)
#define LED_PIN_NUM (1)
#elif defined(LED_PIN0) && defined(LED_PIN1)
#define LED_PIN_NUM (2)
#else
#error "Wrong LED PIN configuration"
#endif

void led_op_ext(uint32_t led_idx, uint32_t op_0on_1off_2toggle);
void led_op(uint32_t op_0on_1off_2toggle);
uint32_t led_is_on_bmp(void);

#endif // __LED_H__
