#ifndef __ATIM_H
#define __ATIM_H

#include "main.h"

/* TIM8_CH1 (PC6) 指定数量 PWM 句柄 */
extern TIM_HandleTypeDef g_timx_npwm_chy_handle;

void atim_timx_npwm_chy_init(uint16_t arr, uint16_t psc);
void atim_timx_npwm_chy_set(uint32_t npwm);

#endif
