#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"

#define LED          GPIO_NUM_4
#define BUTTON       GPIO_NUM_7
#define DEBOUNCE_MS  30

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void app_main(void)
{

    gpio_reset_pin(LED);
    gpio_set_direction(LED, GPIO_MODE_OUTPUT);
    gpio_set_level(LED, 0);

    gpio_reset_pin(BUTTON);
    gpio_set_direction(BUTTON, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON, GPIO_PULLUP_ONLY);

    bool led_on = false;

    int buttonState = 1;             // Поточний стабільний стан кнопки
    int lastButtonState = 1;         // Попереднє зчитане значення
    uint32_t lastDebounceTime = 0;   // Час останньої зміни стану

    while (1)
    {
        int reading = gpio_get_level(BUTTON);

        // Якщо стан змінився (через шум або реальне натискання)
        if (reading != lastButtonState) {
            lastDebounceTime = millis(); // Скидаємо таймер
        }

        // Якщо минуло достатньо часу (шум припинився)
        if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
            
            // Якщо стабільний стан дійсно змінився
            if (reading != buttonState) {
                buttonState = reading;
                
                // Тільки якщо кнопка натиснена (перехід в LOW / 0)
                if (buttonState == 0) {
                    led_on = !led_on;
                    gpio_set_level(LED, led_on);
                }
            }
        }

        lastButtonState = reading;

        // Обов'язкова пауза для FreeRTOS, щоб не блокувати процесор
        vTaskDelay(1);
    }
}