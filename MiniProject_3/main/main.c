#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"
#include "servo.h"
#include "buzzer.h"

// Encoder
#define ENCODER_GPIO_A   5
#define ENCODER_GPIO_B   6
#define ENCODER_GPIO_SW  7

#define LOOP_MS          10
#define DEBOUNCE_COUNT   3
#define POS_MODULO       10
#define COUNTS_PER_STEP  4       // X4: 4 pulses = 1 detent. If one detent moves 2 digits, set to 2
#define ENC_SIGN         1       // 1 or -1: CW must increase the position. If reversed, set to -1
#define PCNT_LIMIT       10000
#define RECENTER_AT      5000

#define PASSWORD      "1234"     // 4 digits, leading zeros allowed, e.g. "0427"
#define CODE_LENGTH   4
#define MAX_ATTEMPTS  3

typedef enum {
    STATE_LOCKED,   // safe is closed, accepting a code
    STATE_OPEN,     // correct code, servo open
    STATE_ALARM     // attempts exhausted
} safe_state_t;

static safe_state_t safe_state    = STATE_LOCKED;
static int          used_attempts = 0;

static const char *TAG = "SAFE";

typedef enum { DIR_NONE = 0, DIR_CW, DIR_CCW } enc_dir_t;

static pcnt_unit_handle_t pcnt_unit = NULL;

static void setup_encoder_pcnt(void)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_LIMIT,
        .low_limit  = -PCNT_LIMIT,
    };
    pcnt_new_unit(&unit_config, &pcnt_unit);

    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 10000 };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

    // Channel 1: count edges of A, B defines direction
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num  = ENCODER_GPIO_A,
        .level_gpio_num = ENCODER_GPIO_B,
    };
    pcnt_new_channel(pcnt_unit, &chan_a_config, &chan_a);

    // Channel 2: count edges of B, A defines direction
    pcnt_channel_handle_t chan_b = NULL;
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num  = ENCODER_GPIO_B,
        .level_gpio_num = ENCODER_GPIO_A,
    };
    pcnt_new_channel(pcnt_unit, &chan_b_config, &chan_b);

    // Channel A: rising -> +1, falling -> -1; B low inverts
    pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    // Channel B: mirror of A
    pcnt_channel_set_edge_action(chan_b,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

    pcnt_unit_enable(pcnt_unit);
    pcnt_unit_clear_count(pcnt_unit);
    pcnt_unit_start(pcnt_unit);

    ESP_LOGI(TAG, "PCNT X4 initialized");
}

static void setup_button(void)
{
    gpio_config_t btn_config = {
        .pin_bit_mask = 1ULL << ENCODER_GPIO_SW,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,   // external pull-up on the KY-040 module
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_config);
}

static char code_entered[CODE_LENGTH + 1] = {0};
static int  code_entered_count = 0;

static void code_reset(void)
{
    memset(code_entered, 0, sizeof(code_entered));
    code_entered_count = 0;
}

// Full safe reset (encoder button)
static void safe_reset(void)
{
    buzzer_stop();          // stops the alarm / any melody
    servo_close();
    safe_state    = STATE_LOCKED;
    used_attempts = 0;
    code_reset();
}

static void on_code_correct(void)
{
    ESP_LOGI(TAG, "Code CORRECT, opening");
    safe_state = STATE_OPEN;
    servo_open();
    buzzer_play(BUZZER_MELODY_SUCCESS);
}

static void on_alarm(void)
{
    ESP_LOGE(TAG, "!!! ALARM !!!");
    safe_state = STATE_ALARM;
    buzzer_play(BUZZER_MELODY_ALARM);   // loops until the button is pressed
}

static void on_code_wrong(void)
{
    used_attempts++;
    ESP_LOGW(TAG, "Code WRONG (attempt %d/%d)", used_attempts, MAX_ATTEMPTS);

    if (used_attempts >= MAX_ATTEMPTS) {
        on_alarm();                          // alarm replaces the "wrong code" melody
    } else {
        buzzer_play(BUZZER_MELODY_WRONG);
    }
}

// Safe code entry logic: called on every direction change
static void on_direction_change(int digit, enc_dir_t new_dir)
{
    if (safe_state != STATE_LOCKED) {
        return;   // safe is open or alarm in progress: ignore input until button press
    }

    code_entered[code_entered_count++] = '0' + digit;
    ESP_LOGW(TAG, ">>> Digit %d accepted (%d/%d), now %s <<<",
             digit, code_entered_count, CODE_LENGTH, new_dir == DIR_CW ? "Clockwise" : "CounterClockWise");

    if (code_entered_count == CODE_LENGTH) {
        if (strcmp(code_entered, PASSWORD) == 0) {
            on_code_correct();
        } else {
            on_code_wrong();
        }
        code_reset();
    }
}

void app_main(void)
{
    setup_encoder_pcnt();
    setup_button();
    servo_init();
    buzzer_init();

    int       last_raw = 0;
    int       acc      = 0;          // accumulated pulses (until a full detent)
    int       position = 0;          // 0..9
    enc_dir_t dir      = DIR_NONE;

    int stable_button_level = 1;
    int debounce_cnt = 0;

    while (1) {
        // ---------- Button: reset ----------
        int level = gpio_get_level(ENCODER_GPIO_SW);

        if (level == stable_button_level) {
            debounce_cnt = 0;
        } else if (++debounce_cnt >= DEBOUNCE_COUNT) {
            stable_button_level = level;
            debounce_cnt = 0;

            if (stable_button_level == 0) {
                ESP_LOGI(TAG, "Button pressed: Safe reset!");
                pcnt_unit_clear_count(pcnt_unit);
                last_raw = 0;
                acc      = 0;
                position = 0;
                dir      = DIR_NONE;
                safe_reset();
            }
        }

        // ---------- Encoder ----------
        int raw = 0;
        pcnt_unit_get_count(pcnt_unit, &raw);
        acc += ENC_SIGN * (raw - last_raw);
        last_raw = raw;

        int steps = acc / COUNTS_PER_STEP;     // full detents (signed)
        if (steps != 0) {
            acc -= steps * COUNTS_PER_STEP;

            enc_dir_t new_dir = (steps > 0) ? DIR_CW : DIR_CCW;

            if (dir != DIR_NONE && new_dir != dir) {
                on_direction_change(position, new_dir);   // digit BEFORE the new movement
            }
            dir = new_dir;

            // CW adds, CCW subtracts
            position = ((position + steps) % POS_MODULO + POS_MODULO) % POS_MODULO;
            ESP_LOGI(TAG, "pos=%d dir=%s", position, dir == DIR_CW ? "CW" : "CCW");

        } else if (acc == 0 && abs(raw) > RECENTER_AT) {
            // Encoder is idle: safely bring the raw counter back to 0
            pcnt_unit_clear_count(pcnt_unit);
            last_raw = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}