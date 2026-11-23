#include <stdio.h>
#include <string.h>
#include <stdint.h>
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

#define OLED_ADDR       0x3C
#define OLED_W          128
#define OLED_H          64

#define BME_ADDR_LOW    0x76            /* SDO -> GND */
#define BME_ADDR_HIGH   0x77            /* SDO -> VDD */
#define I2C_TIMEOUT_MS  100

#define UPDATE_PERIOD_MS 1000           /* оновлення раз на секунду */
#define DEG             "\xC2\xB0"      /* знак ° у UTF-8 */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_bme_i2c;
static struct bme280_dev       s_bme;
static bool                    s_bme_ok;

/* Мітки LVGL: 0 — попередній вивід, далі T, RH, P */
static lv_obj_t *s_lbl_prev, *s_lbl_t, *s_lbl_rh, *s_lbl_p;

/* ====================== Callback-и для драйвера Bosch ==================== */
/* Читання len байтів, починаючи з регістра reg_addr */
static BME280_INTF_RET_TYPE bme_i2c_read(uint8_t reg_addr, uint8_t *reg_data,
                                         uint32_t len, void *intf_ptr)
{
    i2c_master_dev_handle_t dev = (i2c_master_dev_handle_t)intf_ptr;
    esp_err_t err = i2c_master_transmit_receive(dev, &reg_addr, 1, reg_data, len,
                                                I2C_TIMEOUT_MS);
    return (err == ESP_OK) ? BME280_INTF_RET_SUCCESS : -1;
}

/* Запис: адреса регістра + дані одним I2C-пакетом */
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

/* Затримка потрібна драйверу лише під час ініціалізації (кілька мс) */
static void bme_delay_us(uint32_t period, void *intf_ptr)
{
    (void)intf_ptr;
    esp_rom_delay_us(period);
}

/* ============================== Ініціалізація ============================ */
/* Ініціалізація шини I2C, дисплея (esp_lcd) та LVGL */
static esp_err_t display_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,           /* внутрішні підтяжки */
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "bus");

    /* Канал вводу/виводу esp_lcd поверх I2C */
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = OLED_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
        .control_phase_bytes = 1,       /* керуючий байт 0x00 / 0x40 */
        .dc_bit_offset = 6,             /* біт D/C у керуючому байті SSD1306 */
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(s_bus, &io_cfg, &io), TAG, "panel io");

    /* Драйвер контролера SSD1306 (штатний компонент esp_lcd) */
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_ssd1306_config_t ssd_cfg = { .height = OLED_H };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,           /* апаратного RESET немає */
        .vendor_config = &ssd_cfg,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_ssd1306(io, &panel_cfg, &panel), TAG, "ssd1306");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(panel, true), TAG, "on");

    /* LVGL: окрема задача, монохромний дисплей */
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

    /* Створюємо 4 мітки (шрифт 14 px => рядок 16 px, разом 64 px) */
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_disp_get_scr_act(disp);
    lv_obj_t **lbls[4] = { &s_lbl_prev, &s_lbl_t, &s_lbl_rh, &s_lbl_p };
    for (int i = 0; i < 4; i++) {
        *lbls[i] = lv_label_create(scr);
        lv_obj_set_style_text_font(*lbls[i], &lv_font_montserrat_14, 0);
        lv_obj_align(*lbls[i], LV_ALIGN_TOP_LEFT, 0, i * 16);
        lv_label_set_text(*lbls[i], "");
    }
    lvgl_port_unlock();
    return ESP_OK;
}

/* Ініціалізація BME280 через офіційний драйвер Bosch */
static esp_err_t bme_init(void)
{
    /* Автовизначення адреси: 0x76 або 0x77 */
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

    /* Підключаємо callback-и до драйвера Bosch */
    s_bme.intf     = BME280_I2C_INTF;
    s_bme.read     = bme_i2c_read;
    s_bme.write    = bme_i2c_write;
    s_bme.delay_us = bme_delay_us;
    s_bme.intf_ptr = s_bme_i2c;

    if (bme280_init(&s_bme) != BME280_OK) {
        ESP_LOGE(TAG, "bme280_init: помилка (перевірте адресу/підключення)");
        return ESP_FAIL;
    }

    /* Oversampling x1, нормальний режим, standby 0.5 мс, фільтр вимкнено */
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
/* Викликається з задачі LVGL; mutex уже взято, delay() не потрібен */
static void update_cb(lv_timer_t *timer)
{
    (void)timer;
    char l_prev[24], l_t[24], l_rh[24], l_p[24];

    /* Місце для попереднього ДЗ (наприклад, час із DS1307). Поки що — uptime */
    snprintf(l_prev, sizeof(l_prev), "Uptime: %lus",
             (unsigned long)(esp_timer_get_time() / 1000000));

    struct bme280_data d;
    if (s_bme_ok && bme280_get_sensor_data(BME280_ALL, &d, &s_bme) == BME280_OK) {
        /* Драйвер повертає °C, %RH та Па (double); тиск переводимо в hPa */
        snprintf(l_t,  sizeof(l_t),  "T: %.1f" DEG "C", d.temperature);
        snprintf(l_rh, sizeof(l_rh), "RH: %.0f%%", d.humidity);
        snprintf(l_p,  sizeof(l_p),  "P: %.0f hPa", d.pressure / 100.0);
    } else {
        snprintf(l_t,  sizeof(l_t),  "T: Err");
        snprintf(l_rh, sizeof(l_rh), "RH: Err");
        snprintf(l_p,  sizeof(l_p),  "P: Err");
    }

    lv_label_set_text(s_lbl_prev, l_prev);
    lv_label_set_text(s_lbl_t,    l_t);
    lv_label_set_text(s_lbl_rh,   l_rh);
    lv_label_set_text(s_lbl_p,    l_p);

    /* Дублюємо значення з екрана в лог */
    ESP_LOGI(TAG, "%s | %s | %s", l_t, l_rh, l_p);
}

/* ================================ main =================================== */
void app_main(void)
{
    ESP_ERROR_CHECK(display_init());

    s_bme_ok = (bme_init() == ESP_OK);
    if (!s_bme_ok) {
        ESP_LOGE(TAG, "BME280 недоступний, на екрані буде Err");
    }

    /* Таймер LVGL замість delay(): перший виклик одразу, далі раз на секунду */
    lvgl_port_lock(0);
    lv_timer_t *t = lv_timer_create(update_cb, UPDATE_PERIOD_MS, NULL);
    lv_timer_ready(t);
    lvgl_port_unlock();
}