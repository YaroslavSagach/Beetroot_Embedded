#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED GPIO_NUM_4

static const char *TAG = "LED";

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void app_main(void)
{
    gpio_reset_pin(LED);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);

    bool level = 1;
    gpio_set_level(LED, level);
    uint32_t last = millis();

    while (1)
    {
        uint32_t interval = level ? 1500 : 500;

        if (millis() - last >= interval)
        {
            last += interval;
            level = !level;
            gpio_set_level(LED, level);
            ESP_LOGI(TAG, "Level: %d", level);
        }

        vTaskDelay(1);
    }
}