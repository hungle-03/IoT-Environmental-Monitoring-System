#include "driver/gpio.h"
#include "output_gpio.h"

void output_gpio_create(gpio_num_t gpio_num)
{
    gpio_reset_pin(gpio_num);
    gpio_set_direction(gpio_num, GPIO_MODE_OUTPUT);
}

void output_gpio_set_level(gpio_num_t gpio_num, int level)
{
    gpio_set_level(gpio_num, level);
}

void output_gpio_toggle(gpio_num_t gpio_num)
{

    gpio_set_level(gpio_num, 1);
    
}