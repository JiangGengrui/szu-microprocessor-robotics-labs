#include "key.h"

void key_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOH_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP;
    g.Pin = GPIO_PIN_3 | GPIO_PIN_2;                 /* KEY0=PH3, KEY1=PH2 */
    HAL_GPIO_Init(GPIOH, &g);
    g.Pin = GPIO_PIN_13;                             /* KEY2=PC13 */
    HAL_GPIO_Init(GPIOC, &g);
    g.Pull = GPIO_PULLDOWN; g.Pin = GPIO_PIN_0;      /* WK_UP=PA0 */
    HAL_GPIO_Init(GPIOA, &g);
}

uint8_t key_scan(void)
{
    static uint8_t up = 1;
    uint8_t k0 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET);
    uint8_t k1 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET);
    uint8_t k2 = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET);
    uint8_t kw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);

    if (up && (k0 || k1 || k2 || kw))
    {
        HAL_Delay(10);                               /* 消抖 */
        k0 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_3) == GPIO_PIN_RESET);
        k1 = (HAL_GPIO_ReadPin(GPIOH, GPIO_PIN_2) == GPIO_PIN_RESET);
        k2 = (HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_13) == GPIO_PIN_RESET);
        kw = (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0) == GPIO_PIN_SET);
        up = 0;
        if (k0) return KEY0;
        if (k1) return KEY1;
        if (k2) return KEY2;
        if (kw) return WK_UP;
    }
    else if (!k0 && !k1 && !k2 && !kw) up = 1;
    return 0;
}
