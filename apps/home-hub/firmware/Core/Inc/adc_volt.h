#ifndef __ADC_VOLT_H
#define __ADC_VOLT_H
#include "main.h"

void     adc_volt_init(void);
uint16_t adc_volt_raw_avg(uint8_t times);   /* 平均后的 12 位原始值 */
uint32_t adc_volt_mv(uint16_t raw);         /* 换算成 mV */
#endif
