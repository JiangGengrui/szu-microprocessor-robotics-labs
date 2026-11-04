#ifndef __BEEP_H
#define __BEEP_H

#include "stm32f4xx_hal.h"

/* 蜂鸣器经 PCF8574T 的 P0 控制（I2C2：PH4/PH5）
 * 7 位地址 0x20；P0 输出低=鸣叫；P1~P7 接网络复位等其他外设，必须保持 1 */
#define PCF8574_ADDR   (0x20 << 1)   /* HAL 需 8 位寻址格式 */
#define BEEP_ON_BYTE   0xFE          /* P0=0 鸣叫，其余位=1 */
#define BEEP_OFF_BYTE  0xFF         /* 全高：停止 */

uint8_t BEEP_Init(void);             /* 探测 PCF8574 并默认关闭，0=成功 */
void    BEEP_Beep(uint32_t ms);      /* 非阻塞鸣叫 ms 毫秒 */
void    BEEP_Task(void);             /* 主循环中周期调用：到时自动关 */

#endif
