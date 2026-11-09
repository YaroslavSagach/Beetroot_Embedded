#pragma once

#define SERVO_ANGLE_CLOSED  0
#define SERVO_ANGLE_OPEN    90

void servo_init(void);               // налаштовує ШІМ і ставить серво в закрите положення
void servo_set_angle(int angle);     // 0..180
void servo_open(void);
void servo_close(void);