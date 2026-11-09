#pragma once

typedef enum {
    BUZZER_STOP = 0,
    BUZZER_MELODY_SUCCESS,
    BUZZER_MELODY_WRONG,
    BUZZER_MELODY_ALARM
} buzzer_cmd_t;

void buzzer_init(void);
void buzzer_play(buzzer_cmd_t melody);
void buzzer_stop(void);