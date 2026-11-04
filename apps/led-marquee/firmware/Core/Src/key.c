#include "key.h"
#include "gpio.h"

/* 10ms 周期采样 + 边沿检测：
 * 采样周期本身构成消抖窗口，只有"上一次稳定为松开、本次为按下"才上报一次，
 * 因此长按只触发一次事件（满足蜂鸣器不连续发声的要求） */
uint8_t KEY_Scan(void)
{
    uint8_t now[4];                    /* 当前键值：1=按下 */
    static uint8_t last[4] = {0};      /* 上一次稳定状态 */
    uint8_t i, event = KEY_NONE;

    now[0] = (HAL_GPIO_ReadPin(KEY0_GPIO_Port,   KEY0_Pin)   == GPIO_PIN_RESET); /* 按下=低 */
    now[1] = (HAL_GPIO_ReadPin(KEY1_GPIO_Port,   KEY1_Pin)   == GPIO_PIN_RESET);
    now[2] = (HAL_GPIO_ReadPin(KEY2_GPIO_Port,   KEY2_Pin)   == GPIO_PIN_RESET);
    now[3] = (HAL_GPIO_ReadPin(WK_UP_GPIO_Port, WK_UP_Pin)   == GPIO_PIN_SET);  /* 按下=高 */

    for (i = 0; i < 4; i++) {
        if (now[i] && !last[i])        /* 松开 -> 按下 的边沿 */
            event = i + 1;
        last[i] = now[i];
    }
    return event;
}
