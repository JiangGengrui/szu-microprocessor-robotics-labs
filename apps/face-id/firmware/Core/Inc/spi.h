#ifndef __SPI_H
#define __SPI_H

#include "main.h"

/* =========================================================================
 * SPI5 适配层(薄封装) —— 供 norflash.c 使用
 *
 * 引脚: PF7 = SPI5_SCK / PF8 = SPI5_MISO / PF9 = SPI5_MOSI (AF5)
 *       PF6 = NOR FLASH 片选(普通 GPIO 输出, 在 norflash.c 里控制)
 * 引脚复用 + SPI5 外设初始化由 CubeMX 生成的
 *   MX_SPI5_Init()      (main.c)
 *   HAL_SPI_MspInit()   (stm32f4xx_hal_msp.c)
 * 负责, 因此本文件只提供"运行时改速率 + 收发一个字节"两个小函数,
 * 不再重复实现 spi5_init()/HAL_SPI_MspInit(), 避免与 CubeMX 重复定义。
 * ========================================================================= */

extern SPI_HandleTypeDef hspi5;                        /* CubeMX 生成的 SPI5 句柄(main.c) */

void    spi5_set_speed(uint32_t baudrate_prescaler);   /* 运行时修改波特率分频(SPI_BAUDRATEPRESCALER_x) */
uint8_t spi5_read_write_byte(uint8_t txdata);          /* 全双工收发 1 字节 */

#endif
