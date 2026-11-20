#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

static const char *TAG = "clock";

/* ---------- Конфігурація ---------- */
#define I2C_PORT        I2C_NUM_0
#define PIN_SDA         GPIO_NUM_6
#define PIN_SCL         GPIO_NUM_7
#define I2C_FREQ_HZ     100000          /* DS1307 максимум 100 кГц */

#define PIN_BOOT        GPIO_NUM_0
#define PIN_MODE_BTN    GPIO_NUM_17     /* зовнішня кнопка між піном і GND -> зміна режиму */
#define BTN_POLL_MS     20
#define BTN_DEBOUNCE_MS 40

#define OLED_ADDR       0x3C
#define OLED_W          128
#define OLED_H          64

#define DS1307_ADDR     0x68
#define DS1307_REG_SEC  0x00

/* Аналоговий годинник */
#define CLK_CX          (OLED_W / 2)
#define CLK_CY          (OLED_H / 2)
#define CLK_R           31              /* радіус циферблата */
#define LEN_HOUR        14
#define LEN_MIN         22
#define LEN_SEC         26

/* ---------- Режими ---------- */
typedef enum {
    MODE_TIME = 0,      /* 1) тільки час */
    MODE_ANALOG,        /* 2) аналоговий годинник */
    MODE_DATE,          /* 3) тільки дата */
    MODE_TIME_DATE,     /* 4) час + дата (стандарт) */
    MODE_COUNT
} clock_mode_t;

/* ---------- Глобальні дескриптори ---------- */
static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_rtc;
static esp_lcd_panel_handle_t s_panel;

static clock_mode_t s_mode = MODE_TIME_DATE;
static bool s_inverted = false;             /* тема light/dark, не залежить від режиму */

static struct tm s_tm;                      /* останній відомий час (під lvgl_port_lock) */
static bool s_have_time = false;

static lv_obj_t *s_lbl_time;                /* HH:MM:SS (режими 1 і 4) */
static lv_obj_t *s_lbl_date;                /* Sat 14.02.2026 (режим 4) */
static lv_obj_t *s_lbl_dbig;                /* 14.02 (режим 3) */
static lv_obj_t *s_lbl_dsub;                /* Sat 2026 (режим 3) */
static lv_obj_t *s_analog;                  /* контейнер аналогового годинника (режим 2) */
static lv_obj_t *s_hand_h, *s_hand_m, *s_hand_s;
static lv_point_t s_pts_h[2], s_pts_m[2], s_pts_s[2];
static lv_point_t s_pts_tick[12][2];

/* ---------- BCD ---------- */
static inline uint8_t bcd2bin(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static inline uint8_t bin2bcd(uint8_t v) { return ((v / 10) << 4) | (v % 10); }

/* ====================================================================
 *  I2C шина
 * ==================================================================== */
static void i2c_bus_init(void)
{
    i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&cfg, &s_bus));
}

/* ====================================================================
 *  DS1307
 * ==================================================================== */
static esp_err_t rtc_write_time(const struct tm *t)
{
    uint8_t buf[8] = {
        DS1307_REG_SEC,
        bin2bcd(t->tm_sec) & 0x7F,              /* CH = 0 -> годинник іде */
        bin2bcd(t->tm_min),
        bin2bcd(t->tm_hour),                    /* 24-годинний режим */
        bin2bcd(t->tm_wday + 1),                /* 1..7 */
        bin2bcd(t->tm_mday),
        bin2bcd(t->tm_mon + 1),
        bin2bcd(t->tm_year - 100),              /* 2000..2099 */
    };
    return i2c_master_transmit(s_rtc, buf, sizeof(buf), 100);
}

/* Зчитування часу. Повертає ESP_ERR_INVALID_STATE, якщо годинник зупинений (CH=1) */
static esp_err_t rtc_read_time(struct tm *t)
{
    uint8_t reg = DS1307_REG_SEC;
    uint8_t d[7];

    esp_err_t err = i2c_master_transmit(s_rtc, &reg, 1, 200);
    if (err != ESP_OK) return err;

    err = i2c_master_receive(s_rtc, d, sizeof(d), 200);
    if (err != ESP_OK) return err;

    if (d[0] & 0x80) return ESP_ERR_INVALID_STATE;

    memset(t, 0, sizeof(*t));
    t->tm_sec  = bcd2bin(d[0] & 0x7F);
    t->tm_min  = bcd2bin(d[1] & 0x7F);
    t->tm_hour = bcd2bin(d[2] & 0x3F);
    t->tm_mday = bcd2bin(d[4] & 0x3F);
    t->tm_mon  = bcd2bin(d[5] & 0x1F) - 1;
    t->tm_year = bcd2bin(d[6]) + 100;

    /* День тижня обчислюємо самі (регістр DS1307 може бути невірним) */
    struct tm tmp = *t;
    tmp.tm_isdst = -1;
    mktime(&tmp);
    t->tm_wday = tmp.tm_wday;
    t->tm_yday = tmp.tm_yday;
    return ESP_OK;
}

/* Встановлення часу компіляції (використовується, якщо годинник зупинений) */
static void rtc_set_from_build_time(void)
{
    static const char *months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = {0};
    int d, y, hh, mm, ss;
    sscanf(__DATE__, "%3s %d %d", mon, &d, &y);
    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    struct tm t = {
        .tm_sec = ss, .tm_min = mm, .tm_hour = hh,
        .tm_mday = d, .tm_mon = (strstr(months, mon) - months) / 3,
        .tm_year = y - 1900, .tm_isdst = -1,
    };
    mktime(&t);                                  /* заповнює tm_wday */
    ESP_ERROR_CHECK(rtc_write_time(&t));
    ESP_LOGW(TAG, "RTC був зупинений -> встановлено час збірки: %s %s", __DATE__, __TIME__);
}

static void rtc_init(void)
{
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS1307_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(s_bus, &dev, &s_rtc));

    struct tm t;
    esp_err_t err = rtc_read_time(&t);
    if (err == ESP_ERR_INVALID_STATE) {
        rtc_set_from_build_time();
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "DS1307 не відповідає: %s", esp_err_to_name(err));
    }
}

/* ====================================================================
 *  Допоміжні функції інтерфейсу
 * ==================================================================== */
static void set_visible(lv_obj_t *obj, bool visible)
{
    if (visible) lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
}

static lv_obj_t *make_label(lv_obj_t *parent, const lv_font_t *font, const char *txt)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_white(), 0);
    lv_label_set_text(l, txt);
    return l;
}

static lv_obj_t *make_line(lv_obj_t *parent, lv_point_t *pts, int width)
{
    lv_obj_t *ln = lv_line_create(parent);
    lv_obj_set_size(ln, OLED_W, OLED_H);        /* фіксований розмір -> без артефактів при русі стрілок */
    lv_obj_set_pos(ln, 0, 0);
    lv_obj_set_style_line_color(ln, lv_color_white(), 0);
    lv_obj_set_style_line_width(ln, width, 0);
    lv_obj_set_style_line_rounded(ln, false, 0);
    lv_line_set_points(ln, pts, 2);
    return ln;
}

static void set_hand(lv_obj_t *line, lv_point_t pts[2], float deg, int len)
{
    float rad = deg * (float)M_PI / 180.0f;
    pts[0].x = CLK_CX;
    pts[0].y = CLK_CY;
    pts[1].x = CLK_CX + (lv_coord_t)lroundf(len * sinf(rad));
    pts[1].y = CLK_CY - (lv_coord_t)lroundf(len * cosf(rad));
    lv_line_set_points(line, pts, 2);
}

/* Створення аналогового годинника: коло, 12 рисок, 3 стрілки, центральна точка */
static void analog_create(lv_obj_t *scr)
{
    s_analog = lv_obj_create(scr);
    lv_obj_remove_style_all(s_analog);
    lv_obj_set_size(s_analog, OLED_W, OLED_H);
    lv_obj_set_pos(s_analog, 0, 0);
    lv_obj_clear_flag(s_analog, LV_OBJ_FLAG_SCROLLABLE);

    /* Циферблат */
    lv_obj_t *face = lv_obj_create(s_analog);
    lv_obj_remove_style_all(face);
    lv_obj_set_size(face, CLK_R * 2, CLK_R * 2);
    lv_obj_set_pos(face, CLK_CX - CLK_R, CLK_CY - CLK_R);
    lv_obj_set_style_radius(face, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(face, 1, 0);
    lv_obj_set_style_border_color(face, lv_color_white(), 0);
    lv_obj_set_style_border_opa(face, LV_OPA_COVER, 0);

    /* Риски годин (більші на 12/3/6/9) */
    for (int i = 0; i < 12; i++) {
        float rad = i * 30.0f * (float)M_PI / 180.0f;
        int r_in = (i % 3 == 0) ? CLK_R - 7 : CLK_R - 4;
        int r_out = CLK_R - 2;
        s_pts_tick[i][0].x = CLK_CX + (lv_coord_t)lroundf(r_in  * sinf(rad));
        s_pts_tick[i][0].y = CLK_CY - (lv_coord_t)lroundf(r_in  * cosf(rad));
        s_pts_tick[i][1].x = CLK_CX + (lv_coord_t)lroundf(r_out * sinf(rad));
        s_pts_tick[i][1].y = CLK_CY - (lv_coord_t)lroundf(r_out * cosf(rad));
        make_line(s_analog, s_pts_tick[i], (i % 3 == 0) ? 2 : 1);
    }

    /* Стрілки: години - товста, хвилини - середня, секунди - тонка */
    s_hand_h = make_line(s_analog, s_pts_h, 3);
    s_hand_m = make_line(s_analog, s_pts_m, 2);
    s_hand_s = make_line(s_analog, s_pts_s, 1);
    set_hand(s_hand_h, s_pts_h, 0, LEN_HOUR);
    set_hand(s_hand_m, s_pts_m, 0, LEN_MIN);
    set_hand(s_hand_s, s_pts_s, 0, LEN_SEC);

    /* Центральна точка */
    lv_obj_t *dot = lv_obj_create(s_analog);
    lv_obj_remove_style_all(dot);
    lv_obj_set_size(dot, 5, 5);
    lv_obj_set_pos(dot, CLK_CX - 2, CLK_CY - 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
}

/* ====================================================================
 *  Малювання поточного режиму (викликати під lvgl_port_lock)
 * ==================================================================== */
static void render_locked(const struct tm *t)
{
    char buf[24];

    switch (s_mode) {
    case MODE_TIME:
        strftime(buf, sizeof(buf), "%H:%M:%S", t);
        lv_label_set_text(s_lbl_time, buf);
        break;

    case MODE_ANALOG:
        set_hand(s_hand_h, s_pts_h, ((t->tm_hour % 12) + t->tm_min / 60.0f) * 30.0f, LEN_HOUR);
        set_hand(s_hand_m, s_pts_m, (t->tm_min + t->tm_sec / 60.0f) * 6.0f, LEN_MIN);
        set_hand(s_hand_s, s_pts_s, t->tm_sec * 6.0f, LEN_SEC);
        break;

    case MODE_DATE:
        strftime(buf, sizeof(buf), "%d.%m", t);
        lv_label_set_text(s_lbl_dbig, buf);
        strftime(buf, sizeof(buf), "%a %Y", t);
        lv_label_set_text(s_lbl_dsub, buf);
        break;

    case MODE_TIME_DATE:
    default:
        strftime(buf, sizeof(buf), "%H:%M:%S", t);
        lv_label_set_text(s_lbl_time, buf);
        strftime(buf, sizeof(buf), "%a %d.%m.%Y", t);
        lv_label_set_text(s_lbl_date, buf);
        break;
    }
}

/* Показати/сховати елементи та розкласти їх під поточний режим (під lvgl_port_lock) */
static void apply_mode_locked(void)
{
    set_visible(s_lbl_time, s_mode == MODE_TIME || s_mode == MODE_TIME_DATE);
    set_visible(s_lbl_date, s_mode == MODE_TIME_DATE);
    set_visible(s_lbl_dbig, s_mode == MODE_DATE);
    set_visible(s_lbl_dsub, s_mode == MODE_DATE);
    set_visible(s_analog,   s_mode == MODE_ANALOG);

    if (s_mode == MODE_TIME)      lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, 0);
    if (s_mode == MODE_TIME_DATE) lv_obj_align(s_lbl_time, LV_ALIGN_TOP_MID, 0, 8);

    if (s_have_time) render_locked(&s_tm);
}

/* ====================================================================
 *  Дисплей SSD1306 (esp_lcd + LVGL)
 * ==================================================================== */
static void display_init(void)
{
    /* 1. Панель I2C IO */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &io));

    /* 2. Драйвер SSD1306 */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = OLED_H };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = GPIO_NUM_NC,
        .vendor_config = &ssd_cfg,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    s_panel = panel;

    /* 3. LVGL */
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io,
        .panel_handle = panel,
        .buffer_size = OLED_W * OLED_H,
        .double_buffer = true,
        .hres = OLED_W,
        .vres = OLED_H,
        .monochrome = true,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
    };
    lv_disp_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    /* 4. Інтерфейс: усі елементи створюються один раз, режими лише ховають/показують */
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_lbl_time = make_label(scr, &lv_font_montserrat_24, "--:--:--");
    s_lbl_date = make_label(scr, &lv_font_montserrat_14, "--- --.--.----");
    lv_obj_align(s_lbl_date, LV_ALIGN_BOTTOM_MID, 0, -10);

    s_lbl_dbig = make_label(scr, &lv_font_montserrat_24, "--.--");
    lv_obj_align(s_lbl_dbig, LV_ALIGN_CENTER, 0, -8);
    s_lbl_dsub = make_label(scr, &lv_font_montserrat_14, "--- ----");
    lv_obj_align(s_lbl_dsub, LV_ALIGN_CENTER, 0, 16);

    analog_create(scr);

    apply_mode_locked();
    lvgl_port_unlock();
}

static void display_update(const struct tm *t)
{
    if (lvgl_port_lock(100)) {
        s_tm = *t;
        s_have_time = true;
        render_locked(&s_tm);
        lvgl_port_unlock();
    }
}

/* ====================================================================
 *  Кнопки
 *   BOOT     -> інверсія кольорів (апаратна, команда SSD1306 0xA6/0xA7)
 *   PIN 17   -> наступний режим: 1 -> 2 -> 3 -> 4 -> 1 ...
 * ==================================================================== */
static void display_set_inverted(bool inv)
{
    if (lvgl_port_lock(100)) {
        esp_lcd_panel_invert_color(s_panel, inv);
        lvgl_port_unlock();
    }
    s_inverted = inv;
    ESP_LOGI(TAG, "Інверсія: %s", inv ? "увімкнена" : "вимкнена");
}

static void display_next_mode(void)
{
    if (lvgl_port_lock(100)) {
        s_mode = (clock_mode_t)((s_mode + 1) % MODE_COUNT);
        apply_mode_locked();             /* інверсія не чіпається -> тема зберігається */
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Режим: %d", (int)s_mode + 1);
}

typedef struct {
    gpio_num_t pin;
    int stable;                          /* 1 = відпущена, 0 = натиснута */
    int counter;
} btn_t;

/* true один раз у момент натискання (з антидребезгом) */
static bool btn_pressed(btn_t *b)
{
    int level = gpio_get_level(b->pin);
    if (level != b->stable) {
        b->counter += BTN_POLL_MS;
        if (b->counter >= BTN_DEBOUNCE_MS) {
            b->stable = level;
            b->counter = 0;
            return level == 0;
        }
    } else {
        b->counter = 0;
    }
    return false;
}

static void button_task(void *arg)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << PIN_BOOT) | (1ULL << PIN_MODE_BTN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,       /* кнопки тягнуть пін до GND */
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    btn_t boot = { .pin = PIN_BOOT,     .stable = 1 };
    btn_t mode = { .pin = PIN_MODE_BTN, .stable = 1 };

    while (1) {
        if (btn_pressed(&boot)) display_set_inverted(!s_inverted);
        if (btn_pressed(&mode)) display_next_mode();
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }
}

static void i2c_scan(void)
{
    for (uint8_t a = 0x08; a < 0x78; a++) {
        if (i2c_master_probe(s_bus, a, 50) == ESP_OK) {
            ESP_LOGI(TAG, "Знайдено пристрій: 0x%02X", a);
        }
    }
}

/* ====================================================================
 *  main
 * ==================================================================== */
void app_main(void)
{
    i2c_bus_init();
    i2c_scan();
    rtc_init();
    display_init();
    xTaskCreate(button_task, "button", 3072, NULL, 5, NULL);

    struct tm now, last = { .tm_sec = -1 };
    while (1) {
        esp_err_t err = rtc_read_time(&now);
        if (err == ESP_OK) {
            if (now.tm_sec != last.tm_sec) {     /* оновлюємо рівно при зміні секунди */
                display_update(&now);
                last = now;
            }
        } else {
            ESP_LOGE(TAG, "Помилка читання RTC: %s", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(200));          /* опитування 5 разів/с -> ≥1 оновлення/с */
    }
}