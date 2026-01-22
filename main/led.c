#include "driver/gpio.h"
#include "./board.h"

#if defined(LED_POLARITY_INV)
#define LED_ON (0)
#define LED_OFF (1)
#else
#define LED_ON (1)
#define LED_OFF (0)
#endif

esp_err_t led_init(void)
{
    gpio_reset_pin(LED_PIN);
    gpio_set_direction(LED_PIN, GPIO_MODE_INPUT_OUTPUT); // use INPUT_OUTPUT to allow read back the status
    gpio_set_level(LED_PIN, LED_OFF);

    return ESP_OK;
}

void led_op(uint32_t op_0on_1off_2toggle)
{
    if (op_0on_1off_2toggle == 0) {
        gpio_set_level(LED_PIN, LED_ON);
    } else if (op_0on_1off_2toggle == 1) {
        gpio_set_level(LED_PIN, LED_OFF);
    } else if (op_0on_1off_2toggle == 2) {
        uint32_t gpio_read = gpio_get_level(LED_PIN);
        gpio_set_level(LED_PIN, !gpio_read);
    }
}

uint32_t led_is_on(void)
{
    return (gpio_get_level(LED_PIN) == LED_ON) ? 1 : 0;
}
