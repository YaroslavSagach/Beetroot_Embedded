#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#define ADC_UNIT ADC_UNIT_1           // АЦП1
#define ADC_CHANNEL_LDR ADC_CHANNEL_5 // GPIO6
#define LED GPIO_NUM_16

#define SMA_FILTER_WINDOW 10
#define LOW_THRESHOLD 1000
#define HIGH_THRESHOLD 2000

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
    adc_oneshot_config_channel(adc1_handle, ADC_CHANNEL_LDR, &chan_config);

    // LED 
    gpio_reset_pin(LED);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED, 0);


    // SMA filter
    int samples[SMA_FILTER_WINDOW] = {0};
    int sample_index = 0;
    int samples_count = 0;
    int samples_sum = 0;

    bool led_on = false;

    while (1)
    {
        int raw_ldr = 0;
        adc_oneshot_read(adc1_handle, ADC_CHANNEL_LDR, &raw_ldr);

        // SMA filter impl
        if (samples_count == SMA_FILTER_WINDOW) {
            samples_sum -= samples[sample_index];
        } else {
            samples_count++;
        }
        samples[sample_index] = raw_ldr;
        samples_sum += raw_ldr;
        sample_index = (sample_index + 1) % SMA_FILTER_WINDOW;

        int filtered_ldr = samples_sum / samples_count;

        ESP_LOGI(TAG, "raw_ldr=%4d   filtered(SMA)=%d", raw_ldr, filtered_ldr);

        if (!led_on && filtered_ldr < LOW_THRESHOLD) {
            ESP_LOGI(TAG, "The room is dark, turning LED ON!");
            led_on = true;
        }
        else if (led_on && filtered_ldr > HIGH_THRESHOLD) {
            ESP_LOGI(TAG, "The room is light, turning LED OFF!");
            led_on = false;
        }

        gpio_set_level(LED, led_on ? 1 : 0);

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}