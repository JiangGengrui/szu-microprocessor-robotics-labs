#ifndef __LED_H
#define __LED_H

#include "stm32f4xx_hal.h"

void LED_Write(uint8_t pattern);       /* 8 位灯状态：bit0~bit7 对应 LED0~LED7，1=亮 */
void BOARD_LED0_Set(uint8_t on);       /* 板载 LED0：1=亮 0=灭（运行心跳指示） */
void BOARD_LED1_Set(uint8_t on);       /* 板载 LED1：1=亮 0=灭（错误提示） */

#endif
