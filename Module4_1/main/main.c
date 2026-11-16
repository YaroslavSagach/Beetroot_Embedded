#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define UART_PORT      UART_NUM_1
#define UART_TX_PIN    17
#define UART_RX_PIN    18
#define UART_BAUD      9600
#define UART_BUF_SIZE  256

static const char *TAG = "UART_LINK";

void uart_link_init(void)
{
    uart_config_t uart_config = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,     // 8 біт даних
        .parity     = UART_PARITY_DISABLE,  // "N" у 8N1 — без парності
        .stop_bits  = UART_STOP_BITS_1,     // "1" у 8N1 — один стоп-біт
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, UART_BUF_SIZE, UART_BUF_SIZE, 0, NULL, 0));
    ESP_LOGI(TAG, "UART1 готовий: %d 8N1 на GPIO%d(TX)/GPIO%d(RX)", UART_BAUD, UART_TX_PIN, UART_RX_PIN);
}

void app_main(void)
{
    uart_link_init();

    while (1) {
        uint8_t byte_to_send = 0x41;   // 'A' = 01000001 — той самий приклад, що й на слайдах
        uart_write_bytes(UART_PORT, (const char *)&byte_to_send, 1);
        ESP_LOGI(TAG, "Надіслано: 0x%02X ('%c')", byte_to_send, byte_to_send);

        uint8_t rx_byte;
        int len = uart_read_bytes(UART_PORT, &rx_byte, 1, pdMS_TO_TICKS(1000));
        if (len > 0) {
            ESP_LOGI(TAG, "Отримано: 0x%02X ('%c')", rx_byte, rx_byte);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
