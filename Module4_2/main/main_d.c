#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "I2C_SCAN";

// Налаштування шини I2C
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_IO      GPIO_NUM_6      // Пін даних SDA
#define I2C_SCL_IO      GPIO_NUM_7      // Пін тактування SCL

// I2C-адреси відомих пристроїв
#define ADDR_OLED_1     0x3C            // OLED-дисплей GM009605 v4.3 (SSD1306)
#define ADDR_EEPROM     0x50            // Пам'ять AT24C32 на платі Tiny RTC
#define ADDR_DS1307     0x68            // Годинник DS1307 на платі Tiny RTC

// Параметри дисплея 128x64
#define OLED_W          128
#define OLED_H          64
#define OLED_PAGES      (OLED_H / 8)

static uint8_t fb[OLED_W * OLED_PAGES];   // кадровий буфер
static i2c_master_dev_handle_t oled;

// ---------------------------------------------------------------------------
// Низькорівневий драйвер SSD1306
// ---------------------------------------------------------------------------
static void oled_cmd(const uint8_t *cmds, size_t len)
{
    uint8_t buf[16];
    buf[0] = 0x00;                        // Co=0, D/C#=0 -> команди
    memcpy(&buf[1], cmds, len);
    ESP_ERROR_CHECK(i2c_master_transmit(oled, buf, len + 1, 100));
}

static void oled_init(void)
{
    static const uint8_t init_seq[] = {
        0xAE,             // дисплей вимкнено
        0xD5, 0x80,       // частота тактування
        0xA8, 0x3F,       // мультиплекс 64
        0xD3, 0x00,       // зсув дисплея
        0x40,             // початковий рядок 0
        0x8D, 0x14,       // увімкнути charge pump
        0x20, 0x02,       // режим адресації по сторінках
        0xA1,             // віддзеркалення по X
        0xC8,             // віддзеркалення по Y
        0xDA, 0x12,       // конфігурація COM-пінів
        0x81, 0xCF,       // контраст
        0xD9, 0xF1,       // pre-charge
        0xDB, 0x40,       // VCOMH
        0xA4,             // вивід із RAM
        0xA6,             // нормальний (не інверсний) режим
        0xAF,             // дисплей увімкнено
    };
    // Відправляємо команди по одній, щоб не вилізти за розмір буфера
    for (size_t i = 0; i < sizeof(init_seq);) {
        size_t n = 1;
        uint8_t c = init_seq[i];
        if (c == 0xD5 || c == 0xA8 || c == 0xD3 || c == 0x8D || c == 0x20 ||
            c == 0xDA || c == 0x81 || c == 0xD9 || c == 0xDB) {
            n = 2;
        }
        oled_cmd(&init_seq[i], n);
        i += n;
    }
}

static void oled_flush(void)
{
    static uint8_t line[1 + OLED_W];
    for (uint8_t page = 0; page < OLED_PAGES; page++) {
        uint8_t set_pos[] = { (uint8_t)(0xB0 | page), 0x00, 0x10 };
        oled_cmd(set_pos, sizeof(set_pos));
        line[0] = 0x40;                   // Co=0, D/C#=1 -> дані
        memcpy(&line[1], &fb[page * OLED_W], OLED_W);
        ESP_ERROR_CHECK(i2c_master_transmit(oled, line, sizeof(line), 100));
    }
}

// ---------------------------------------------------------------------------
// Примітиви малювання
// ---------------------------------------------------------------------------
static void px(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_W || y < 0 || y >= OLED_H) return;
    if (on) fb[x + (y / 8) * OLED_W] |=  (1 << (y % 8));
    else    fb[x + (y / 8) * OLED_W] &= ~(1 << (y % 8));
}

static void fill_circle(int cx, int cy, int r, bool on)
{
    for (int y = -r; y <= r; y++)
        for (int x = -r; x <= r; x++)
            if (x * x + y * y <= r * r) px(cx + x, cy + y, on);
}

static void draw_circle(int cx, int cy, int r)
{
    for (int a = 0; a < 360; a++) {
        float t = a * M_PI / 180.0f;
        px(cx + (int)roundf(r * cosf(t)), cy + (int)roundf(r * sinf(t)), true);
    }
}

// Серце: клас кривої, пікселі в межах формули (x²+y²-1)³ - x²y³ <= 0
static void draw_heart(int cx, int cy, int size, bool on)
{
    for (int y = -size; y <= size; y++) {
        for (int x = -size; x <= size; x++) {
            float fx = (float)x / size * 1.2f;
            float fy = -(float)y / size * 1.2f + 0.2f;
            float a = fx * fx + fy * fy - 1.0f;
            if (a * a * a - fx * fx * fy * fy * fy <= 0) px(cx + x, cy + y, on);
        }
    }
}

// ---------------------------------------------------------------------------
// Смайлик
// ---------------------------------------------------------------------------
static void draw_smiley(bool eyes_open, bool big_smile)
{
    memset(fb, 0, sizeof(fb));

    draw_circle(64, 32, 30);
    draw_circle(64, 32, 29);

    // Очі
    if (eyes_open) {
        fill_circle(52, 24, 4, true);
        fill_circle(76, 24, 4, true);
    } else {
        for (int x = -4; x <= 4; x++) {   // кліпання: «—   —»
            px(52 + x, 25, true);
            px(76 + x, 25, true);
        }
    }

    // Рот: дуга внизу
    int r = big_smile ? 20 : 15;
    for (int a = 25; a <= 155; a++) {
        float t = a * M_PI / 180.0f;
        int x = 64 + (int)roundf(r * cosf(t));
        int y = 34 + (int)roundf(r * sinf(t) * 0.8f);
        px(x, y, true);
        px(x, y + 1, true);
    }

    // Сердечка в кутках
    draw_heart(14, 12, 6, true);
    draw_heart(113, 12, 6, big_smile);
    draw_heart(14, 52, 6, big_smile);
    draw_heart(113, 52, 6, true);
}

// ---------------------------------------------------------------------------
// Скан шини
// ---------------------------------------------------------------------------
static const char *get_device_name(uint8_t addr)
{
    switch (addr) {
        case ADDR_OLED_1: return "OLED-дисплей GM009605 (SSD1306)";
        case ADDR_EEPROM: return "Пам'ять EEPROM AT24C32 (Tiny RTC)";
        case ADDR_DS1307: return "Годинник DS1307 (Tiny RTC)";
        default:          return NULL;
    }
}

void app_main(void)
{
    vTaskDelay(pdMS_TO_TICKS(500));

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
    bool oled_found = false;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus_handle, addr, 50) == ESP_OK) {
            found++;
            const char *name = get_device_name(addr);
            if (addr == ADDR_OLED_1) oled_found = true;

            if (name != NULL) ESP_LOGI(TAG, "Знайдено пристрій: 0x%02X — %s", addr, name);
            else              ESP_LOGI(TAG, "Знайдено невідомий пристрій: 0x%02X", addr);
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "Пристроїв на шині I2C не знайдено.");
    } else {
        ESP_LOGI(TAG, "Сканування завершено. Усього знайдено пристроїв: %d", found);
    }

    if (!oled_found) {
        ESP_LOGE(TAG, "OLED (0x%02X) не знайдено — малювати нема де.", ADDR_OLED_1);
        return;
    }

    // Додаємо OLED на шину
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = ADDR_OLED_1,
        .scl_speed_hz    = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_cfg, &oled));

    oled_init();
    ESP_LOGI(TAG, "OLED ініціалізовано, запускаю смайлик :)");

    // Анімація: усміхається, іноді кліпає
    int frame = 0;
    while (1) {
        bool blink     = (frame % 20) >= 18;     // кліпає раз на ~2 секунди
        bool big_smile = (frame / 10) % 2;       // рот змінює розмір
        draw_smiley(!blink, big_smile);
        oled_flush();
        frame++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}