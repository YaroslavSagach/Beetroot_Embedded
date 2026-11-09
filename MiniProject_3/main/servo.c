#include "servo.h"
#include "driver/ledc.h"

#define SERVO_GPIO      1
#define SERVO_TIMER     LEDC_TIMER_0
#define SERVO_CHANNEL   LEDC_CHANNEL_0
#define SERVO_MODE      LEDC_LOW_SPEED_MODE
#define SERVO_FREQ_HZ   50
#define SERVO_RES       LEDC_TIMER_14_BIT

// Ширина імпульсу для 0° і 180°. Якщо твоє серво не доходить до кінців, підкоригуй
#define SERVO_MIN_US    500
#define SERVO_MAX_US    2500
#define SERVO_PERIOD_US (1000000 / SERVO_FREQ_HZ)   // 20000 мкс

void servo_set_angle(int angle)
{
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;

    uint32_t pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / 180;
    uint32_t duty     = (pulse_us * (1u << SERVO_RES)) / SERVO_PERIOD_US;

    ledc_set_duty(SERVO_MODE, SERVO_CHANNEL, duty);
    ledc_update_duty(SERVO_MODE, SERVO_CHANNEL);
}

void servo_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode      = SERVO_MODE,
        .timer_num       = SERVO_TIMER,
        .duty_resolution = SERVO_RES,
        .freq_hz         = SERVO_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer);

    ledc_channel_config_t channel = {
        .gpio_num   = SERVO_GPIO,
        .speed_mode = SERVO_MODE,
        .channel    = SERVO_CHANNEL,
        .timer_sel  = SERVO_TIMER,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&channel);

    servo_close();
}

void servo_open(void)  { servo_set_angle(SERVO_ANGLE_OPEN); }
void servo_close(void) { servo_set_angle(SERVO_ANGLE_CLOSED); }