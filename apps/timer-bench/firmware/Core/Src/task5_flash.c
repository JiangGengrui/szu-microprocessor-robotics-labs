#include "tasks.h"
#include "oled.h"
#include "key.h"

/* 任务5: N次闪光与硬件急停
 * 要求: 按键选择1~9目标数; 高级定时器PWM+重复计数器RCR+单脉冲模式OPM,
 *       使LED准确闪烁N次后自动停止; 另一按键接BKIN, 按下立即进入安全状态,
 *       释放不自动恢复, 必须确认操作重新使能。
 * 硬件: 闪光LED=PA8(TIM1_CH1), 经1kΩ限流电阻, 高电平点亮;
 *       BKIN按键=PA6(TIM1_BKIN), 一端接引脚一端接GND(上拉, 按下=低, BreakPolarity=LOW);
 *       KEY0=PH3选N, WK_UP=PA0启动/故障确认。
 * 说明: 不用TIM8——其CH1~CH4(PC6~PC9)被OLED数据线占用, 故改用同为高级定时器的TIM1。
 * 时钟: TIM1在APB2, 定时器时钟180MHz; PSC=17999 -> 10kHz; ARR=4999 -> 2Hz闪光。
 * RCR=N-1原因: 边沿对齐PWM1向上计数, 每次溢出(一个PWM周期)产生一次更新事件计数,
 *              RCR=N-1时第N次溢出才产生UEV, OPM在UEV时停表 -> 恰好输出N个脉冲。
 * 退出: 运行中按 KEY1(PH2) 返回任务菜单。
 */

TIM_HandleTypeDef g_htim1;          /* 非static: stm32f4xx_it.c需引用 */
static volatile uint8_t g_bk_fault = 0;   /* BKIN故障标志(仅中断置位) */
static uint8_t g_fault_drawn = 0;         /* 故障画面是否已绘制(进入任务时复位) */

/* BKIN中断钩子(由main.c的HAL_TIMEx_BreakCallback分发) */
void task5_on_break(void)
{
    g_bk_fault = 1;
}

/* TIM1初始化: CH1=PA8 PWM 2Hz, BKIN=PA6 低电平触发, 禁止自动恢复 */
static void task5_tim1_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_OC_InitTypeDef oc = {0};
    TIM_BreakDeadTimeConfigTypeDef sBreakConfig = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM1_CLK_ENABLE();

    g_htim1.Instance               = TIM1;
    g_htim1.Init.Prescaler         = 17999;              /* 180MHz/18000 = 10kHz */
    g_htim1.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_htim1.Init.Period            = 4999;               /* 10kHz/5000 = 2Hz */
    g_htim1.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    g_htim1.Init.RepetitionCounter = 0;
    g_htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&g_htim1);

    /* PA8=TIM1_CH1(AF1), PA6=TIM1_BKIN(AF1) */
    gpio_init_struct.Pin       = GPIO_PIN_8 | GPIO_PIN_6;
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull      = GPIO_PULLUP;           /* BKIN上拉: 悬空=高=无故障 */
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    oc.OCMode       = TIM_OCMODE_PWM1;
    oc.Pulse        = 2500;                             /* 50%占空比 */
    oc.OCPolarity   = TIM_OCPOLARITY_HIGH;              /* 高电平点亮 */
    oc.OCNPolarity  = TIM_OCNPOLARITY_HIGH;
    oc.OCFastMode   = TIM_OCFAST_DISABLE;
    oc.OCIdleState  = TIM_OCIDLESTATE_RESET;            /* 空闲=低 -> LED灭 */
    oc.OCNIdleState = TIM_OCNIDLESTATE_RESET;
    HAL_TIM_PWM_ConfigChannel(&g_htim1, &oc, TIM_CHANNEL_1);

    /* BKIN: 低电平触发, 禁止自动恢复(AOE=0 -> 故障清除后MOE保持0) */
    sBreakConfig.OffStateRunMode  = TIM_OSSR_DISABLE;
    sBreakConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
    sBreakConfig.LockLevel        = TIM_LOCKLEVEL_OFF;
    sBreakConfig.DeadTime         = 0;
    sBreakConfig.BreakState       = TIM_BREAK_ENABLE;
    sBreakConfig.BreakPolarity    = TIM_BREAKPOLARITY_LOW;
    sBreakConfig.AutomaticOutput  = TIM_AUTOMATICOUTPUT_DISABLE;
    HAL_TIMEx_ConfigBreakDeadTime(&g_htim1, &sBreakConfig);

    __HAL_TIM_ENABLE_IT(&g_htim1, TIM_IT_BREAK);        /* 使能刹车中断 */
    HAL_NVIC_SetPriority(TIM1_BRK_TIM9_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(TIM1_BRK_TIM9_IRQn);

    HAL_TIM_PWM_Start(&g_htim1, TIM_CHANNEL_1);         /* 置MOE, 但不启动计数 */
    TIM1->CR1 &= ~TIM_CR1_CEN;
}

/* 启动N次闪光: RCR=N-1 + OPM, N次溢出后硬件自动停 */
static void task5_start(uint8_t n)
{
    TIM1->CR1 &= ~TIM_CR1_CEN;
    TIM1->CNT  = 0;
    TIM1->RCR  = n - 1;

    TIM1->CR1 |=  TIM_CR1_URS;      /* URS=1: 只有计数溢出才产生更新事件 */
    TIM1->EGR  =  TIM_EGR_UG;       /* 软件更新: 装载RCR/PSC/ARR到影子寄存器 */
    TIM1->CR1 &= ~TIM_CR1_URS;

    TIM1->SR    = 0;                /* 清所有标志 */
    __HAL_TIM_MOE_ENABLE(&g_htim1); /* 确保MOE=1 */
    TIM1->CR1  |=  TIM_CR1_OPM;     /* 单脉冲模式: UEV时硬件自动清CEN */
    TIM1->CR1  |=  TIM_CR1_CEN;     /* 启动 */
}

void task5_run(void)
{
    uint8_t n = 3;                      /* 默认闪3次 */
    uint8_t key;
    uint8_t running = 0;                /* 闪光进行中 */
    uint8_t need_refresh = 1;

    g_bk_fault    = 0;                  /* 从菜单重新进入时清掉上一次遗留的故障状态 */
    g_fault_drawn = 0;

    oled_init();
    key_init();
    task5_tim1_init();

    while (1)
    {
        key = key_scan(0);

        if (key == KEY1)                        /* 退出到任务菜单 */
        {
            TIM1->CR1 &= ~TIM_CR1_CEN;          /* 停表 */
            __HAL_TIM_MOE_DISABLE(&g_htim1);    /* 关输出 -> LED灭(空闲电平为低) */
            HAL_NVIC_DisableIRQ(TIM1_BRK_TIM9_IRQn);
            return;
        }

        /* BKIN故障处理: 必须WK_UP确认才重新使能, 释放BKIN不自动恢复 */
        if (g_bk_fault)
        {
            running = 0;
            TIM1->CR1 &= ~TIM_CR1_CEN;              /* 强制停表, 回到干净状态 */
            if (!g_fault_drawn)
            {
                oled_clear();
                oled_show_string(0, 0,  (uint8_t *)"BKIN FAULT!", 16);
                oled_show_string(0, 20, (uint8_t *)"OUTPUT: SAFE", 16);
                oled_show_string(0, 40, (uint8_t *)"WKUP: RE-ARM", 16);
                oled_refresh_gram();
                g_fault_drawn = 1;
            }
            if (key == WK_UP)
            {
                g_bk_fault = 0;
                g_fault_drawn = 0;
                TIM1->SR = ~TIM_SR_BIF;             /* 清刹车标志 */
                __HAL_TIM_MOE_ENABLE(&g_htim1);     /* 人工确认后重新使能输出 */
                need_refresh = 1;
            }
            continue;
        }

        if (key == KEY0 && !running)
        {
            n++;
            if (n > 9) n = 1;
            need_refresh = 1;
        }
        if (key == WK_UP && !running)
        {
            task5_start(n);
            running = 1;
            need_refresh = 1;
        }

        /* 闪光完成检测: OPM停表(CEN=0) */
        if (running && (TIM1->CR1 & TIM_CR1_CEN) == 0)
        {
            running = 0;
            need_refresh = 1;
        }

        if (need_refresh)
        {
            oled_clear();
            oled_show_string(0, 0,  (uint8_t *)"N FLASH + BKIN", 16);
            oled_show_string(0, 20, (uint8_t *)"N=   KEY0:+1", 16);
            oled_show_int(24, 20, n, 16);
            oled_show_string(0, 40, (uint8_t *)"WKUP:START ", 16);
            oled_show_string(88, 40, (uint8_t *)(running ? "RUN" : "END"), 16);
            oled_refresh_gram();
            need_refresh = 0;
        }
    }
}
