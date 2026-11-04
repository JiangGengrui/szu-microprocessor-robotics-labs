#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx_hal.h"

#define KEY_NONE   0
#define KEY0_DOWN  1
#define KEY1_DOWN  2
#define KEY2_DOWN  3
#define WKUP_DOWN  4

uint8_t KEY_Scan(void);   /* 每 10ms 调用一次，返回按下事件（仅沿触发一次） */

#endif
