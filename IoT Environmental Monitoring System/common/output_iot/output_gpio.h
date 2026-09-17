#ifndef OUTPUT_GPIO_H
#define OUTPUT_GPIO_H

#include "esp_err.h"
#include "hal/gpio_types.h"




void output_gpio_create(gpio_num_t gpio_nume);
void output_gpio_set_level(gpio_num_t gpio_num, int level);
void output_gpio_toggle(gpio_num_t gpio_num);

#endif