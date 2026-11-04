#include "tasks.h"
#include "oled.h"
#include "key.h"
#include <stdlib.h>     /* rand/srand */

/* 任务2: 随机反应速度测试器
 * 要求: 基本定时器(TIM6)产生1ms时间基准; 开始后随机等待2~5s再点亮提示LED并开始计时;
 *       按键按下后OLED显示本次反应时间; 识别提前按键的"抢跑"; 保存最好成绩;
 *       定时器中断中只允许计数和设置事件标志。
 * 硬件: LED0=PB1(板载, 低电平点亮), KEY0=PH3(上拉, 按下=0)
 * 时钟: TIM6在APB1, 定时器时钟90MHz; PSC=89 -> 1MHz计数; ARR=999 -> 1ms更新中断
 * 退出: 运行中按 KEY1(PH2) 返回任务菜单。
 */

TIM_HandleTypeDef htim6;

static volatile uint32_t g_ms = 0;    /* 毫秒计数, 仅由中断累加 */

/* TIM6更新中断钩子: 只允许计数(由main.c的HAL_TIM_PeriodElapsedCallback分发) */
void task2_on_ms(void)
{
    g_ms++;
}

/* TIM6基本定时器初始化: 1ms更新中断 */
static void task2_tim6_init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();

    htim6.Instance               = TIM6;
    htim6.Init.Prescaler         = 89;          /* 90MHz/90 = 1MHz */
    htim6.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim6.Init.Period            = 999;         /* 1MHz/1000 = 1kHz -> 1ms */
    htim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&htim6);

    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    HAL_TIM_Base_Start_IT(&htim6);
}

/* LED0(PB1)初始化, 板载低电平点亮 */
static void task2_led_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio_init_struct.Pin   = GPIO_PIN_1;
    gpio_init_struct.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio_init_struct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);   /* 初始熄灭 */
}

#define LED0_ON()    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET)
#define LED0_OFF()   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET)

/* 状态机状态 */
typedef enum {
    S_IDLE = 0,     /* 空闲, 等待开始 */
    S_WAIT,         /* 随机等待2~5s(此间按键=抢跑) */
    S_TIMING,       /* LED已亮, 计时中, 等按键 */
    S_SHOW,         /* 显示本次成绩 */
} react_state_t;

void task2_run(void)
{
    react_state_t state = S_IDLE;
    uint32_t wait_target = 0;       /* 随机等待目标ms */
    uint32_t reaction = 0;          /* 本次反应时间ms */
    uint32_t best = 0xFFFFFFFF;     /* 最好成绩 */
    uint8_t  need_refresh = 1;
    uint8_t  key;

    oled_init();
    task2_led_init();
    key_init();
    task2_tim6_init();
    srand(HAL_GetTick());           /* 随机种子 */

    while (1)
    {
        key = key_scan(0);          /* 单击模式: 必须松手再按, 防止开始键按住误判抢跑 */

        if (key == KEY1)            /* 退出到任务菜单 */
        {
            LED0_OFF();
            HAL_TIM_Base_Stop_IT(&htim6);
            HAL_NVIC_DisableIRQ(TIM6_DAC_IRQn);
            return;
        }

        switch (state)
        {
        case S_IDLE:
            if (need_refresh)
            {
                oled_clear();
                oled_show_string(0, 0,  (uint8_t *)"REACTION TEST", 16);
                oled_show_string(0, 20, (uint8_t *)"KEY0: START", 16);
                if (best != 0xFFFFFFFF)
                {
                    oled_show_string(0, 40, (uint8_t *)"BEST:", 16);
                    oled_show_int(48, 40, (int32_t)best, 16);
                    oled_show_string(88, 40, (uint8_t *)"ms", 16);
                }
                oled_refresh_gram();
                need_refresh = 0;
            }
            if (key == KEY0)
            {
                wait_target = 2000 + (uint32_t)(rand() % 3001);   /* 随机2~5s */
                g_ms = 0;
                state = S_WAIT;
                need_refresh = 1;
            }
            break;

        case S_WAIT:
            if (need_refresh)
            {
                oled_clear();
                oled_show_string(0, 0, (uint8_t *)"WAIT FOR LED...", 16);
                oled_refresh_gram();
                need_refresh = 0;
            }
            if (key == KEY0)                            /* 抢跑! */
            {
                oled_clear();
                oled_show_string(0, 0, (uint8_t *)"TOO EARLY!", 16);
                oled_show_string(0, 20, (uint8_t *)"KEY0: RETRY", 16);
                oled_refresh_gram();
                LED0_OFF();
                state = S_IDLE;
                need_refresh = 1;
                HAL_Delay(1000);                        /* 提示停留1s */
            }
            else if (g_ms >= wait_target)               /* 随机时间到, 点灯计时 */
            {
                LED0_ON();
                g_ms = 0;
                state = S_TIMING;
            }
            break;

        case S_TIMING:
            if (key == KEY0)                            /* 按下, 记录反应时间 */
            {
                reaction = g_ms;
                LED0_OFF();
                if (reaction < best) best = reaction;   /* 更新最好成绩 */
                state = S_SHOW;
                need_refresh = 1;
            }
            break;

        case S_SHOW:
            if (need_refresh)
            {
                oled_clear();
                oled_show_string(0, 0,  (uint8_t *)"TIME:", 16);
                oled_show_int(48, 0, (int32_t)reaction, 16);
                oled_show_string(96, 0, (uint8_t *)"ms", 16);
                oled_show_string(0, 20, (uint8_t *)"BEST:", 16);
                oled_show_int(48, 20, (int32_t)best, 16);
                oled_show_string(96, 20, (uint8_t *)"ms", 16);
                oled_show_string(0, 40, (uint8_t *)"KEY0: AGAIN", 16);
                oled_refresh_gram();
                need_refresh = 0;
            }
            if (key == KEY0)
            {
                state = S_IDLE;
                need_refresh = 1;
            }
            break;
        }
    }
}
