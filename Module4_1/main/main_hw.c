#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_PORT      UART_NUM_1
#define UART_TX_PIN    17
#define UART_RX_PIN    18
#define UART_BAUD      9600
#define UART_BUF_SIZE  256

#define BUTTON_GPIO    GPIO_NUM_0
#define LED_GPIO       GPIO_NUM_6 // external LED
#define DEBOUNCE_US    50000

#define CMD_LED_ON     '1'
#define CMD_LED_OFF    '0'

static const char *TAG = "UART_LINK";

static volatile bool button_pressed = false;
static int64_t last_button_time = 0;

static void uart_link_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0));
    ESP_LOGI(TAG, "UART1 готовий: %d 8N1, TX=GPIO%d, RX=GPIO%d", UART_BAUD, UART_TX_PIN, UART_RX_PIN);
}

static void led_init(void)
{
    ESP_ERROR_CHECK(gpio_reset_pin(LED_GPIO));
    ESP_ERROR_CHECK(gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT));
    gpio_set_level(LED_GPIO, 0);
}

static void IRAM_ATTR button_isr_handler(void *arg)
{
    button_pressed = true;
}

static void button_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(BUTTON_GPIO, button_isr_handler, NULL));
}

void app_main(void)
{
    uart_link_init();
    led_init();
    button_init();

    bool led_state = false; // STM32 LED state

    while (1) {

        // ---- Button ESP32 -> LED STM32 ----
        if (button_pressed) {
            button_pressed = false;
            int64_t now = esp_timer_get_time();
            int level = gpio_get_level(BUTTON_GPIO);

            if (level == 0 && (now - last_button_time) > DEBOUNCE_US) {
                last_button_time = now;

                led_state = !led_state;
                uint8_t cmd = led_state ? CMD_LED_ON : CMD_LED_OFF;
                uart_write_bytes(UART_PORT, (const char *)&cmd, 1);

                ESP_LOGI(TAG, "Button pressed -> sent '%c' (LED STM: %s)",
                         cmd, led_state ? "ON" : "OFF");
            }
        }

        // ---- Signal from STM32 -> LED ESP32 ----
        uint8_t rx_byte;
        while (uart_read_bytes(UART_PORT, &rx_byte, 1, 0) > 0) {
            if (rx_byte == CMD_LED_ON) {
                gpio_set_level(LED_GPIO, 1);
                ESP_LOGI(TAG, "Got signal '1' -> LED ESP32 ON");
            } else if (rx_byte == CMD_LED_OFF) {
                gpio_set_level(LED_GPIO, 0);
                ESP_LOGI(TAG, "Got signal '0' -> LED ESP32 OFF");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}