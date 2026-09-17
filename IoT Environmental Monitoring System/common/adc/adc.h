#ifndef ADC_H
#define ADC_H

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

esp_err_t adc_init(adc_unit_t unit);

esp_err_t adc_config_channel(adc_channel_t channel,
                             adc_atten_t atten,
                             adc_bitwidth_t bitwidth);

esp_err_t adc_read(adc_channel_t channel, int *raw);

esp_err_t adc_read_voltage(adc_channel_t channel,
                           int *voltage_mv);

esp_err_t adc_deinit(void);

#endif