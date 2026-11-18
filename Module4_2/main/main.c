#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "I2C_SCAN";

// Налаштування шини I2C
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_IO      GPIO_NUM_6      // Пін даних SDA- білий
#define I2C_SCL_IO      GPIO_NUM_7      // Пін тактування SCL

// I2C-адреси відомих пристроїв
#define ADDR_OLED_1     0x3C            // OLED-дисплей GM009605 v4.3 (основна адреса SSD1306)
#define ADDR_EEPROM     0x50            // Пам'ять AT24C32 на платі Tiny RTC
#define ADDR_DS1307     0x68            // Мікросхема годинника DS1307 на платі Tiny RTC

// Повертає назву пристрою за його I2C-адресою або NULL, якщо адреса невідома
static const char *get_device_name(uint8_t addr)
{
    switch (addr) {
        case ADDR_OLED_1:
            return "OLED-дисплей GM009605 (SSD1306)";
        case ADDR_EEPROM:
            return "Пам'ять EEPROM AT24C32 (Tiny RTC)";
        case ADDR_DS1307:
            return "Годинник DS1307 (Tiny RTC)";
        default:
            return NULL;
    }
}


void app_main(void)
{
 
    vTaskDelay(pdMS_TO_TICKS(500));

    // Конфігурація контролера шини I2C
    i2c_master_bus_config_t bus_cfg = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port   = I2C_PORT,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,   
    };

    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_cfg, &bus_handle));

    ESP_LOGI(TAG, "Сканування шини I2C (0x08..0x77) на SDA=GPIO%d, SCL=GPIO%d:", I2C_SDA_IO, I2C_SCL_IO);

    int found = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus_handle, addr, 50) == ESP_OK) {
            found++;
            const char *name = get_device_name(addr);

            if (name != NULL) {
                ESP_LOGI(TAG, "Знайдено пристрій: 0x%02X — %s", addr, name);
            } else {
                ESP_LOGI(TAG, "Знайдено невідомий пристрій: 0x%02X", addr);
            }
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "Пристроїв на шині I2C не знайдено.");
    } else {
        ESP_LOGI(TAG, "Сканування завершено. Усього знайдено пристроїв: %d", found);
    }
}