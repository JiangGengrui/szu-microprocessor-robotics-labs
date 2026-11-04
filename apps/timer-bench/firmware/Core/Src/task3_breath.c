#include "tasks.h"
#include "oled.h"
#include "key.h"

/* 任务3: 三色流水呼吸灯
 * 要求: 通用定时器的三个PWM通道分别驱动R/G/B三个LED, PWM频率1kHz;
 *       周期性修改三个CCR使三路亮度按不同相位平滑变化;
 *       按键切换同步呼吸/三色流水/警灯闪烁至少三种模式。
 * 硬件: R=PA6(TIM3_CH1), G=PA7(TIM3_CH2), B=PB6(TIM4_CH1), 均AF2复用推挽;
 *       外接LED阳极经1kΩ限流电阻接引脚, 阴极接GND -> 高电平点亮, OCPolarity=HIGH;
 *       KEY0=PH3 切换模式。
 * 引脚选型说明: TIM3_CH3 在STM32F429上只有 PB0 与 PC8 两个可选脚——
 *       PB0 是板载LED1, 阿波罗V2开发板未把它引出到排针(无法接杜邦线);
 *       PC8 已被OLED数据线D2占用。
 *       因此蓝通道改用 TIM4_CH1 = PB6: 阿波罗V2官方《IO资源分配表》中 PB6 标注为
 *       "未连接任何设备, 可完全独立使用", 是板上唯一完全干净的引脚, 且带通用定时器
 *       通道复用。仍为硬件PWM, 频率与级数同TIM3两路完全一致(1kHz/1000级)。
 *       注: 任务4用PB6作TIM4_CH1输入捕获, 两者不会同时运行, 切换任务时需改接线。
 * 时钟: TIM3/TIM4 均在APB1, 定时器时钟90MHz; PSC=89 -> 1MHz; ARR=999 -> 1kHz PWM
 * 退出: 运行中按 KEY1(PH2) 返回任务菜单。
 */

static TIM_HandleTypeDef g_htim3;           /* R/G: TIM3_CH1/CH2 (PA6/PA7) */
static TIM_HandleTypeDef g_htim4b;          /* B  : TIM4_CH1   (PB6)      */

#define BREATH_ARR   999U

/* 三角波呼吸曲线: phase 0~999 -> 0~999~0 线性渐亮渐灭 */
static uint16_t breath_val(uint16_t phase)
{
    if (phase > BREATH_ARR) phase = BREATH_ARR;
    if (phase < 500) return phase * 2;                 /* 上升段 0~998 */
    return (BREATH_ARR - phase) * 2;                   /* 下降段 998~0 */
}

/* TIM3 两路硬件PWM初始化: CH1=PA6(红), CH2=PA7(绿), 1kHz */
static void task3_tim3_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();

    g_htim3.Instance               = TIM3;
    g_htim3.Init.Prescaler         = 89;               /* 90MHz/90 = 1MHz */
    g_htim3.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_htim3.Init.Period            = BREATH_ARR;       /* 1MHz/1000 = 1kHz */
    g_htim3.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    g_htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&g_htim3);

    /* PA6=CH1, PA7=CH2 (AF2_TIM3) */
    gpio_init_struct.Pin       = GPIO_PIN_6 | GPIO_PIN_7;
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull      = GPIO_NOPULL;
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);

    oc.OCMode     = TIM_OCMODE_PWM1;       /* CNT<CCR时输出有效电平 */
    oc.Pulse      = 0;                     /* 初始占空比0(灭) */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;   /* 高电平点亮外接LED */
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&g_htim3, &oc, TIM_CHANNEL_1);
    HAL_TIM_PWM_ConfigChannel(&g_htim3, &oc, TIM_CHANNEL_2);

    HAL_TIM_PWM_Start(&g_htim3, TIM_CHANNEL_1);
    HAL_TIM_PWM_Start(&g_htim3, TIM_CHANNEL_2);
}

/* 蓝通道(PB6)硬件PWM初始化: 通用定时器 TIM4_CH1, 与TIM3同为1kHz/1000级
 * 选PB6的理由见文件头"引脚选型说明": 阿波罗V2上PB6是唯一"未连接任何设备"的引脚,
 * 且带TIM4_CH1复用; TIM3_CH3的两个候选脚(PB0/PC8)都不可用。 */
static void task3_tim4b_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};
    TIM_OC_InitTypeDef oc = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM4_CLK_ENABLE();

    g_htim4b.Instance               = TIM4;
    g_htim4b.Init.Prescaler         = 89;               /* 90MHz/90 = 1MHz */
    g_htim4b.Init.CounterMode       = TIM_COUNTERMODE_UP;
    g_htim4b.Init.Period            = BREATH_ARR;       /* 1MHz/1000 = 1kHz */
    g_htim4b.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    g_htim4b.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
    HAL_TIM_PWM_Init(&g_htim4b);

    /* PB6 = TIM4_CH1 (AF2) */
    gpio_init_struct.Pin       = GPIO_PIN_6;
    gpio_init_struct.Mode      = GPIO_MODE_AF_PP;
    gpio_init_struct.Pull      = GPIO_NOPULL;
    gpio_init_struct.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio_init_struct.Alternate = GPIO_AF2_TIM4;
    HAL_GPIO_Init(GPIOB, &gpio_init_struct);

    oc.OCMode     = TIM_OCMODE_PWM1;       /* CNT<CCR时输出有效电平 */
    oc.Pulse      = 0;                     /* 初始占空比0(灭) */
    oc.OCPolarity = TIM_OCPOLARITY_HIGH;   /* 高电平点亮外接LED */
    oc.OCFastMode = TIM_OCFAST_DISABLE;
    HAL_TIM_PWM_ConfigChannel(&g_htim4b, &oc, TIM_CHANNEL_1);

    HAL_TIM_PWM_Start(&g_htim4b, TIM_CHANNEL_1);
}

/* 设置三路亮度: R/G 写 TIM3 的 CCR1/CCR2, B 写 TIM4 的 CCR1
 * 三路同为硬件PWM, 入参范围一致(0~999对应 0~100%占空比) */
static void rgb_set(uint16_t r, uint16_t g, uint16_t b)
{
    if (r > BREATH_ARR) r = BREATH_ARR;
    if (g > BREATH_ARR) g = BREATH_ARR;
    if (b > BREATH_ARR) b = BREATH_ARR;

    __HAL_TIM_SET_COMPARE(&g_htim3,  TIM_CHANNEL_1, r);
    __HAL_TIM_SET_COMPARE(&g_htim3,  TIM_CHANNEL_2, g);
    __HAL_TIM_SET_COMPARE(&g_htim4b, TIM_CHANNEL_1, b);
}

typedef enum { MODE_SYNC = 0, MODE_FLOW, MODE_POLICE, MODE_MAX } breath_mode_t;

static const char *mode_name[MODE_MAX] = { "SYNC ", "FLOW ", "POLICE" };

void task3_run(void)
{
    breath_mode_t mode = MODE_SYNC;
    uint16_t phase = 0;                  /* 呼吸相位 0~999 */
    uint8_t  police_flip = 0;            /* 警灯红蓝翻转 */
    uint8_t  tick = 0;                   /* 10ms计数, 警灯节拍用 */
    uint8_t  need_refresh = 1;
    uint8_t  key;

    oled_init();
    task3_tim3_init();
    task3_tim4b_init();
    key_init();

    while (1)
    {
        key = key_scan(0);
        if (key == KEY1)                /* 退出到任务菜单: 先关灯再停三路PWM */
        {
            rgb_set(0, 0, 0);
            HAL_TIM_PWM_Stop(&g_htim3,  TIM_CHANNEL_1);
            HAL_TIM_PWM_Stop(&g_htim3,  TIM_CHANNEL_2);
            HAL_TIM_PWM_Stop(&g_htim4b, TIM_CHANNEL_1);
            return;
        }
        if (key == KEY0)
        {
            mode = (breath_mode_t)((mode + 1) % MODE_MAX);
            rgb_set(0, 0, 0);
            phase = 0;
            need_refresh = 1;
        }

        if (need_refresh)
        {
            oled_clear();
            oled_show_string(0, 0,  (uint8_t *)"RGB BREATH", 16);
            oled_show_string(0, 20, (uint8_t *)"MODE:", 16);
            oled_show_string(48, 20, (uint8_t *)mode_name[mode], 16);
            oled_show_string(0, 40, (uint8_t *)"KEY0: NEXT", 16);
            oled_refresh_gram();
            need_refresh = 0;
        }

        switch (mode)
        {
        case MODE_SYNC:     /* 同步呼吸: 三路同相位 */
        {
            uint16_t v = breath_val(phase);
            rgb_set(v, v, v);
            break;
        }
        case MODE_FLOW:     /* 三色流水: 相位互差120度(ARR的1/3) */
            rgb_set(breath_val(phase),
                    breath_val((phase + 333) % 1000),
                    breath_val((phase + 666) % 1000));
            break;
        case MODE_POLICE:   /* 警灯: 红蓝交替爆闪, 绿灭 */
            if (police_flip) rgb_set(BREATH_ARR, 0, 0);
            else             rgb_set(0, 0, BREATH_ARR);
            break;
        default:
            break;
        }

        HAL_Delay(10);                  /* 10ms步进: phase 0->999 单程10s, 完整明暗周期10s */
        phase++;
        if (phase >= 1000) phase = 0;

        if (++tick >= 15)               /* 150ms翻转一次警灯 */
        {
            tick = 0;
            police_flip ^= 1;
        }
    }
}
