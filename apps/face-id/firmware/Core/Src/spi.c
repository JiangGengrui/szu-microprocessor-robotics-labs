#include "spi.h"

/* =========================================================================
 * SPI5 薄封装(移植自正点原子阿波罗 F429 NOR Flash 例程的 spi.c, 但去掉 spi5_init()/
 * HAL_SPI_MspInit(), 因为这两件事已由 CubeMX 生成的 MX_SPI5_Init() 与
 * stm32f4xx_hal_msp.c 的 HAL_SPI_MspInit() 完成)。
 *
 * 注意 HAL 的一个细节: HAL_SPI_Init() 内部 **不会** 使能 SPI, 必须显式
 * __HAL_SPI_ENABLE()。这里 spi5_set_speed() 做了关闭-改分频-使能三步,
 * 所以只要调用过 spi5_set_speed()(norflash_init 会调用), SPI5 就是使能状态。
 * ========================================================================= */

/**
 * @brief  SPI5 速度设置
 * @note   SPI 速率 = fPCLK2 / 分频系数; SPI5 挂在 APB2 上, 本工程 APB2 = 90MHz,
 *         故 SPI_BAUDRATEPRESCALER_2  ->  45 MHz
 * @param  baudrate_prescaler : SPI_BAUDRATEPRESCALER_x
 */
void spi5_set_speed(uint32_t baudrate_prescaler)
{
    assert_param(IS_SPI_BAUDRATE_PRESCALER(baudrate_prescaler));
    __HAL_SPI_DISABLE(&hspi5);                 /* 改分频前必须先关闭 SPI */
    hspi5.Instance->CR1 &= 0xFFC7;             /* 位 5:3(BR[2:0])清零 */
    hspi5.Instance->CR1 |= baudrate_prescaler; /* 写入新的分频系数 */
    __HAL_SPI_ENABLE(&hspi5);                  /* 重新使能 */
}

/**
 * @brief  SPI5 读写一个字节
 * @param  txdata : 要发送的字节
 * @retval 收到的字节
 */
uint8_t spi5_read_write_byte(uint8_t txdata)
{
    uint8_t rxdata = 0;

    HAL_SPI_TransmitReceive(&hspi5, &txdata, &rxdata, 1, 1000);

    return rxdata;
}
