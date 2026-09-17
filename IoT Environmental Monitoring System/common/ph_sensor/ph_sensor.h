
#ifndef PH_SENSOR_H
#define PH_SENSOR_H

#include "hal/gpio_types.h"
#include "adc.h"

struct ph_sensor_read
{
    int status;
    int adc_value;
    int voltage_mv;
    float ph;
};

void PH_SENSOR_Init(gpio_num_t gpio_num);

struct ph_sensor_read PH_SENSOR_Read(void);

#endif

