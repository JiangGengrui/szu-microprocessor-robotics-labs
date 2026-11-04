#include "tasks.h"
#include "oled.h"
#include "key.h"
#include <stdio.h>      /* sprintf */

/* 任务4: 频率与占空比自测仪
 * 要求: 一个通用定时器产生PWM, 杜邦线接另一个通用定时器的输入捕获通道;
 *       至少测100Hz/25%、1kHz/50%、10kHz/75%三组信号;
 *       OLED显示设定值、测量值及相对误差; 断开跳线后在规定时间显示"无信号"。
 * 硬件: 产生端 TIM3_CH1=PA6(推挽输出) --杜邦线--> 捕获端 TIM4_CH1=PB6(输入, 非推挽);
 *       KEY0=PH3 切换三组测试信号。
 * 时钟: TIM3/TIM4均在APB1, 定时器时钟90MHz; PSC=89 -> 1MHz(1us)计数基准。
 * 捕获原理: TIM4 PWM输入模式——CH1直连(上升沿)测周期入CCR1, CH2间接(下降沿)测高电平入CCR2,
 *           从模式RESET使TI1FP1上升沿复位计数器。freq=1e6/(CCR1+1), duty=(CCR2+1)/(CCR1+1)。
 * 退出: 运行中按 KEY1(PH2) 返回任务菜单。
 */

TIM_HandleTypeDef g_htim3;          /* 非static: stm32f4xx_it.c需引用 */
TIM_HandleTypeDef g_htim4;

static volatile uint32_t g_cap_period = 0;   /* 捕获周期计数值(us) */
static volatile uint32_t g_cap_high   = 0;   /* 捕获高电平计数值(us) */
static volatile uint8_t  g_cap_flag   = 0;   /* 新捕获标志 */
static volatile uint32_t g_cap_tick   = 0;   /* 最后一次捕获的HAL tick */

/* 三组测试信号: {ARR, CCR, 设定频率Hz, 设定占空比%} */
typedef struct { uint16_t arr; uint16_t ccr; uint16_t freq; uint8_t duty; } test_group_t;
static const test_group_t g_groups[3] = {
    { 9999, 2500, 100,   25 },   /* 100Hz/25%  (1MHz/10000) */
    { 999,  500,  1000,  50 },   /* 1kHz/50%   (1MHz/1000)  */
    { 99,   75,   10000, 75 },   /* 10kHz/75%  (1MHz/100)   */
};

/* TIM3_CH1 PWM产生端初始化: PA6, PSC=89(1MHz), 参数按组重装 */
static void task4_gen_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    g_htim3.Instance               = TIM3;
    g_htim3.Init.Prescaler         = 89;
    g_htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_htim3.Init.Period            = g_groups[0].arr;
    g_htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    g_htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_PWM_Init(&g_htim3);

    gpio_init_struct.Pin       = GPIO_PIN_6;            /* PA6=TIM3_CH1 */
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;       /* 推挽复用输出 */
    gpio_init_struct.Pull      = GPIO_NOPULL;
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    oc.OCMode     = TIM_OCMODE_PWM1;
    oc.Pulse      = g_groups[0].ccr;
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&g_htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&g_htim3, TIM_CHANNEL_1);
}

/* 切换测试组: 重装ARR/CCR并软件更新 */
static void task4_gen_set(uint8_t idx)
{
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->ARR  = g_groups[idx].arr;
    TIM3->CCR1 = g_groups[idx].ccr;
    TIM3->CNT  = 0;
    TIM3->EGR  = TIM_EGR_UG;              /* 立即装载到影子寄存器 */
    TIM3->CR1 |= TIM_CR1_CEN;
}

/* TIM4 PWM输入捕获初始化: PB6=CH1, 从模式RESET */
static void task4_cap_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_IC_InitTypeDef sConfigIC = {0};
    TIM_SlaveConfigTypeDef sSlaveConfig = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    g_htim4.Instance               = TIM4;
    g_htim4.Init.Prescaler         = 89;              /* 1MHz(1us)捕获基准 */
    g_htim4.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_htim4.Init.Period            = 0xFFFF;
    g_htim4.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    g_htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_IC_Init(&g_htim4);

    gpio_init_struct.Pin       = GPIO_PIN_6;          /* PB6=TIM4_CH1 */
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;     /* 复用(输入方向由外设控制) */
    gpio_init_struct.Pull      = GPIO_PULLUP;
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &gpio_init_struct);

    /* CH1直连上升沿: 测周期 -> CCR1 */
    sConfigIC.ICPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
    sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
    sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
    sConfigIC.ICFilter    = 0;
    HAL_TIM_IC_ConfigChannel(&g_htim4, &sConfigIC, TIM_CHANNEL_1);

    /* CH2间接下降沿: 测高电平 -> CCR2 */
    sConfigIC.ICPolarity  = TIM_INPUTCHANNELPOLARITY_FALLING;
    sConfigIC.ICSelection = TIM_ICSELECTION_INDIRECTTI;
    HAL_TIM_IC_ConfigChannel(&g_htim4, &sConfigIC, TIM_CHANNEL_2);

    /* 从模式: TI1FP1上升沿复位计数器, 使CCR1=整周期 */
    sSlaveConfig.SlaveMode        = TIM_SLAVEMODE_RESET;
    sSlaveConfig.InputTrigger     = TIM_TS_TI1FP1;
    sSlaveConfig.TriggerPolarity  = TIM_INPUTCHANNELPOLARITY_RISING;
    sSlaveConfig.TriggerPrescaler = TIM_ICPSC_DIV1;
    sSlaveConfig.TriggerFilter    = 0;
    HAL_TIM_SlaveConfigSynchro(&g_htim4, &sSlaveConfig);

    HAL_NVIC_SetPriority(TIM4_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(TIM4_IRQn);

    HAL_TIM_IC_Start(&g_htim4, TIM_CHANNEL_2);        /* CCR2不需要中断 */
    HAL_TIM_IC_Start_IT(&g_htim4, TIM_CHANNEL_1);     /* CCR1中断取数 */
}

/* 捕获中断钩子(由main.c的HAL_TIM_IC_CaptureCallback分发) */
void task4_on_capture(TIM_HandleTypeDef *htim)
{
    if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
    {
        g_cap_period = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1) + 1;
        g_cap_high   = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2) + 1;
        g_cap_flag   = 1;
        g_cap_tick   = HAL_GetTick();
    }
}

void task4_run(void)
{
    uint8_t  group = 0;
    uint8_t  key;
    uint32_t mea_freq = 0, mea_duty = 0;
    uint32_t ferr_x10 = 0, derr = 0;   /* 频率误差(0.1%单位), 占空比误差(百分点) */
    uint8_t  no_signal = 0;            /* 1 = 当前正在显示"无信号" */

    oled_init();
    key_init();
    task4_gen_init();
    task4_cap_init();
    g_cap_tick = HAL_GetTick();         /* 起算时刻: 1s内没有捕获就报"无信号" */

    oled_clear();
    oled_show_string(0, 0, (uint8_t *)"PWM METER", 16);
    oled_refresh_gram();

    while (1)
    {
        char body[32];              /* 待显示内容 */
        char line[24];              /* 定宽16字符(8x16字体 = 128px 满行) + 结束符 */

        key = key_scan(0);

        if (key == KEY1)            /* 退出到任务菜单: 停产生端与捕获端 */
        {
            HAL_TIM_PWM_Stop(&g_htim3, TIM_CHANNEL_1);
            HAL_TIM_IC_Stop_IT(&g_htim4, TIM_CHANNEL_1);
            HAL_TIM_IC_Stop(&g_htim4, TIM_CHANNEL_2);
            HAL_NVIC_DisableIRQ(TIM4_IRQn);
            return;
        }

        if (key == KEY0)
        {
            group = (group + 1) % 3;
            task4_gen_set(group);
            g_cap_flag = 0;                 /* 清旧捕获, 等新一轮 */
            g_cap_tick = HAL_GetTick();     /* 重新计时: 1s内没有新捕获就报"无信号" */
            no_signal  = 0;                 /* 允许"无信号"提示重新出现 */

            /* 立即刷新设定行, 并清空测量/误差两行,
             * 否则会残留上一组的旧数据(要求4: 不得继续显示旧数据) */
            sprintf(body, "SET:%uHz %u%%", g_groups[group].freq, g_groups[group].duty);
            sprintf(line, "%-16.16s", body);
            oled_show_string(0, 16, (uint8_t *)line, 16);
            oled_show_string(0, 32, (uint8_t *)"                ", 16);
            oled_show_string(0, 48, (uint8_t *)"                ", 16);
            oled_refresh_gram();
        }

        /* 每100ms刷新一次显示 */
        if (g_cap_flag)
        {
            g_cap_flag = 0;
            no_signal = 0;
            mea_freq = 1000000UL / g_cap_period;
            mea_duty = g_cap_high * 100 / g_cap_period;
            /* 相对误差(0.1%单位) */
            ferr_x10 = (mea_freq > g_groups[group].freq)
                       ? (mea_freq - g_groups[group].freq) * 1000 / g_groups[group].freq
                       : (g_groups[group].freq - mea_freq) * 1000 / g_groups[group].freq;
            derr = (mea_duty > g_groups[group].duty)
                   ? (mea_duty - g_groups[group].duty)
                   : (g_groups[group].duty - mea_duty);

            /* 三行统一按16字符定宽写入(右侧空格补满整行128px), 字符串变短时
             * 才能把上一次多余的字形覆盖掉, 不会留下残影 */
            sprintf(body, "SET:%uHz %u%%", g_groups[group].freq, g_groups[group].duty);
            sprintf(line, "%-16.16s", body);
            oled_show_string(0, 16, (uint8_t *)line, 16);

            sprintf(body, "MEA:%luHz %lu%%", mea_freq, mea_duty);
            sprintf(line, "%-16.16s", body);
            oled_show_string(0, 32, (uint8_t *)line, 16);

            sprintf(body, "ERR:%lu.%lu%% D:%lu%%", ferr_x10 / 10, ferr_x10 % 10, derr);
            sprintf(line, "%-16.16s", body);
            oled_show_string(0, 48, (uint8_t *)line, 16);

            oled_refresh_gram();
        }
        else if (HAL_GetTick() - g_cap_tick > 1000 && !no_signal)
        {
            /* 超过1s无新捕获 -> 跳线断开 */
            no_signal = 1;
            oled_show_string(0, 32, (uint8_t *)"                ", 16);
            oled_show_string(0, 48, (uint8_t *)"NO SIGNAL       ", 16);
            oled_refresh_gram();
        }

        HAL_Delay(100);
    }
}
