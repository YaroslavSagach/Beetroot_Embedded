#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

/* ---------- Піни ---------- */
#define SERVO_GPIO          4

/* ---------- ADC ---------- */
#define ADC_UNIT            ADC_UNIT_1
#define ADC_CHANNEL         ADC_CHANNEL_0    // GPIO1
#define ADC_ATTEN           ADC_ATTEN_DB_12

/* ---------- PWM (LEDC) ---------- */
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ       50
#define SERVO_RES           LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX      ((1 << 14) - 1)
#define SERVO_PERIOD_US     20000

/* ---------- Параметри SG90 ---------- */
#define SERVO_MIN_US        500
#define SERVO_MAX_US        2500

#define DUTY_MIN            ((SERVO_MIN_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)
#define DUTY_MAX            ((SERVO_MAX_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)

/* ---------- ADC ---------- */
#define ADC_MAX_VALUE       4095

static adc_oneshot_unit_handle_t adc_handle;

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t channel_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, ADC_CHANNEL, &channel_config
    ));
}

static void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = SERVO_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = DUTY_MIN,
        .hpoint     = 0,
    };

    ESP_ERROR_CHECK(ledc_channel_config(&channel));
}

static void servo_set_duty(int duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(
        SERVO_MODE, SERVO_CHANNEL, (uint32_t)duty
    ));

    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, SERVO_CHANNEL));
}

void app_main(void)
{
    adc_init();
    servo_init();

    while (1) {
        int adc_raw = 0;

        ESP_ERROR_CHECK(adc_oneshot_read(
            adc_handle, ADC_CHANNEL, &adc_raw
        ));

        // ADC (0-4095) -> Pulse (500-2500 us)
        int pulse_us = SERVO_MIN_US +
            (adc_raw * (SERVO_MAX_US - SERVO_MIN_US)) / ADC_MAX_VALUE;

        // Pulse -> Duty
        int duty = (pulse_us * SERVO_DUTY_MAX) / SERVO_PERIOD_US;

        // Pulse -> Angle (0-180)
        float angle = (float)(pulse_us - SERVO_MIN_US) * 180.0f /
                      (SERVO_MAX_US - SERVO_MIN_US);

        servo_set_duty(duty);

        ESP_LOGI(TAG, "ADC: %d | Angle: %.1f | Pulse: %d us | Duty: %d",
                 adc_raw, angle, pulse_us, duty);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}