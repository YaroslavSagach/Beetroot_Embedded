#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "bme280.h"

static const char *TAG = "APP";

/* ===================== Налаштування I2C та пристроїв ===================== */
#define I2C_PORT        I2C_NUM_0
#define PIN_SDA         GPIO_NUM_6
#define PIN_SCL         GPIO_NUM_7
#define I2C_FREQ_HZ     100000          /* DS1307 максимум 100 кГц */

#define PIN_BTN         GPIO_NUM_0
#define BTN_POLL_MS     20
#define BTN_LOCKOUT_MS  100             /* антидребезг */

#define OLED_ADDR       0x3C
#define OLED_W          128
#define OLED_H          64

#define BME_ADDR_LOW    0x76            /* SDO -> GND */
#define BME_ADDR_HIGH   0x77            /* SDO -> VDD */
#define I2C_TIMEOUT_MS  100

#define UPDATE_PERIOD_MS 1000           /* оновлення раз на секунду */
#define DEG             "\xC2\xB0"      /* знак ° у UTF-8 */

/* Кольори: білий = пікселі світяться. Щоб інвертувати екран — поміняйте місцями */
#define UI_FG           lv_color_white()
#define UI_BG           lv_color_black()

/* Діапазони шкал */
#define T_MIN           (-10)
#define T_MAX           40
#define P_MIN           950
#define P_MAX           1050

/* Геометрія аналогової сторінки: 3 колонки по 42 px, підписи внизу */
#define COL_W           42
#define LBL_Y           48
#define DIAL_CX         63
#define DIAL_CY         23

/* lv_line у LVGL 9 використовує інший тип точки, ніж у LVGL 8 */
#if LVGL_VERSION_MAJOR >= 9
typedef lv_point_precise_t gpoint_t;
#else
typedef lv_point_t gpoint_t;
#endif

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_bme_i2c;
static struct bme280_dev       s_bme;
static bool                    s_bme_ok;

/* ------------------------------ Сторінки UI ------------------------------ */
enum { PAGE_TEXT = 0, PAGE_ANALOG, PAGE_COUNT };
static int       s_page;
static lv_obj_t *s_pg[PAGE_COUNT];

/* Текстова сторінка */
static lv_obj_t *s_lbl_prev, *s_lbl_t, *s_lbl_rh, *s_lbl_p;

/* Аналогова сторінка */
static lv_obj_t *s_bar_t, *s_bar_h;                 /* термометр, вологість */
static lv_obj_t *s_needle;                          /* стрілка барометра */
static lv_obj_t *s_a_lbl_t, *s_a_lbl_p, *s_a_lbl_h; /* числа під віджетами */
static gpoint_t  s_needle_pts[2];
static gpoint_t  s_tick_pts[21][2];

/* ====================== Callback-и для драйвера Bosch ==================== */
static BME280_INTF_RET_TYPE bme_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                         uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)intf_ptr;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg_addr, 1, reg_data, len,
                                                I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? BME280_INTF_RET_SUCCESS : -1;
}

static BME280_INTF_RET_TYPE bme_i2c_write(uint8_t reg_addr, const uint8_t *reg_data,
                                          uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)intf_ptr;
    uint8_t buf[32];
    if (len > sizeof(buf) - 1) return -1;
    buf[0] = reg_addr;
    memcpy(&buf[1], reg_data, len);
    esp_err_t err = i2c_master_transmit(dev, buf, len + 1, I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? BME280_INTF_RET_SUCCESS : -1;
}

static void bme_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    esp_rom_delay_us(period);
}

/* ============================ Допоміжні для UI =========================== */
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Порожній контейнер-сторінка на весь екран */
static lv_obj_t *page_create(lv_obj_t *scr)
{
    lv_obj_t *o = lv_obj_create(scr);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, OLED_W, OLED_H);
    lv_obj_set_pos(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/* Прямокутник/коло: filled = заливка, інакше лише рамка 1 px */
static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h, bool filled, int radius)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_border_color(o, UI_FG, 0);
    lv_obj_set_style_border_width(o, filled ? 0 : 1, 0);
    lv_obj_set_style_bg_color(o, UI_FG, 0);
    lv_obj_set_style_bg_opa(o, filled ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    return o;
}

/* Вертикальна шкала (lv_bar): рамка + заливка всередині з відступом 2 px */
static lv_obj_t *vbar(lv_obj_t *p, int x, int y, int w, int h, int mn, int mx, int radius)
{
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(b, UI_FG, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, radius, 0);
    lv_obj_set_style_pad_all(b, 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, UI_FG, LV_PART_INDICATOR);
    lv_obj_set_style_radius(b, 0, LV_PART_INDICATOR);
    lv_bar_set_range(b, mn, mx);
    lv_bar_set_value(b, mn, LV_ANIM_OFF);
    return b;
}

/* Риски шкали. y0/ih — верх і висота внутрішньої області бара.
 * right=true: риски ідуть вправо від x; false: вліво (закінчуються на x). */
static void ticks(lv_obj_t *p, int x, bool right, int y0, int ih,
                  int vmin, int vmax, int step, int major, int w_long, int w_short)
{
    for (int v = vmin; v <= vmax; v += step) {
        int y = y0 + (ih - 1) * (vmax - v) / (vmax - vmin);
        int w = (v % major == 0) ? w_long : w_short;
        rect(p, right ? x : x - w, y, w, 1, true, 0);
    }
}

/* Лінія по двох точках (риска циферблата / стрілка) */
static lv_obj_t *line_new(lv_obj_t *p, gpoint_t *pts, int width)
{
    lv_obj_t *l = lv_line_create(p);
    lv_obj_remove_style_all(l);
    lv_obj_set_size(l, OLED_W, OLED_H);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, UI_FG, 0);
    lv_line_set_points(l, pts, 2);
    return l;
}

/* Точка на колі циферблата: frac 0..1 -> кут 135°..405° (270° за годинниковою) */
static void polar(int r, float frac, gpoint_t *out)
{
    float a = (135.0f + 270.0f * frac) * (float)M_PI / 180.0f;
    out->x = DIAL_CX + (int)lroundf(r * cosf(a));
    out->y = DIAL_CY + (int)lroundf(r * sinf(a));
}

static lv_obj_t *lbl_new(lv_obj_t *p, int x, int y, int w, bool center)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, UI_FG, 0);
    if (w > 0) lv_obj_set_width(l, w);
    if (center) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, "");
    return l;
}

/* ============================ Побудова сторінок ========================== */
static void build_text(lv_obj_t *pg)
{
    lv_obj_t **lbls[4] = { &s_lbl_prev, &s_lbl_t, &s_lbl_rh, &s_lbl_p };
    for (int i = 0; i < 4; i++) {
        *lbls[i] = lbl_new(pg, 0, i * 16, 0, false);
    }
}

static void build_analog(lv_obj_t *pg)
{
    /* --- 1. Термометр: трубка + колба + шкала праворуч --- */
    s_bar_t = vbar(pg, 10, 2, 8, 36, T_MIN, T_MAX, 3);
    rect(pg, 7, 34, 14, 14, true, LV_RADIUS_CIRCLE);                 /* колба */
    ticks(pg, 20, true, 4, 32, T_MIN, T_MAX, 5, 10, 7, 4);

    /* --- 2. Барометр: коло, 21 риска, стрілка, втулка --- */
    rect(pg, DIAL_CX - 21, DIAL_CY - 21, 42, 42, false, LV_RADIUS_CIRCLE);
    for (int i = 0; i <= 20; i++) {
        float frac = i / 20.0f;
        bool major = (i % 5 == 0);                 /* 950, 975, 1000, 1025, 1050 */
        polar(19, frac, &s_tick_pts[i][0]);
        polar(major ? 13 : 16, frac, &s_tick_pts[i][1]);
        line_new(pg, s_tick_pts[i], 1);
    }
    s_needle_pts[0].x = DIAL_CX;
    s_needle_pts[0].y = DIAL_CY;
    polar(12, 0.0f, &s_needle_pts[1]);
    s_needle = line_new(pg, s_needle_pts, 2);
    rect(pg, DIAL_CX - 2, DIAL_CY - 2, 5, 5, true, LV_RADIUS_CIRCLE);

    /* --- 3. Вологість: широка шкала + риски кожні 10 % ліворуч --- */
    s_bar_h = vbar(pg, 98, 2, 16, 42, 0, 100, 2);
    ticks(pg, 95, false, 4, 38, 0, 100, 10, 50, 8, 4);

    /* Числа під віджетами */
    s_a_lbl_t = lbl_new(pg, 0 * COL_W, LBL_Y, COL_W, true);
    s_a_lbl_p = lbl_new(pg, 1 * COL_W, LBL_Y, COL_W, true);
    s_a_lbl_h = lbl_new(pg, 2 * COL_W, LBL_Y, COL_W, true);
}

static void page_show(int p)
{
    s_page = p;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (i == p) lv_obj_clear_flag(s_pg[i], LV_OBJ_FLAG_HIDDEN);
        else        lv_obj_add_flag(s_pg[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ============================== Ініціалізація ============================ */
static esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus");

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &io), TAG, "panel io");

    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = OLED_H };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,
        .vendor_config = &ssd_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel), TAG, "ssd1306");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "on");

    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "lvgl init");

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
    ESP_RETURN_ON_FALSE(disp, ESP_FAIL, TAG, "add disp");

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_set_style_bg_color(scr, UI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    s_pg[PAGE_TEXT]   = page_create(scr);
    s_pg[PAGE_ANALOG] = page_create(scr);
    build_text(s_pg[PAGE_TEXT]);
    build_analog(s_pg[PAGE_ANALOG]);
    page_show(PAGE_TEXT);
    lvgl_port_unlock();
    return ESP_OK;
}

static esp_err_t bme_init(void)
{
    uint8_t addr = BME_ADDR_LOW;
    if (i2c_master_probe(s_bus, BME_ADDR_LOW, 50) != ESP_OK) {
        addr = BME_ADDR_HIGH;
    }
    ESP_LOGI(TAG, "BME280 адреса: 0x%02X", addr);

    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(s_bus, &cfg, &s_bme_i2c), TAG, "bme dev");

    s_bme.intf     = BME280_I2C_INTF;
    s_bme.read     = bme_i2c_read;
    s_bme.write    = bme_i2c_write;
    s_bme.delay_us = bme_delay_us;
    s_bme.intf_ptr = s_bme_i2c;

    if (bme280_init(&s_bme) != BME280_OK) {
        ESP_LOGE(TAG, "bme280_init: помилка (перевірте адресу/підключення)");
        return ESP_FAIL;
    }

    struct bme280_settings st;
    bme280_get_sensor_settings(&st, &s_bme);
    st.osr_h = BME280_OVERSAMPLING_1X;
    st.osr_p = BME280_OVERSAMPLING_1X;
    st.osr_t = BME280_OVERSAMPLING_1X;
    st.filter = BME280_FILTER_COEFF_OFF;
    st.standby_time = BME280_STANDBY_TIME_0_5_MS;
    if (bme280_set_sensor_settings(BME280_SEL_ALL_SETTINGS, &st, &s_bme) != BME280_OK ||
        bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &s_bme) != BME280_OK) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

/* ================== Періодичне оновлення (таймер LVGL, 1 с) ============== */
static void update_cb(lv_timer_t *timer)
{
    (void)timer;
    char l_prev[24], l_t[24], l_rh[24], l_p[24];

    snprintf(l_prev, sizeof(l_prev), "Uptime: %lus",
             (unsigned long)(esp_timer_get_time() / 1000000));

    struct bme280_data d;
    bool ok = s_bme_ok && bme280_get_sensor_data(BME280_ALL, &d, &s_bme) == BME280_OK;

    float t = T_MIN, h = 0, p = P_MIN;      /* при помилці — шкали в мінімум */
    if (ok) {
        t = (float)d.temperature;
        h = (float)d.humidity;
        p = (float)(d.pressure / 100.0);    /* Па -> hPa */
        snprintf(l_t,  sizeof(l_t),  "T: %.1f" DEG "C", t);
        snprintf(l_rh, sizeof(l_rh), "RH: %.0f%%", h);
        snprintf(l_p,  sizeof(l_p),  "P: %.0f hPa", p);
    } else {
        snprintf(l_t,  sizeof(l_t),  "T: Err");
        snprintf(l_rh, sizeof(l_rh), "RH: Err");
        snprintf(l_p,  sizeof(l_p),  "P: Err");
    }

    if (s_page == PAGE_TEXT) {
        lv_label_set_text(s_lbl_prev, l_prev);
        lv_label_set_text(s_lbl_t,    l_t);
        lv_label_set_text(s_lbl_rh,   l_rh);
        lv_label_set_text(s_lbl_p,    l_p);
    } else {
        /* Аналоговий режим: шкали + короткі числа (без одиниць, щоб влізло в 42 px) */
        lv_bar_set_value(s_bar_t, (int)lroundf(clampf(t, T_MIN, T_MAX)), LV_ANIM_OFF);
        lv_bar_set_value(s_bar_h, (int)lroundf(clampf(h, 0, 100)), LV_ANIM_OFF);

        float frac = (clampf(p, P_MIN, P_MAX) - P_MIN) / (float)(P_MAX - P_MIN);
        polar(12, frac, &s_needle_pts[1]);
        lv_line_set_points(s_needle, s_needle_pts, 2);

        if (ok) {
            char a_t[16], a_p[16], a_h[16];
            snprintf(a_t, sizeof(a_t), "%.1f" DEG, t);
            snprintf(a_p, sizeof(a_p), "%.0f", p);
            snprintf(a_h, sizeof(a_h), "%.0f%%", h);
            lv_label_set_text(s_a_lbl_t, a_t);
            lv_label_set_text(s_a_lbl_p, a_p);
            lv_label_set_text(s_a_lbl_h, a_h);
        } else {
            lv_label_set_text(s_a_lbl_t, "Err");
            lv_label_set_text(s_a_lbl_p, "Err");
            lv_label_set_text(s_a_lbl_h, "Err");
        }
    }

    ESP_LOGI(TAG, "%s | %s | %s", l_t, l_rh, l_p);
}

/* ====================== Кнопка BOOT (опитування в LVGL) ================== */
/* Викликається з задачі LVGL кожні 20 мс — mutex уже взято */
static void btn_cb(lv_timer_t *timer)
{
    (void)timer;
    static int prev = 1;
    static uint32_t last_press;
    int cur = gpio_get_level(PIN_BTN);

    if (prev == 1 && cur == 0 && lv_tick_elaps(last_press) > BTN_LOCKOUT_MS) {
        last_press = lv_tick_get();
        page_show((s_page + 1) % PAGE_COUNT);
        update_cb(NULL);                    /* одразу перемалювати нову сторінку */
    }
    prev = cur;
}

/* ================================ main =================================== */
void app_main(void)
{
    ESP_ERROR_CHECK(display_init());

    s_bme_ok = (bme_init() == ESP_OK);
    if (!s_bme_ok) {
        ESP_LOGE(TAG, "BME280 недоступний, на екрані буде Err");
    }

    /* Кнопка BOOT: вхід із підтяжкою (на платі вона й так є) */
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << PIN_BTN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    lvgl_port_lock(0);
    lv_timer_t *t = lv_timer_create(update_cb, UPDATE_PERIOD_MS, NULL);
    lv_timer_ready(t);
    lv_timer_create(btn_cb, BTN_POLL_MS, NULL);
    lvgl_port_unlock();
}