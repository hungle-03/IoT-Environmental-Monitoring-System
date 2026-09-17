#include <stdio.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "input_gpio.h"

input_gpio_callback_t input_callback = NULL;

static void IRAM_ATTR input_gpio_handler(void *arg)
{
    int gpio_num = (uint32_t)arg;

    if (input_callback != NULL) {
        input_callback(gpio_num);
    }
}

void input_gpio_create(gpio_num_t gpio_num,
                       interrupt_type_edle_t intr_type)
{
    gpio_reset_pin(gpio_num);

    gpio_set_direction(gpio_num, GPIO_MODE_INPUT);

    gpio_set_pull_mode(gpio_num, GPIO_PULLUP_ONLY);

    gpio_set_intr_type(gpio_num, (gpio_int_type_t)intr_type);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(
        gpio_num,
        input_gpio_handler,
        (void *)gpio_num
    );
}

int input_gpio_get_level(gpio_num_t gpio_num)
{
    return gpio_get_level(gpio_num);
}

void input_gpio_callback(input_gpio_callback_t callback)
{
    input_callback = callback;
}