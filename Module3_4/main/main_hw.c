
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

// GPIO
#define BUZZER_PIN       GPIO_NUM_2
#define BUTTON_PIN       GPIO_NUM_5
#define BUTTON_DEBOUNCE_MS 50

// LEDC
#define LEDC_TIMER       LEDC_TIMER_0
#define LEDC_MODE        LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL     LEDC_CHANNEL_0
#define LEDC_DUTY_RES    LEDC_TIMER_10_BIT
#define LEDC_DUTY        512

// Notes
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   329
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   493
#define NOTE_C5   523
#define NOTE_REST 0

static const char *TAG = "buzzer_synth";

static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time());
}

// Note struct
typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

// Melody struct
typedef struct {
    const note_t *notes;
    size_t length;
} melody_t;

// Baby Shark
static const note_t melody1[] = {
    {NOTE_REST, 400},
    {NOTE_C4, 250}, {NOTE_D4, 250}, {NOTE_E4, 250},
    {NOTE_C4, 250}, {NOTE_D4, 250}, {NOTE_E4, 250},
    {NOTE_C4, 250}, {NOTE_D4, 250}, {NOTE_E4, 500}
};

// Jingle Bells
static const note_t melody2[] = {
    {NOTE_REST, 400},
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 500},
    {NOTE_E4, 250}, {NOTE_E4, 250}, {NOTE_E4, 500},
    {NOTE_E4, 250}, {NOTE_G4, 250},
    {NOTE_C4, 250}, {NOTE_D4, 250}, {NOTE_E4, 800}
};

// We Will Rock You
static const note_t melody3[] = {
    {NOTE_REST, 400},
    {NOTE_C4, 300}, {NOTE_C4, 300}, {NOTE_D4, 500},
    {NOTE_C4, 300}, {NOTE_C4, 300}, {NOTE_D4, 500},
    {NOTE_C4, 300}, {NOTE_C4, 300}, {NOTE_D4, 300},
    {NOTE_E4, 700}
};

// Twinkle Twinkle Little Star
static const note_t melody4[] = {
    {NOTE_REST, 400},
    {NOTE_C4, 300}, {NOTE_C4, 300},
    {NOTE_G4, 300}, {NOTE_G4, 300},
    {NOTE_A4, 300}, {NOTE_A4, 300},
    {NOTE_G4, 600}
};

static const melody_t melodies[] = {
    {melody1, sizeof(melody1) / sizeof(melody1[0])},
    {melody2, sizeof(melody2) / sizeof(melody2[0])},
    {melody3, sizeof(melody3) / sizeof(melody3[0])},
    {melody4, sizeof(melody4) / sizeof(melody4[0])}
};

#define MELODIES_COUNT (sizeof(melodies) / sizeof(melodies[0]))

static volatile int current_melody = 0;

static void buzzer_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_MODE,
        .timer_num = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz = 1000,
        .clk_cfg = LEDC_AUTO_CLK
    };

    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode = LEDC_MODE,
        .channel = LEDC_CHANNEL,
        .timer_sel = LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = BUZZER_PIN,
        .duty = 0,
        .hpoint = 0
    };

    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

volatile bool buttonPressed = false;
uint32_t lastButtonTime = 0;

static void IRAM_ATTR button_isr_handler(void *arg)
{
   buttonPressed = true;
}

static void button_init(void)
{
    gpio_config_t button_config = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE
    };

    gpio_config(&button_config);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(BUTTON_PIN, button_isr_handler, NULL);
}
static void play_note(uint16_t freq, uint16_t duration_ms)
{
    if (freq == NOTE_REST) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        ESP_ERROR_CHECK(ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq));

        ESP_ERROR_CHECK(
            ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY)
        );
        ESP_ERROR_CHECK(
            ledc_update_duty(LEDC_MODE, LEDC_CHANNEL)
        );
    }

    vTaskDelay(pdMS_TO_TICKS(duration_ms));

    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);

    vTaskDelay(pdMS_TO_TICKS(50));
}

void app_main(void)
{
    ESP_LOGI(TAG, "Initializing buzzer...");

    buzzer_init();
    button_init();

    while (1) {
        uint32_t currentMillis = millis();

        if (buttonPressed) {
            buttonPressed = false;
            if (currentMillis - lastButtonTime > BUTTON_DEBOUNCE_MS) {
                current_melody = (current_melody + 1) % MELODIES_COUNT;
            }
        }

        int selected = current_melody;
       
        const melody_t *melody = &melodies[selected];

        ESP_LOGI(TAG, "Playing melody %d", selected + 1);

        for (size_t i = 0; i < melody->length; i++) {

            if (selected != current_melody) {
                break;
            }

            play_note(
                melody->notes[i].freq,
                melody->notes[i].duration_ms
            );
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}
