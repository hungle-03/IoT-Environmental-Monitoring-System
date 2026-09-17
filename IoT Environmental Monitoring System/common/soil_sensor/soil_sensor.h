#ifndef SOIL_SENSOR_H
#define SOIL_SENSOR_H

#include "hal/gpio_types.h"
#include "adc.h"

struct soil_sensor_read
{
    int status;
    int adc_value;
    float soil_moisture;
};

void SOIL_SENSOR_Init(gpio_num_t gpio_num);

struct soil_sensor_read SOIL_SENSOR_Read(void);

#endif