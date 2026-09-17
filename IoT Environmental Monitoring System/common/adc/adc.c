#include "adc.h"

#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define ADC_MAX_CHANNELS 10

static const char *TAG = "ADC";

static adc_oneshot_unit_handle_t adc_handle;

static adc_cali_handle_t cali_handle[ADC_MAX_CHANNELS];

static bool calibration_enabled[ADC_MAX_CHANNELS];

static adc_unit_t adc_unit;


/* =========================
 * INIT
 * ========================= */

esp_err_t adc_init(adc_unit_t unit)
{
    adc_oneshot_unit_init_cfg_t config =
    {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };

    adc_unit = unit;

    return adc_oneshot_new_unit(
        &config,
        &adc_handle
    );
}


/* =========================
 * CONFIG CHANNEL
 * ========================= */

esp_err_t adc_config_channel(adc_channel_t channel,
                             adc_atten_t atten,
                             adc_bitwidth_t bitwidth)
{
    adc_oneshot_chan_cfg_t config =
    {
        .atten = atten,
        .bitwidth = bitwidth,
    };

    esp_err_t ret;

    ret = adc_oneshot_config_channel(
        adc_handle,
        channel,
        &config
    );

    if (ret != ESP_OK)
        return ret;


    /* Calibration */

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

    adc_cali_curve_fitting_config_t cali_config =
    {
        .unit_id = adc_unit,
        .chan = channel,
        .atten = atten,
        .bitwidth = bitwidth,
    };

    ret = adc_cali_create_scheme_curve_fitting(
        &cali_config,
        &cali_handle[channel]
    );

    if (ret == ESP_OK)
    {
        calibration_enabled[channel] = true;

        ESP_LOGI(TAG,
                 "CH%d calibration enabled",
                 channel);

        return ESP_OK;
    }

#endif


#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

    adc_cali_line_fitting_config_t cali_config =
    {
        .unit_id = adc_unit,
        .atten = atten,
        .bitwidth = bitwidth,
    };

#if CONFIG_IDF_TARGET_ESP32
    cali_config.default_vref = 0;
#endif

    ret = adc_cali_create_scheme_line_fitting(
        &cali_config,
        &cali_handle[channel]
    );

    if (ret == ESP_OK)
    {
        calibration_enabled[channel] = true;

        ESP_LOGI(TAG,
                 "CH%d calibration enabled",
                 channel);

        return ESP_OK;
    }

#endif

    calibration_enabled[channel] = false;

    ESP_LOGW(TAG,
             "CH%d calibration unavailable",
             channel);

    return ESP_OK;
}


/* =========================
 * READ RAW
 * ========================= */

esp_err_t adc_read(adc_channel_t channel, int *raw)
{
    return adc_oneshot_read(
        adc_handle,
        channel,
        raw
    );
}


/* =========================
 * READ VOLTAGE
 * ========================= */

esp_err_t adc_read_voltage(adc_channel_t channel,
                           int *voltage_mv)
{
    int raw;

    esp_err_t ret = adc_read(
        channel,
        &raw
    );

    if (ret != ESP_OK)
        return ret;

    if (!calibration_enabled[channel])
        return ESP_ERR_NOT_SUPPORTED;

    return adc_cali_raw_to_voltage(
        cali_handle[channel],
        raw,
        voltage_mv
    );
}


/* =========================
 * DEINIT
 * ========================= */

esp_err_t adc_deinit(void)
{
    for (int i = 0; i < ADC_MAX_CHANNELS; i++)
    {
        if (!calibration_enabled[i])
            continue;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED

        adc_cali_delete_scheme_curve_fitting(
            cali_handle[i]
        );

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED

        adc_cali_delete_scheme_line_fitting(
            cali_handle[i]
        );

#endif

        calibration_enabled[i] = false;
        cali_handle[i] = NULL;
    }

    return adc_oneshot_del_unit(
        adc_handle
    );
}