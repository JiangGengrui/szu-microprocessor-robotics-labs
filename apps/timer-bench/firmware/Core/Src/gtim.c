#include "gtim.h"

TIM_HandleTypeDef g_timx_pwm_chy_handle;   /* PWM 句柄, 定义在全局 */

/* TIM3_CH4 PWM 初始化: arr=500-1, psc=90-1 → 2kHz, 极性低(低电平点亮) */
void gtim_timx_pwm_chy_init(uint16_t arr, uint16_t psc)
{
    TIM_OC_InitTypeDef timx_oc_pwm_chy = {0};

    g_timx_pwm_chy_handle.Instance               = TIM3;
    g_timx_pwm_chy_handle.Init.Prescaler         = psc;
    g_timx_pwm_chy_handle.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_timx_pwm_chy_handle.Init.Period            = arr;
    g_timx_pwm_chy_handle.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    HAL_TIM_PWM_Init(&g_timx_pwm_chy_handle);    /* 触发 PWM MspInit */

    timx_oc_pwm_chy.OCMode     = TIM_OCMODE_PWM1;   /* PWM 模式1 */
    timx_oc_pwm_chy.Pulse      = arr / 2;            /* 初始占空比 */
    timx_oc_pwm_chy.OCPolarity = TIM_OCPOLARITY_LOW; /* 极性低(亮) */
    HAL_TIM_PWM_ConfigChannel(&g_timx_pwm_chy_handle,
                              &timx_oc_pwm_chy, TIM_CHANNEL_4);
    HAL_TIM_PWM_Start(&g_timx_pwm_chy_handle, TIM_CHANNEL_4);
}

/* TIM3_CH4 引脚(复用 AF2)/时钟的底层配置, 由 HAL_TIM_PWM_Init 回调
 * 注意: 本函数是全局 weak 覆盖。后续子应用3/4复用TIM3的其他通道(CH1~CH3),
 * GPIO由各任务模块自行配置, 这里只保留时钟使能, 不再动PB1(避免影响板载LED0) */
void HAL_TIM_PWM_MspInit(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM3)
    {
        __HAL_RCC_TIM3_CLK_ENABLE();
    }
}
