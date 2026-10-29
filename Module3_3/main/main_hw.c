
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

// GPIO
#define LED_PIN          6
#define MOTOR_PIN        2

// ADC
#define ADC_UNIT_USED    ADC_UNIT_1
#define LDR_CHANNEL      ADC_CHANNEL_4  // GPIO5
#define POT_CHANNEL      ADC_CHANNEL_3  // GPIO4

static adc_oneshot_unit_handle_t adc1_handle;

// PWM
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_TIMER_NUM   LEDC_TIMER_0
#define LEDC_FREQUENCY   1000
#define LEDC_BITS        10
#define LEDC_RESOLUTION  LEDC_TIMER_10_BIT
#define LEDC_MAX_DUTY    ((1 << LEDC_BITS) - 1)

#define LEDC_LED_CHANNEL    LEDC_CHANNEL_0
#define LEDC_MOTOR_CHANNEL  LEDC_CHANNEL_1

#define LDR_DARK_THRESHOLD   1000
#define LDR_BRIGHT_THRESHOLD 2000

static void set_duty(ledc_channel_t channel, uint32_t duty)
{
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, channel));
}

static void pwm_init(void)
{
    ledc_timer_config_t timer_config = {
        .speed_mode      = LEDC_MODE,
        .duty_resolution = LEDC_RESOLUTION,
        .timer_num       = LEDC_TIMER_NUM,
        .freq_hz         = LEDC_FREQUENCY,
        .clk_cfg         = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    // LED CHANNEL
    ledc_channel_config_t led_config = {
        .gpio_num   = LED_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_LED_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_NUM,
        .duty       = 0,
        .hpoint     = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&led_config));

    // MOTOR CHANNEL
    ledc_channel_config_t motor_config = {
        .gpio_num   = MOTOR_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_MOTOR_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER_NUM,
        .duty       = 0,
        .hpoint     = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&motor_config));
}

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_USED,
        .clk_src = 0,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_new_unit(&init_config, &adc1_handle)
    );

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc1_handle, LDR_CHANNEL, &channel_config
        )
    );

    ESP_ERROR_CHECK(
        adc_oneshot_config_channel(
            adc1_handle, POT_CHANNEL, &channel_config
        )
    );
}

void app_main(void)
{
    pwm_init();
    adc_init();

    while (1) {
        int ldr_raw = 0;
        int pot_raw = 0;

        ESP_ERROR_CHECK(
            adc_oneshot_read(adc1_handle, LDR_CHANNEL, &ldr_raw)
        );

        ESP_ERROR_CHECK(
            adc_oneshot_read(adc1_handle, POT_CHANNEL, &pot_raw)
        );

        // 12-бітний ADC (0..4095) -> 10-бітний PWM (0..1023)
        uint32_t led_duty;
        uint32_t motor_duty = pot_raw >> 2;

         // підгонка значень фоторезистора під duty ШІМ (чим темніше, тим світліше світить світлодіод)
        if (ldr_raw <= LDR_DARK_THRESHOLD) {
            led_duty = LEDC_MAX_DUTY;
        } else if (ldr_raw >= LDR_BRIGHT_THRESHOLD) {
            led_duty = 0;
        } else {
            led_duty = (LDR_BRIGHT_THRESHOLD - ldr_raw) * LEDC_MAX_DUTY
                    / (LDR_BRIGHT_THRESHOLD - LDR_DARK_THRESHOLD);
}

        set_duty(LEDC_LED_CHANNEL, led_duty);
        set_duty(LEDC_MOTOR_CHANNEL, motor_duty);

        printf("LDR: %d | LED PWM: %lu | POT: %d | Motor PWM: %lu\n",
               ldr_raw,
               (unsigned long)led_duty,
               pot_raw,
               (unsigned long)motor_duty);

        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
