#ifndef __EFFECTS_H
#define __EFFECTS_H

#include "stm32f4xx_hal.h"

#define EFFECT_COUNT 4

void    EFFECT_Set(uint8_t id);   /* 切换特效 1~4，并复位步进 */
uint8_t EFFECT_Get(void);
void    EFFECT_Step(void);        /* 每个节拍调用：推进并输出 */

#endif
