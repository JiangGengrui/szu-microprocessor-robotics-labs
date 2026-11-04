#include "beep.h"
#include "i2c.h"

static uint8_t  s_beeping = 0;       /* 正在鸣叫 */
static uint32_t s_off_tick = 0;      /* 到期时刻 */

uint8_t BEEP_Init(void)
{
    uint8_t state = BEEP_OFF_BYTE;
    if (HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &state, 1, 100) != HAL_OK)
        return 1;                    /* 总线上无应答 */
    return 0;
}

void BEEP_Beep(uint32_t ms)
{
    uint8_t on = BEEP_ON_BYTE;
    HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &on, 1, 100);
    s_beeping  = 1;
    s_off_tick = HAL_GetTick() + ms;
}

void BEEP_Task(void)
{
    if (s_beeping && (HAL_GetTick() >= s_off_tick)) {
        uint8_t off = BEEP_OFF_BYTE;
        HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &off, 1, 100);
        s_beeping = 0;
    }
}
