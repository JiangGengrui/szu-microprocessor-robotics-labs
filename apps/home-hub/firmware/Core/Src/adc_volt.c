#include "adc_volt.h"

extern ADC_HandleTypeDef hadc1;    /* CubeMX 生成的句柄 */

void adc_volt_init(void)
{
    HAL_ADC_Start(&hadc1);
}

uint16_t adc_volt_raw_avg(uint8_t times)
{
    uint32_t sum = 0;
    for (uint8_t i = 0; i < times; i++)
    {
        HAL_ADC_Start(&hadc1);
        HAL_ADC_PollForConversion(&hadc1, 10);
        sum += HAL_ADC_GetValue(&hadc1);
    }
    return (uint16_t)(sum / times);
}

uint32_t adc_volt_mv(uint16_t raw)
{
    /* V(mV) = raw * VDD / 4095;  VDD 取 3300mV */
    return (uint32_t)(((uint64_t)raw * 3300U) / 4095U);
}
