#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define BUTTON       GPIO_NUM_7

static const char *TAG = "INFO";

uint32_t lastInterruptTime = 0;
uint32_t totalDelayTime = 0;
volatile bool buttonFlag = false;

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time());
}

static void IRAM_ATTR button_isr_handler(void* arg) 
{
    buttonFlag = true;
    lastInterruptTime = millis();
}

void app_main(void)
{
    gpio_reset_pin(BUTTON);
    gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

    gpio_set_intr_type(BUTTON, GPIO_INTR_NEGEDGE);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON, button_isr_handler, NULL);

    int buttonPressCounter = 0;

    while (1)
    {
        if (buttonFlag) {
            buttonFlag = false;

            buttonPressCounter++; 
            
            uint32_t interruptDelay = millis() - lastInterruptTime;
            totalDelayTime += interruptDelay;
            float avgDelay = (float) (totalDelayTime / buttonPressCounter) / 1000;

            ESP_LOGI(TAG, "Button presses : %d. \n", buttonPressCounter);
            ESP_LOGI(TAG, "Delay time in us : %d. Avg delay : %0.2f \n", interruptDelay, avgDelay);
        }

        vTaskDelay(1);
    }
}