#include "dht11.h"

#define DHT11_PORT   GPIOB
#define DHT11_PIN    GPIO_PIN_12

/* DWT 微秒延时 */
static void dht11_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks);
}

static void dht11_mode_input(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = DHT11_PIN; g.Mode = GPIO_MODE_INPUT;
    g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &g);
}
static void dht11_mode_output(void)
{
    GPIO_InitTypeDef g = {0};
    g.Pin = DHT11_PIN; g.Mode = GPIO_MODE_OUTPUT_OD;   /* 开漏! */
    g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT11_PORT, &g);
}

void dht11_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;   /* 使能 DWT */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    DHT11_PORT->BSRR = DHT11_PIN;                     /* 释放总线 */
    dht11_mode_output();
}

/* 读一位: 返回0/1; 0xFF=超时 */
static uint8_t dht11_read_bit(void)
{
    uint32_t t = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        if (++t > 10000) return 0xFF;
    t = 0;
    while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
        if (++t > 10000) return 0xFF;                 /* 等 54us 低电平结束 */
    dht11_delay_us(40);                               /* 40us 后采样 */
    return (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET) ? 1 : 0;
}

static uint8_t dht11_read_byte(void)
{
    uint8_t i, b = 0;
    for (i = 0; i < 8; i++)
    {
        uint8_t bit = dht11_read_bit();
        if (bit == 0xFF) return 0xFF;
        b = (uint8_t)((b << 1) | bit);
    }
    return b;
}

uint8_t dht11_read(dht11_data_t *d)
{
    uint8_t buf[5], i, sum;
    uint32_t t;

    d->valid = 0;

    /* 起始信号: 拉低 >=18ms */
    dht11_mode_output();
    HAL_GPIO_WritePin(DHT11_PORT, DHT11_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    DHT11_PORT->BSRR = DHT11_PIN;
    dht11_delay_us(30);
    dht11_mode_input();

    /* 等响应 */
    t = 0; while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        if (++t > 20000) return 1;
    t = 0; while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_RESET)
        if (++t > 20000) return 1;
    t = 0; while (HAL_GPIO_ReadPin(DHT11_PORT, DHT11_PIN) == GPIO_PIN_SET)
        if (++t > 20000) return 1;

    for (i = 0; i < 5; i++)
    {
        buf[i] = dht11_read_byte();
        if (buf[i] == 0xFF) return 1;
    }
    sum = (uint8_t)(buf[0] + buf[1] + buf[2] + buf[3]);
    if (sum != buf[4]) return 2;                      /* 校验失败 */

    d->humi_int = buf[0]; d->humi_dec = buf[1];
    d->temp_int = buf[2]; d->temp_dec = buf[3];
    d->valid = 1;
    return 0;
}
