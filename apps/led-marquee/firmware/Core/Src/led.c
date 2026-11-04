#include "led.h"
#include "gpio.h"

/* 8 个面包板 LED 的引脚表（PG14/PA1/PC4/PH13/PH15/PI1/PG11/PI5，低电平点亮） */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
} led_map_t;

static const led_map_t s_leds[8] = {
    {LED0_GPIO_Port, LED0_Pin},
    {LED1_GPIO_Port, LED1_Pin},
    {LED2_GPIO_Port, LED2_Pin},
    {LED3_GPIO_Port, LED3_Pin},
    {LED4_GPIO_Port, LED4_Pin},
    {LED5_GPIO_Port, LED5_Pin},
    {LED6_GPIO_Port, LED6_Pin},
    {LED7_GPIO_Port, LED7_Pin},
};

/* pattern 的 bit i =1 时第 i 个 LED 输出低电平（点亮） */
void LED_Write(uint8_t pattern)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (pattern & (1u << i))
            HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, GPIO_PIN_RESET);
        else
            HAL_GPIO_WritePin(s_leds[i].port, s_leds[i].pin, GPIO_PIN_SET);
    }
}

void BOARD_LED0_Set(uint8_t on)
{
    HAL_GPIO_WritePin(BOARD_LED0_GPIO_Port, BOARD_LED0_Pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

void BOARD_LED1_Set(uint8_t on)
{
    HAL_GPIO_WritePin(BOARD_LED1_GPIO_Port, BOARD_LED1_Pin,
                      on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
