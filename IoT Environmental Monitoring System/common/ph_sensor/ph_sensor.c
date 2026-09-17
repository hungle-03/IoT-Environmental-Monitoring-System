
#include "ph_sensor.h"

static adc_channel_t ph_channel;

static bool PH_SENSOR_GPIO_To_Channel(gpio_num_t gpio_num)
{
    switch (gpio_num)
    {
        case GPIO_NUM_32:
            ph_channel = ADC_CHANNEL_4;
            break;

        case GPIO_NUM_33:
            ph_channel = ADC_CHANNEL_5;
            break;

        case GPIO_NUM_34:
            ph_channel = ADC_CHANNEL_6;
            break;

        case GPIO_NUM_35:
            ph_channel = ADC_CHANNEL_7;
            break;

        default:
            return false;
    }

    return true;
}

void PH_SENSOR_Init(gpio_num_t gpio_num)
{
    if (!PH_SENSOR_GPIO_To_Channel(gpio_num))
    {
        return;
    }

    adc_init(ADC_UNIT_1);

    adc_config_channel(
        ph_channel,
        ADC_ATTEN_DB_12,
        ADC_BITWIDTH_DEFAULT
    );
}


struct ph_sensor_read PH_SENSOR_Read(void)
{
    struct ph_sensor_read result = {
        .status = 0,
        .adc_value = 0,
        .voltage_mv = 0,
        .ph = 0.0f
    };

    int raw = 0;
    int voltage_mv = 0;

    if (adc_read(ph_channel, &raw) != ESP_OK)
    {
        return result;
    }

    if (adc_read_voltage(ph_channel, &voltage_mv) != ESP_OK)
    {
        return result;
    }

    result.adc_value = raw;
    result.voltage_mv = voltage_mv;

    // pH = -6 * V + 22
    // voltage_mv đơn vị mV
    result.ph = -0.006f * voltage_mv + 22.0f;

    return result;
}

