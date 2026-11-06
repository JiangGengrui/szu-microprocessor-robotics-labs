#ifndef __NORFLASH_H
#define __NORFLASH_H

#include "main.h"

/* =========================================================================
 * 板载 SPI NOR FLASH(25QXX 系列)驱动
 *   移植自 正点原子 阿波罗 F429 NOR Flash 例程
 *
 *   接口 : SPI5  (PF7 = SCK / PF8 = MISO / PF9 = MOSI)
 *   片选 : PF6   (普通 GPIO 输出, 低有效)
 *   容量 : 扇区 4KB / 块 64KB / 页 256B, 擦除最小单位 = 1 个扇区
 *   读 ID: 0x90 命令 + 3 字节哑元, 读回 2 字节 = 厂商码<<8 | 容量码
 *          W25Q256 / BY25Q256 容量码为 0x18, 需使能 4 字节地址模式
 * ========================================================================= */

/* NOR FLASH 片选引脚(PF6) */
#define NORFLASH_CS_GPIO_PORT           GPIOF
#define NORFLASH_CS_GPIO_PIN            GPIO_PIN_6
#define NORFLASH_CS_GPIO_CLK_ENABLE()   do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)

#define NORFLASH_CS(x)      do{ x ? \
                                  HAL_GPIO_WritePin(NORFLASH_CS_GPIO_PORT, NORFLASH_CS_GPIO_PIN, GPIO_PIN_SET) : \
                                  HAL_GPIO_WritePin(NORFLASH_CS_GPIO_PORT, NORFLASH_CS_GPIO_PIN, GPIO_PIN_RESET); \
                            }while(0)

/* FLASH 芯片列表(norflash_read_id() 的返回值) */
#define W25Q80      0XEF13          /* W25Q80   芯片 ID */
#define W25Q16      0XEF14          /* W25Q16   芯片 ID */
#define W25Q32      0XEF15          /* W25Q32   芯片 ID */
#define W25Q64      0XEF16          /* W25Q64   芯片 ID */
#define W25Q128     0XEF17          /* W25Q128  芯片 ID */
#define W25Q256     0XEF18          /* W25Q256  芯片 ID */
#define BY25Q64     0X6816          /* BY25Q64  芯片 ID */
#define BY25Q128    0X6817          /* BY25Q128 芯片 ID */
#define BY25Q256    0X6818          /* BY25Q256 芯片 ID */
#define NM25Q64     0X5216          /* NM25Q64  芯片 ID */
#define NM25Q128    0X5217          /* NM25Q128 芯片 ID */

/* 指令表 */
#define FLASH_WriteEnable           0x06
#define FLASH_WriteDisable          0x04
#define FLASH_ReadStatusReg1        0x05
#define FLASH_ReadStatusReg2        0x35
#define FLASH_ReadStatusReg3        0x15
#define FLASH_WriteStatusReg1       0x01
#define FLASH_WriteStatusReg2       0x31
#define FLASH_WriteStatusReg3       0x11
#define FLASH_ReadData              0x03
#define FLASH_FastReadData          0x0B
#define FLASH_PageProgram           0x02
#define FLASH_BlockErase            0xD8
#define FLASH_SectorErase           0x20
#define FLASH_ChipErase             0xC7
#define FLASH_ManufactDeviceID      0x90
#define FLASH_JedecDeviceID         0x9F
#define FLASH_Enable4ByteAddr       0xB7
#define FLASH_Exit4ByteAddr         0xE9

extern uint16_t g_norflash_type;        /* 当前芯片型号(0x90 读出的 2 字节 ID) */

/* ---------- 对外接口 ---------- */
void     norflash_init(void);                                           /* 初始化(含 CS 引脚 + 4 字节地址模式判断) */
uint16_t norflash_read_id(void);                                        /* 读芯片 ID(0x90), 如 W25Q256 -> 0xEF18 */
void     norflash_write_enable(void);                                   /* 写使能 */
uint8_t  norflash_read_sr(uint8_t regno);                               /* 读状态寄存器 regno = 1~3 */
void     norflash_write_sr(uint8_t regno, uint8_t sr);                  /* 写状态寄存器 regno = 1~3 */

void     norflash_erase_chip(void);                                     /* 整片擦除(耗时很长) */
void     norflash_erase_sector(uint32_t saddr);                         /* 擦除 1 个扇区, saddr = 扇区号(非字节地址) */
void     norflash_read(uint8_t *pbuf, uint32_t addr, uint16_t datalen); /* 从 addr 读 datalen 字节(<=65535) */
void     norflash_write(uint8_t *pbuf, uint32_t addr, uint16_t datalen);/* 写 datalen 字节到 addr, 带擦除 */

#endif
