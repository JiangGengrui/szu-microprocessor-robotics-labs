#include "atim.h"

TIM_HandleTypeDef g_timx_npwm_chy_handle;   /* TIM8 PWM 句柄 */

/* TIM8_CH1 PWM 初始化: PC6, PSC=8999, ARR=9999 -> 20kHz计数/2Hz */
void atim_timx_npwm_chy_init(uint16_t arr, uint16_t psc)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_OC_InitTypeDef timx_oc_npwm_chy = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_TIM8_CLK_ENABLE();

    g_timx_npwm_chy_handle.Instance               = TIM8;
    g_timx_npwm_chy_handle.Init.Prescaler         = psc;
    g_timx_npwm_chy_handle.Init.Period            = arr;
    g_timx_npwm_chy_handle.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_timx_npwm_chy_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    g_timx_npwm_chy_handle.Init.RepetitionCounter = 0;
    HAL_TIM_PWM_Init(&g_timx_npwm_chy_handle);

    /* PC6 复用 AF3 为 TIM8_CH1 */
    gpio_init_struct.Pin       = GPIO_PIN_6;
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull      = GPIO_PULLUP;
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF3_TIM8;
    HAL_GPIO_Init(GPIOC, &gpio_init_struct);

    timx_oc_npwm_chy.OCMode     = TIM_OCMODE_PWM1;
    timx_oc_npwm_chy.Pulse      = arr / 2;          /* 50% 占空比 */
    timx_oc_npwm_chy.OCPolarity = TIM_OCPOLARITY_LOW; /* 低电平有效: 停表后空闲=高 -> LED灭 */
    HAL_TIM_PWM_ConfigChannel(&g_timx_npwm_chy_handle, &timx_oc_npwm_chy, TIM_CHANNEL_1);

    /* 高级定时器必须 MOE=1 才能输出; HAL_TIM_PWM_Start 内部已置 MOE + CEN */
    HAL_TIM_PWM_Start(&g_timx_npwm_chy_handle, TIM_CHANNEL_1);
    TIM8->CR1 &= ~TIM_CR1_CEN;    /* 先停, 输出的时机交给 set */
}

/* 单脉冲模式 + RCR: 输出 npwm 个脉冲后硬件自动停止(无需中断) */
void atim_timx_npwm_chy_set(uint32_t npwm)
{
    if (npwm == 0) return;
    if (npwm > 256) npwm = 256;   /* RCR 为 8 位: 1~256 */

    TIM8->CR1 &= ~TIM_CR1_CEN;    /* 停表 */
    TIM8->CNT  = 0;               /* 计数清零 */
    TIM8->RCR  = npwm - 1;        /* RCR = N-1, 输出 N 个脉冲 */

    /* 软件更新装载 RCR 到影子寄存器(URS=1, 不产生中断) */
    TIM8->CR1 |=  TIM_CR1_URS;
    TIM8->EGR  =  TIM_EGR_UG;
    TIM8->CR1 &= ~TIM_CR1_URS;

    TIM8->CR1 |=  TIM_CR1_OPM;    /* 单脉冲模式: 更新事件到来时硬件自动清 CEN */
    TIM8->CR1 |=  TIM_CR1_CEN;    /* 启动, 溢出 N 次后硬件自动停 */
}
