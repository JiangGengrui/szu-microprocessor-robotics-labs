#include "key.h"

/* 按键初始化: PH2/PH3 上拉输入, PA0 下拉输入 */
void key_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio_init_struct.Pin  = GPIO_PIN_2 | GPIO_PIN_3;    /* KEY1/KEY0 */
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOH, &gpio_init_struct);

    gpio_init_struct.Pin  = GPIO_PIN_0;                 /* WK_UP */
    gpio_init_struct.Mode = GPIO_MODE_INPUT;
    gpio_init_struct.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gpio_init_struct);
}

/* 按键扫描(带消抖)
 * mode = 0: 单击模式, 按住不松开只返回一次
 * mode = 1: 连按模式, 按住可重复返回
 * 返回值: 0=无按键, 1=KEY0, 2=KEY1, 3=WK_UP
 */
uint8_t key_scan(uint8_t mode)
{
    static uint8_t key_up = 1;         /* 按键松开标志 */
    uint8_t k0, k1, kw;

    if (mode) key_up = 1;              /* 连按模式直接视为已松开 */

    k0 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET);
    k1 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET);
    kw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);

    if (key_up && (k0 || k1 || kw))
    {
        HAL_Delay(10);                 /* 消抖 */
        k0 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET);
        k1 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET);
        kw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);
        key_up = 0;
        if (k0) return KEY0;
        if (k1) return KEY1;
        if (kw) return WK_UP;
    }
    else if (!k0 && !k1 && !kw)
    {
        key_up = 1;
    }
    return 0;
}
