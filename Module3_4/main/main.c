// 3-бітний ЦАП R-2R на GPIO ESP32-S3
// GPIO6 = біт 2 (MSB), GPIO5 = біт 1, GPIO4 = біт 0 (LSB)
// Вихід: V = 3,3 В × код / 8  (код 0…7 → 0…≈2,89 В)

#include <math.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PIN_B0 GPIO_NUM_4  // LSB
#define PIN_B1 GPIO_NUM_5
#define PIN_B2 GPIO_NUM_6  // MSB

static const char *TAG = "dac3";

static void dac3_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_B0) | (1ULL << PIN_B1) | (1ULL << PIN_B2),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
}

// Записати код 0…7 у ЦАП
static inline void dac3_write(uint8_t code)
{
    gpio_set_level(PIN_B0, (code >> 0) & 1);
    gpio_set_level(PIN_B1, (code >> 1) & 1);
    gpio_set_level(PIN_B2, (code >> 2) & 1);
}


void app_main(void)
{
    dac3_init();

    // Тест: кожен код по 10 с, міряйте мультиметром на виході
    while (1) {
        for (int c = 0; c < 8; c++) {
            dac3_write(c);
            ESP_LOGI(TAG, "код %d → очікується %.3f В", c, 3.3f * c / 8.0f);
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
    }
}