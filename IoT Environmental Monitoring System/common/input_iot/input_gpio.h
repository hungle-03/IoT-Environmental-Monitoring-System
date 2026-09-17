#ifndef INPUT_GPIO_H
#define INPUT_GPIO_H

#include "esp_err.h"
#include "hal/gpio_types.h"

typedef enum {
    LO_TO_HI = 1,
    HI_TO_LO = 2,
    EDGE_ANY = 3
} interrupt_type_edle_t;

typedef void (*input_gpio_callback_t)(int);

void input_gpio_create(gpio_num_t gpio_num,
                       interrupt_type_edle_t intr_type);

int input_gpio_get_level(gpio_num_t gpio_num);

void input_gpio_callback(input_gpio_callback_t callback);

#endif