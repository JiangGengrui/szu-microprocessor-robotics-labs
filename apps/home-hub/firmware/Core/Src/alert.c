#include "alert.h"

static alert_state_t g_state = ALERT_OK;
static uint8_t g_t_over, g_h_over, g_v_over, g_dht_fault;
static uint8_t g_t_latch, g_h_latch, g_v_latch;   /* 回差锁存: 1=当前判定为越限 */

void alert_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    g.Pin = GPIO_PIN_0 | GPIO_PIN_1;               /* LED1=PB0(绿), LED0=PB1(红) */
    HAL_GPIO_Init(GPIOB, &g);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);   /* 低电平点亮 -> 先灭 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
}

/* 带回差的单通道判定: 超过"上限"进入; 低于"上限-回差"退出 (全整数) */
static uint8_t judge(int32_t v, int32_t hi, int32_t hyst, uint8_t *latch, uint8_t valid)
{
    if (!valid) { *latch = 0; return 0; }
    if (*latch) { if (v < hi - hyst) *latch = 0; }
    else        { if (v > hi)        *latch = 1; }
    return *latch;
}

void alert_poll(int16_t temp, uint8_t temp_ok, int16_t humi, uint8_t humi_ok,
                uint32_t mv, uint8_t adc_ok)
{
    g_t_over = judge(temp, CFG_TEMP_MAX, CFG_HYST_TEMP, &g_t_latch, temp_ok);
    g_h_over = judge(humi, CFG_HUMI_MAX, CFG_HYST_HUMI, &g_h_latch, humi_ok);
    g_v_over = judge((int32_t)mv, CFG_VOLT_MAX_MV, CFG_HYST_VOLT,
                     &g_v_latch, adc_ok);
    g_dht_fault = !temp_ok;

    if (g_dht_fault || !adc_ok)                 g_state = ALERT_FAULT;
    else if (g_t_over || g_h_over || g_v_over)  g_state = ALERT_LIMIT;
    else                                        g_state = ALERT_OK;

    /* 驱动 LED (低电平点亮) */
    switch (g_state)
    {
    case ALERT_OK:
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);   /* 绿亮 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);     /* 红灭 */
        break;
    case ALERT_LIMIT:                                           /* 红 2Hz 闪: 250ms 翻转 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1,
            ((HAL_GetTick() / 250U) & 1U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        break;
    case ALERT_FAULT:
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);     /* 绿灭 */
        HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);   /* 红常亮 */
        break;
    }
}

alert_state_t alert_get(void)  { return g_state; }
uint8_t alert_temp_over(void)  { return g_t_over; }
uint8_t alert_humi_over(void)  { return g_h_over; }
uint8_t alert_volt_over(void)  { return g_v_over; }
uint8_t alert_dht_fault(void)  { return g_dht_fault; }
