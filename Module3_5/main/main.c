#include <stdio.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_log.h"

static const char *TAG = "SERVO";

/* ---------- Піни ---------- */
#define SERVO_GPIO          4

/* ---------- PWM (LEDC) ---------- */
#define SERVO_TIMER         LEDC_TIMER_0
#define SERVO_CHANNEL       LEDC_CHANNEL_0
#define SERVO_MODE          LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ       50                 // період 20 мс
#define SERVO_RES           LEDC_TIMER_14_BIT
#define SERVO_DUTY_MAX      ((1 << 14) - 1)    // 16383
#define SERVO_PERIOD_US     20000

/* ---------- Параметри SG90 ---------- */
#define SERVO_MIN_US        500
#define SERVO_MAX_US        2400

/* ---------- Межі duty (рахуються з імпульсів) ---------- */
#define DUTY_MIN            ((SERVO_MIN_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)   // ≈ 409
#define DUTY_MAX            ((SERVO_MAX_US * SERVO_DUTY_MAX) / SERVO_PERIOD_US)   // ≈ 1965

/* ---------- Параметри руху ---------- */
#define DUTY_STEP           8                  // крок duty за ітерацію
#define STEP_DELAY_MS       15                 // пауза між кроками (швидкість)
#define EDGE_PAUSE_MS       500                // пауза на краях

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
    ESP_ERROR_CHECK(ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, (uint32_t)duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_MODE, SERVO_CHANNEL));
}

void app_main(void)
{
    servo_init();
    ESP_LOGI(TAG, "Старт. Duty їздить %d..%d і назад", DUTY_MIN, DUTY_MAX);

    int duty = DUTY_MIN;
    int dir = DUTY_STEP;           // +STEP вперед, -STEP назад

    while (1) {
        servo_set_duty(duty);
        ESP_LOGI(TAG, "duty: %d", duty);

        /* наступний крок */
        duty += dir;

        if (duty >= DUTY_MAX) {            // правий край
            duty = DUTY_MAX;
            dir = -DUTY_STEP;
            servo_set_duty(duty);
            ESP_LOGI(TAG, "duty: %d (правий край)", duty);
            vTaskDelay(pdMS_TO_TICKS(EDGE_PAUSE_MS));
        } else if (duty <= DUTY_MIN) {     // лівий край
            duty = DUTY_MIN;
            dir = DUTY_STEP;
            servo_set_duty(duty);
            ESP_LOGI(TAG, "duty: %d (лівий край)", duty);
            vTaskDelay(pdMS_TO_TICKS(EDGE_PAUSE_MS));
        }

        vTaskDelay(pdMS_TO_TICKS(STEP_DELAY_MS));
    }
}