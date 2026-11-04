#ifndef __GTIM_H
#define __GTIM_H

#include "main.h"

/* TIM3_CH4 PWM 句柄, 供主函数改占空比用 */
extern TIM_HandleTypeDef g_timx_pwm_chy_handle;

/* 初始化 TIM3_CH4 输出 PWM */
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc);

#endif
