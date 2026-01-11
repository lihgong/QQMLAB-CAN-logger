#include "driver/gpio.h"
#include "./board.h"

#define LED_ON (0)
#define LED_OFF (1)

void led_init(void)
{
    gpio_reset_pin(GPIO_LED_BREATH);
    gpio_set_direction(GPIO_LED_BREATH, GPIO_MODE_INPUT_OUTPUT); // use INPUT_OUTPUT to allow read back the status
    gpio_set_level(GPIO_LED_BREATH, LED_OFF);
}

void led_op(uint32_t op_0on_1off_2toggle)
{
    if (op_0on_1off_2toggle == 0) {
        gpio_set_level(GPIO_LED_BREATH, LED_ON);
    } else if (op_0on_1off_2toggle == 1) {
        gpio_set_level(GPIO_LED_BREATH, LED_OFF);
    } else if (op_0on_1off_2toggle == 2) {
        uint32_t gpio_read = gpio_get_level(GPIO_LED_BREATH);
        gpio_set_level(GPIO_LED_BREATH, !gpio_read);
    }
}

uint32_t led_is_on(void)
{
    return (gpio_get_level(GPIO_LED_BREATH) == LED_ON) ? 1 : 0;
}
