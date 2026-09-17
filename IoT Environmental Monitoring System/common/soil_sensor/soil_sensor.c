
#include "soil_sensor.h"

#define SOIL_SENSOR_ADC_DRY    4095
#define SOIL_SENSOR_ADC_WET    2100

static adc_channel_t soil_channel;

static bool SOIL_SENSOR_GPIO_To_Channel(gpio_num_t gpio_num)
{
    switch (gpio_num)
    {
        case GPIO_NUM_32:
            soil_channel = ADC_CHANNEL_4;
            break;

        case GPIO_NUM_33:
            soil_channel = ADC_CHANNEL_5;
            break;

        case GPIO_NUM_34:
            soil_channel = ADC_CHANNEL_6;
            break;

        case GPIO_NUM_35:
            soil_channel = ADC_CHANNEL_7;
            break;

        default:
            return false;
    }

    return true;
}

void SOIL_SENSOR_Init(gpio_num_t gpio_num)
{
    if (!SOIL_SENSOR_GPIO_To_Channel(gpio_num))
    {
        return;
    }

    adc_init(ADC_UNIT_1);

    adc_config_channel(
        soil_channel,
        ADC_ATTEN_DB_12,
        ADC_BITWIDTH_DEFAULT
    );
}

struct soil_sensor_read SOIL_SENSOR_Read(void)
{
    struct soil_sensor_read result = {
        .status = 0,
        .adc_value = 0,
        .soil_moisture = 0.0f
    };

    int raw = 0;

    if (adc_read(soil_channel, &raw) != ESP_OK)
    {
        return result;
    }

    result.adc_value = raw;

    result.soil_moisture =
        ((float)(SOIL_SENSOR_ADC_DRY - raw) /
         (SOIL_SENSOR_ADC_DRY - SOIL_SENSOR_ADC_WET)) * 100.0f;

    if (result.soil_moisture < 0.0f)
    {
        result.soil_moisture = 0.0f;
    }

    if (result.soil_moisture > 100.0f)
    {
        result.soil_moisture = 100.0f;
    }

    return result;
}

