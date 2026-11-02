#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Піни та параметри LEDC
#define BUZZER_PIN          GPIO_NUM_2
#define LEDC_TIMER          LEDC_TIMER_0
#define LEDC_MODE           LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL        LEDC_CHANNEL_0
#define LEDC_DUTY_RES       LEDC_TIMER_10_BIT // Роздільна здатність 10 біт (0 - 1023)
#define LEDC_DUTY           512               // 50% заповнення (меандр для максимальної гучності)

// Частоти музичних нот (октава 4)
#define NOTE_C4  262
#define NOTE_D4  294
#define NOTE_E4  329
#define NOTE_F4  349
#define NOTE_G4  392
#define NOTE_A4  440
#define NOTE_B4  493
#define NOTE_C5  523
#define NOTE_REST 0       // Пауза (тиша)

static const char *TAG = "buzzer_synth";

// Структура для опису ноти: частота та тривалість у мілісекундах
typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

// Мелодія: "Jingle Bells" (E E E | E E E | E G C D E)
static const note_t melody[] = {
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 500}, // E E E
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 500}, // E E E
    {NOTE_E4, 250}, {NOTE_G4, 250}, {NOTE_C4, 250}, {NOTE_D4, 250}, {NOTE_E4, 800}, // E G C D E
    {NOTE_REST, 400} // Пауза перед повтором
};

// Ініціалізація LEDC PWM
static void buzzer_init(void)
{
    // 1. Налаштування таймера LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .timer_num        = LEDC_TIMER,
        .duty_resolution  = LEDC_DUTY_RES,
        .freq_hz          = 1000, // Початкова частота
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 2. Налаштування каналу LEDC
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = BUZZER_PIN,
        .duty           = 0, // Початково вимкнено (duty = 0)
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

// Функція відтворення однієї ноти
static void play_note(uint16_t freq, uint16_t duration_ms)
{
    if (freq == NOTE_REST) {
        // Якщо пауза — вимикаємо PWM (duty = 0)
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        // Змінюємо частоту ноти
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq);
        // Встановлюємо 50% заповнення для генерації звуку
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
    
    // Тривалість ноти
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // Коротке вимкнення між нотами, щоб вони зливалися окремо
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    vTaskDelay(pdMS_TO_TICKS(50)); 
}

void app_main(void)
{
    ESP_LOGI(TAG, "Ініціалізація синтезатора на LEDC PWM...");
    buzzer_init();

    int melody_len = sizeof(melody) / sizeof(melody[0]);

    while (1) {
        ESP_LOGI(TAG, "Відтворення мелодії...");
        for (int i = 0; i < melody_len; i++) {
            play_note(melody[i].freq, melody[i].duration_ms);
        }
        // Пауза перед наступним циклом мелодії
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}