#include <stdbool.h>
#include <stdint.h>
#include "buzzer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/ledc.h"
#include "driver/gpio.h"

#define BUZZER_PIN      GPIO_NUM_2
#define LEDC_TIMER      LEDC_TIMER_1
#define LEDC_MODE       LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL    LEDC_CHANNEL_1
#define LEDC_DUTY_RES   LEDC_TIMER_10_BIT
#define LEDC_DUTY       512

#define NOTE_GAP_MS     30

// Частоти нот
#define NOTE_E3   165
#define NOTE_A3   220
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_E4   329
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_C5   523
#define NOTE_E5   659
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_C6   1047
#define NOTE_REST 0

typedef struct {
    uint16_t freq;
    uint16_t duration_ms;
} note_t;

static const note_t melody_success[] = {
    {NOTE_C5, 120}, {NOTE_E5, 120}, {NOTE_G5, 120}, {NOTE_C6, 400},
    {NOTE_G5, 120}, {NOTE_C6, 400}
};

static const note_t melody_wrong[] = {
    {NOTE_A3, 200}, {NOTE_E3, 500},
};

static const note_t melody_alarm[] = {
    {NOTE_A5, 250}, {NOTE_E5, 250},
    {NOTE_A5, 250}, {NOTE_E5, 250},
};

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

static QueueHandle_t s_queue; // store last command

static void tone_off(void)
{
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static void tone_on(uint16_t freq)
{
    if (freq == NOTE_REST) {
        tone_off();
        return;
    }
    ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq);
    ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY);
    ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
}

static bool wait_or_interrupt(uint32_t ms, buzzer_cmd_t *next)
{
    return xQueueReceive(s_queue, next, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static bool play_melody(const note_t *m, size_t len, buzzer_cmd_t *next)
{
    for (size_t i = 0; i < len; i++) {
        tone_on(m[i].freq);
        if (wait_or_interrupt(m[i].duration_ms, next)) {
            tone_off();
            return true;
        }
        tone_off();
        if (wait_or_interrupt(NOTE_GAP_MS, next)) {
            return true;
        }
    }
    return false;
}

static void buzzer_task(void *arg)
{
    buzzer_cmd_t cmd = BUZZER_STOP;

    while (1) {
        if (cmd == BUZZER_STOP) {
            tone_off();
            xQueueReceive(s_queue, &cmd, portMAX_DELAY);   // sleep until next task
            continue;
        }

        const note_t *m = NULL;
        size_t len = 0;
        bool loop = false;

        switch (cmd) {
        case BUZZER_MELODY_SUCCESS: m = melody_success; len = ARRAY_LEN(melody_success); break;
        case BUZZER_MELODY_WRONG:   m = melody_wrong;   len = ARRAY_LEN(melody_wrong);   break;
        case BUZZER_MELODY_ALARM:   m = melody_alarm;   len = ARRAY_LEN(melody_alarm);   loop = true; break;
        default: cmd = BUZZER_STOP; continue;
        }

        bool interrupted;
        do {
            interrupted = play_melody(m, len, &cmd);   // new cmd on interrupt
        } while (!interrupted && loop);

        if (!interrupted) {
            cmd = BUZZER_STOP;
        }
    }
}

void buzzer_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_DUTY_RES,
        .freq_hz         = 1000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    ledc_channel_config_t channel = {
        .gpio_num   = BUZZER_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .timer_sel  = LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&channel));

    s_queue = xQueueCreate(1, sizeof(buzzer_cmd_t));
    xTaskCreate(buzzer_task, "buzzer", 2048, NULL, 5, NULL);
}

void buzzer_play(buzzer_cmd_t melody)
{
    xQueueOverwrite(s_queue, &melody);   // new cmd, changes last
}

void buzzer_stop(void)
{
    buzzer_play(BUZZER_STOP);
}