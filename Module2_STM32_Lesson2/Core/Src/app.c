#include "app.h"
#include "main.h"
#include "usbd_cdc_if.h"

#include <stdio.h>
#include <stdint.h>

static volatile uint8_t button_flag = 0;
static char buf[64];

static int serial_print(char *ptr, int len)
{
    CDC_Transmit_FS((uint8_t *)ptr, (uint16_t)len);
    HAL_Delay(1);
    return len;
}

void setup(void)
{
}

void loop(void)
{
    if (button_flag == 1)
    {
        button_flag = 0;

        int len = snprintf(buf, sizeof(buf), "Button pushed.\r\n");
        serial_print(buf, len);

        HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
    }

    HAL_Delay(500);
}

void app_button_pressed(void)
{
    button_flag = 1;
}
