#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/pulse_cnt.h"
#include "driver/gpio.h"

// Encoder
#define ENCODER_GPIO_A   5
#define ENCODER_GPIO_B   6
#define ENCODER_GPIO_SW  7

#define LOOP_MS          10
#define DEBOUNCE_COUNT   3
#define COUNTS_PER_STEP  4        // X4: 4 імпульси = 1 клік. Якщо 1 клік дає 2 кроки — поставте 2
#define PCNT_LIMIT       10000
#define RECENTER_AT      5000

typedef enum {
    IDLE,
    WAITING_FOR_FIRST_OPERAND,
    WAITING_FOR_OPERATION,
    WAITING_FOR_SECOND_OPERAND
} calc_state_t;

typedef enum {
    OP_DIV = 0,
    OP_MUL,
    OP_ADD,
    OP_SUB
} calc_op_t;

static const int OP_COUNT = 4;

static const char *OP_SYMBOL[4] = { "/", "*", "+", "-" };

static const char *TAG = "CALC";

static calc_state_t calc_state = IDLE;
static int          first_operand   = 0;
static int          second_operand   = 0;
static calc_op_t    operation  = OP_DIV;

static pcnt_unit_handle_t pcnt_unit = NULL;

/* ---------------- Encoder (PCNT X4) ---------------- */

static void setup_encoder_pcnt(void)
{
    pcnt_unit_config_t unit_config = {
        .high_limit = PCNT_LIMIT,
        .low_limit  = -PCNT_LIMIT,
    };
    pcnt_new_unit(&unit_config, &pcnt_unit);

    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 10000 };
    pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);

    pcnt_channel_handle_t chan_a = NULL;
    pcnt_chan_config_t chan_a_config = {
        .edge_gpio_num  = ENCODER_GPIO_A,
        .level_gpio_num = ENCODER_GPIO_B,
    };
    pcnt_new_channel(pcnt_unit, &chan_a_config, &chan_a);

    pcnt_channel_handle_t chan_b = NULL;
    pcnt_chan_config_t chan_b_config = {
        .edge_gpio_num  = ENCODER_GPIO_B,
        .level_gpio_num = ENCODER_GPIO_A,
    };
    pcnt_new_channel(pcnt_unit, &chan_b_config, &chan_b);

    pcnt_channel_set_edge_action(chan_a,
        PCNT_CHANNEL_EDGE_ACTION_DECREASE,
        PCNT_CHANNEL_EDGE_ACTION_INCREASE);
    pcnt_channel_set_level_action(chan_a,
        PCNT_CHANNEL_LEVEL_ACTION_KEEP,
        PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

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
        .pull_up_en   = GPIO_PULLUP_DISABLE,   // зовнішній pull-up на KY-040
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&btn_config);
}

/* ---------------- Calculator logic ---------------- */

static void on_encoder_steps(int steps)
{
    switch (calc_state) {
    case WAITING_FOR_FIRST_OPERAND:
        first_operand = first_operand + steps;
        ESP_LOGI(TAG, "Operand 1 = %d", first_operand);
        break;

    case WAITING_FOR_OPERATION:
        // циклічний вибір між 4 операціями
        operation = (calc_op_t)((((int)operation + steps) % OP_COUNT + OP_COUNT) % OP_COUNT);
        ESP_LOGI(TAG, "Operation: %s", OP_SYMBOL[operation]);
        break;

    case WAITING_FOR_SECOND_OPERAND:
        second_operand = second_operand + steps;
        ESP_LOGI(TAG, "Operand 2 = %d", second_operand);
        break;

    case IDLE:
    default:
        break;
    }
}

static void calculate_and_print(void)
{
    double result = 0;

    switch (operation) {
    case OP_DIV:
        if (second_operand == 0) {
            ESP_LOGE(TAG, "%d / %d = ERROR (division by zero)", first_operand, second_operand);
            return;
        }
        result = (double)first_operand / (double)second_operand;
        break;
    case OP_MUL: result = (double)first_operand * second_operand; break;
    case OP_ADD: result = (double)first_operand + second_operand; break;
    case OP_SUB: result = (double)first_operand - second_operand; break;
    default: break;
    }

    ESP_LOGI(TAG, "RESULT: %d %s %d = %g",
             first_operand, OP_SYMBOL[operation], second_operand, result);
}

// Натискання кнопки: перехід між станами
static void on_button_press(void)
{
    switch (calc_state) {
    case IDLE:
        first_operand  = 0;
        second_operand  = 0;
        operation = OP_DIV;
        calc_state = WAITING_FOR_FIRST_OPERAND;
        ESP_LOGI(TAG, "Enter first operand:");
        break;

    case WAITING_FOR_FIRST_OPERAND:
        ESP_LOGI(TAG, "Accepted first operand: %d", first_operand);
        calc_state = WAITING_FOR_OPERATION;
        ESP_LOGI(TAG, "Choose operation:");
        break;

    case WAITING_FOR_OPERATION:
        ESP_LOGI(TAG, "Accepted operation: %s", OP_SYMBOL[operation]);
        calc_state = WAITING_FOR_SECOND_OPERAND;
        ESP_LOGI(TAG, "Enter second operand:");
        break;

    case WAITING_FOR_SECOND_OPERAND:
        calculate_and_print();
        calc_state = IDLE;
        ESP_LOGI(TAG, "Press button to start!");
        break;
    }
}

/* ---------------- Main ---------------- */

void app_main(void)
{
    setup_encoder_pcnt();
    setup_button();

    int last_raw = 0;
    int acc      = 0;

    int stable_button_level = 1;
    int debounce_cnt = 0;

    ESP_LOGI(TAG, "Press button to start!");

    while (1) {
        // ---------- Button ----------
        int level = gpio_get_level(ENCODER_GPIO_SW);

        if (level == stable_button_level) {
            debounce_cnt = 0;
        } else if (++debounce_cnt >= DEBOUNCE_COUNT) {
            stable_button_level = level;
            debounce_cnt = 0;

            if (stable_button_level == 0) {
                on_button_press();
            }
        }

        // ---------- Encoder ----------
        int raw = 0;
        pcnt_unit_get_count(pcnt_unit, &raw);
        acc     += raw - last_raw;
        last_raw = raw;

        int steps = acc / COUNTS_PER_STEP;
        if (steps != 0) {
            acc -= steps * COUNTS_PER_STEP;
            on_encoder_steps(steps);

        } else if (acc == 0 && abs(raw) > RECENTER_AT) {
            pcnt_unit_clear_count(pcnt_unit);
            last_raw = 0;
        }

        vTaskDelay(pdMS_TO_TICKS(LOOP_MS));
    }
}