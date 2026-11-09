#include <stdio.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"

// ---------------- Налаштування пінів і параметрів енкодера ----------------
#define ENCODER_GPIO_A   5            // Пін CLK (сигнал A)
#define ENCODER_GPIO_B   6            // Пін DT (сигнал B)
#define ENCODER_GPIO_SW  7            // Пін SW (кнопка енкодера), оберіть будь-який вільний
#define ENCODER_PPR      20           // Кількість імпульсів на один оберт (PPR) енкодера
#define ENCODER_CPR      ENCODER_PPR  // Режим X1: 1 імпульс = 1 крок, тому CPR = PPR

#define LOOP_MS          10           // Період опитування кнопки, мс
#define PRINT_EVERY      10           // Виводити позицію кожні 10 ітерацій (10 * 10 мс = 100 мс)
#define DEBOUNCE_COUNT   3            // Скільки опитувань поспіль стан має триматись (антидребезг)

// Тег для виводу повідомлень у лог
static const char *TAG = "ENCODER";

// Дескриптор апаратного лічильника імпульсів (PCNT)
static pcnt_unit_handle_t pcnt_unit = NULL;

// Функція ініціалізації апаратного лічильника для енкодера
static void setup_encoder_pcnt(void)
{
    // Межі лічильника (діапазон 16-бітного знакового числа)
    pcnt_unit_config_t unit_config = {
        .high_limit = 32767,    // Верхня межа лічильника
        .low_limit  = -32768,   // Нижня межа лічильника
    };
    // Створюємо блок лічильника PCNT
    pcnt_new_unit(&unit_config, &pcnt_unit);

    // Апаратний фільтр завад: імпульси коротші за 10 мкс ігноруються
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 10000 };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

    // Дескриптор каналу лічильника
    pcnt_channel_handle_t chan_a = NULL;
    // Канал рахує фронти сигналу A, а сигнал B визначає напрямок обертання
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num  = ENCODER_GPIO_A,   // Пін, за фронтами якого ведеться рахунок
        .level_gpio_num = ENCODER_GPIO_B,   // Пін, за рівнем якого визначається напрямок
    };
    pcnt_new_channel(pcnt_unit, &chan_a_config, &chan_a);

    // Передній фронт A: +1, задній фронт A: ігнорувати
    pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_HOLD);

    // B = 1: напрямок як є, B = 0: напрямок інвертується
    pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(pcnt_unit);        // Вмикаємо блок лічильника
    pcnt_unit_clear_count(pcnt_unit);   // Скидаємо лічильник у 0
    pcnt_unit_start(pcnt_unit);         // Запускаємо рахунок

    ESP_LOGI(TAG, "PCNT X1 initialized");
}

// Функція ініціалізації піна кнопки
static void setup_button(void)
{
    gpio_config_t btn_config = {
        .pin_bit_mask = 1ULL << ENCODER_GPIO_SW,   // Маска піна кнопки
        .mode         = GPIO_MODE_INPUT,           // Пін працює як вхід
        .pull_up_en   = GPIO_PULLUP_DISABLE,       // Внутрішня підтяжка вимкнена (потрібна зовнішня)
        .pull_down_en = GPIO_PULLDOWN_DISABLE,     // Підтяжка до GND вимкнена
        .intr_type    = GPIO_INTR_DISABLE,         // Переривання не використовуємо, опитуємо в циклі
    };
    gpio_config(&btn_config);
}

void app_main(void)
{
    // Ініціалізуємо енкодер і кнопку
    setup_encoder_pcnt();
    setup_button();

    int stable_level = 1;   // Підтверджений (стабільний) стан кнопки: 1 = відпущена
    int debounce_cnt = 0;   // Лічильник підтвердження нового стану
    int print_cnt    = 0;   // Лічильник для періодичного виводу позиції

    while (1) {
        // ---------- Обробка кнопки з антидребезгом ----------
        int level = gpio_get_level(ENCODER_GPIO_SW);   // Поточний рівень на піні SW

        if (level == stable_level) {
            // Стан не змінився, скидаємо лічильник підтвердження
            debounce_cnt = 0;
        } else if (++debounce_cnt >= DEBOUNCE_COUNT) {
            // Новий стан тримається достатньо довго: приймаємо його
            stable_level = level;
            debounce_cnt = 0;

            if (stable_level == 0) {
                // Кнопку НАТИСНУЛИ (рівень 0)
                ESP_LOGI(TAG, "Кнопка натиснута");
                pcnt_unit_clear_count(pcnt_unit);   // Приклад дії: скидаємо позицію в 0
            } else {
                // Кнопку ВІДПУСТИЛИ (рівень 1)
                ESP_LOGI(TAG, "Кнопка відпущена");
            }
        }

        // ---------- Періодичний вивід позиції ----------
        if (++print_cnt >= PRINT_EVERY) {
            print_cnt = 0;

            int count = 0;
            pcnt_unit_get_count(pcnt_unit, &count);              // Зчитуємо лічильник
            float angle = count * 360.0f / ENCODER_CPR;          // Кут у градусах
            ESP_LOGI(TAG, "pos=%d steps, angle=%.1f deg", count, angle);
        }

        // Пауза до наступного опитування
        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}