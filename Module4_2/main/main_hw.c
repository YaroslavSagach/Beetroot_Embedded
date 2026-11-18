#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_random.h"

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

// Сітка «Матриці»: клітинка 8x8 пікселів -> 16 колонок x 8 рядків
#define CELL            8
#define COLS            (OLED_W / CELL)
#define ROWS            (OLED_H / CELL)
#define FRAME_MS        70

static uint8_t fb[OLED_W * OLED_PAGES];   // кадровий буфер
static i2c_master_dev_handle_t oled;

// ---------------------------------------------------------------------------
// Шрифт 5x7: кожен символ = 5 байтів-стовпчиків, біт0 = верхній піксель.
// Цифри 0-9 + кілька «інопланетних» символів у дусі катакани.
// ---------------------------------------------------------------------------
static const uint8_t glyphs[][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E},  // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00},  // 1
    {0x42, 0x61, 0x51, 0x49, 0x46},  // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31},  // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10},  // 4
    {0x27, 0x45, 0x45, 0x45, 0x39},  // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30},  // 6
    {0x01, 0x71, 0x09, 0x05, 0x03},  // 7
    {0x36, 0x49, 0x49, 0x49, 0x36},  // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E},  // 9
    {0x44, 0x28, 0x10, 0x28, 0x44},  // X
    {0x08, 0x7F, 0x08, 0x14, 0x22},
    {0x02, 0x7E, 0x12, 0x12, 0x22},
    {0x20, 0x7F, 0x24, 0x28, 0x20},
    {0x7C, 0x04, 0x04, 0x08, 0x70},
    {0x41, 0x7F, 0x41, 0x01, 0x02},
    {0x08, 0x08, 0x2A, 0x08, 0x08},
    {0x7E, 0x09, 0x09, 0x09, 0x7E},
};
#define N_GLYPHS (sizeof(glyphs) / sizeof(glyphs[0]))

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
        0xA6,             // нормальний режим
        0xAF,             // дисплей увімкнено
    };
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

// Малює символ у клітинці (col,row). inverted=true -> світла плашка з темним символом
static void draw_glyph(int col, int row, uint8_t idx, bool inverted)
{
    int x0 = col * CELL + 1;
    int y0 = row * CELL;

    if (inverted) {
        for (int y = 0; y < CELL; y++)
            for (int x = -1; x < 6; x++)
                px(x0 + x, y0 + y, true);
    }
    for (int gx = 0; gx < 5; gx++) {
        uint8_t bits = glyphs[idx][gx];
        for (int gy = 0; gy < 7; gy++) {
            if (bits & (1 << gy)) px(x0 + gx, y0 + gy, !inverted);
        }
    }
}

// ---------------------------------------------------------------------------
// Matrix rain
// ---------------------------------------------------------------------------
typedef struct {
    int     head;       // рядок голови потоку (може бути <0 або >ROWS)
    uint8_t len;        // довжина «хвоста»
    uint8_t period;     // скільки кадрів на один крок вниз (менше = швидше)
    uint8_t tick;
} stream_t;

static stream_t streams[COLS];
static uint8_t  cells[COLS][ROWS];       // який символ у клітинці

static inline uint32_t rnd(uint32_t n) { return esp_random() % n; }

static void stream_reset(stream_t *s, bool first)
{
    s->head   = first ? -(int)rnd(ROWS + 4) : -(int)rnd(6);
    s->len    = 3 + rnd(5);
    s->period = 1 + rnd(3);
    s->tick   = 0;
}

static void matrix_init(void)
{
    for (int c = 0; c < COLS; c++) {
        stream_reset(&streams[c], true);
        for (int r = 0; r < ROWS; r++) cells[c][r] = rnd(N_GLYPHS);
    }
}

static void matrix_step(void)
{
    memset(fb, 0, sizeof(fb));

    for (int c = 0; c < COLS; c++) {
        stream_t *s = &streams[c];

        // рух вниз
        if (++s->tick >= s->period) {
            s->tick = 0;
            s->head++;
            if (s->head >= 0 && s->head < ROWS) {
                cells[c][s->head] = rnd(N_GLYPHS);   // нова літера під головою
            }
            if (s->head - s->len > ROWS) {
                stream_reset(s, false);
            }
        }

        // малювання потоку
        for (int r = 0; r < ROWS; r++) {
            int d = s->head - r;                     // відстань від голови
            if (d < 0 || d >= s->len) continue;

            // мерехтіння: символи випадково змінюються
            if (d == 0 || rnd(100) < 15) cells[c][r] = rnd(N_GLYPHS);

            // «згасання» хвоста: чим далі від голови, тим частіше клітинка пропадає
            if (d > 1 && rnd(s->len) < (uint32_t)(d - 1)) continue;

            draw_glyph(c, r, cells[c][r], d == 0);   // голова — яскрава
        }
    }
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
    ESP_LOGI(TAG, "OLED ініціалізовано. Wake up, Neo...");

    matrix_init();
    while (1) {
        matrix_step();
        oled_flush();
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}
