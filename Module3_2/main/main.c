#include <stdio.h>
#include <math.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define ADC_UNIT ADC_UNIT_1       // АЦП1
#define ADC_CHANNEL ADC_CHANNEL_5 // GPIO6


static const char *TAG= "ADC_LAB";

void app_main(void)
{
    // ADC 
    adc_oneshot_unit_handle_t adc1_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };
    adc_oneshot_new_unit(&init_config, &adc1_handle);

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL, &chan_config);

    adc_cali_handle_t cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT,
        .chan = ADC_CHANNEL,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

    if (ret != ESP_OK)
    {
        ESP_LOGI(TAG, "Заводське калібрування недоступне на цьому чипі");
    }


    while (1)
    {
        int raw = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL, &raw);

        int voltage_mv_calibrated = 0;
        adc_cali_raw_to_voltage(cali_handle, raw, &voltage_mv_calibrated); // ТОЧНЕ переведення з урахуванням eFuse-даних чипа

        float voltage_mv_naive = (raw / 4095.0f) * 3300.0f; // ПРИБЛИЗНЕ переведення "на око"

        float deviation = 0.0f;

        if (raw != 0) {
            deviation = fabsf((float)voltage_mv_calibrated - raw) / fabsf((float)raw) * 100.0f;
        }

        // ESP_LOGI(TAG, "raw=%4d   calibrated=%d mV   naive=%.0f mV   diff=%d mV   deviation=%0.2f",
        //          raw, voltage_mv_calibrated, voltage_mv_naive,
        //          voltage_mv_calibrated - (int)voltage_mv_naive,
        //          deviation);

        printf(">raw:%d\n", raw);
        printf(">calibrated:%d\n", voltage_mv_calibrated);
        printf(">naive:%0.2f\n", voltage_mv_naive);
        printf(">deviation:%0.2f\n", deviation);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}