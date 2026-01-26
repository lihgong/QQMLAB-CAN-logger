#include "driver/gpio.h"
#include "board.h"
#include "led.h"

#if defined(LED_POLARITY_INV)
#define LED_ON (0)
#define LED_OFF (1)
#else
#define LED_ON (1)
#define LED_OFF (0)
#endif

static uint8_t led_pins[LED_PIN_NUM] = {
#if defined(LED_PIN0)
    LED_PIN0,
#endif
#if defined(LED_PIN1)
    LED_PIN1,
#endif
};

esp_err_t led_init(void)
{
    for (uint32_t i = 0; i < LED_PIN_NUM; i++) {
        gpio_reset_pin(led_pins[i]);
        gpio_set_direction(led_pins[i], GPIO_MODE_INPUT_OUTPUT); // use INPUT_OUTPUT to allow read back the status
        gpio_set_level(led_pins[i], LED_OFF);
    }

    return ESP_OK;
}

void led_op_ext(uint32_t led_idx, uint32_t op_0on_1off_2toggle)
{
    if (led_idx < LED_PIN_NUM) {
        uint32_t led_pin = led_pins[led_idx];
        if (op_0on_1off_2toggle == 0) {
            gpio_set_level(led_pin, LED_ON);
        } else if (op_0on_1off_2toggle == 1) {
            gpio_set_level(led_pin, LED_OFF);
        } else if (op_0on_1off_2toggle == 2) {
            uint32_t gpio_read = gpio_get_level(led_pin);
            gpio_set_level(led_pin, !gpio_read);
        }
    }
}

void led_op(uint32_t op_0on_1off_2toggle) // legacy API, always addressing the LED0
{
    led_op_ext(0, op_0on_1off_2toggle);
}

uint32_t led_is_on_bmp(void)
{
    uint32_t ret = 0;
    for (uint32_t i = 0; i < LED_PIN_NUM; i++) {
        if (gpio_get_level(led_pins[i]) == LED_ON) {
            ret |= (1 << i);
        }
    }

    return ret; // return bitmap indicating each LED status
}
