#include "norflash.h"
#include "spi.h"

/* =========================================================================
 * NOR FLASH(25QXX)驱动实现 —— 移植自 正点原子 NOR Flash 例程, 逐字保留官方流程:
 *   - 读 ID 用 0x90 + 3 字节哑元(2 字节返回)
 *   - 读/写长度参数为 uint16_t
 *   - norflash_erase_sector() 的参数是"扇区号"(内部 ×4096 变字节地址)
 *   - norflash_write() 内部做 读-改-擦-写, 保留同扇区其它数据
 * 仅有的改动: 去掉官方 spi5_init()(由 CubeMX 的 MX_SPI5_Init 完成)与
 *             delay/usart 依赖; 把 static 原型从 .h 挪回本文件。
 * ========================================================================= */

uint16_t g_norflash_type = BY25Q256;      /* 默认按 BY25Q256, 实际由 norflash_init 读出 */
uint8_t  g_norflash_buf[4096];            /* 扇区缓存(norflash_write 读-改-写用) */

static void norflash_wait_busy(void);
static void norflash_send_address(uint32_t address);
static void norflash_write_page(uint8_t *pbuf, uint32_t addr, uint16_t datalen);
static void norflash_write_nocheck(uint8_t *pbuf, uint32_t addr, uint16_t datalen);

/**
 * @brief  初始化 SPI NOR FLASH
 */
void norflash_init(void)
{
    uint8_t temp;
    GPIO_InitTypeDef gpio_init_struct;

    NORFLASH_CS_GPIO_CLK_ENABLE();                          /* CS 脚时钟使能 */

    gpio_init_struct.Pin   = NORFLASH_CS_GPIO_PIN;
    gpio_init_struct.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(NORFLASH_CS_GPIO_PORT, &gpio_init_struct);

    NORFLASH_CS(1);                                         /* 取消片选 */

    /* SPI5 外设已由 MX_SPI5_Init() 以 256 分频初始化, 这里提速到 2 分频(45MHz) */
    spi5_set_speed(SPI_BAUDRATEPRESCALER_2);

    g_norflash_type = norflash_read_id();                   /* 读芯片 ID */

    if (g_norflash_type == W25Q256 || g_norflash_type == BY25Q256)
    {   /* 容量 32MB 的 25Q256 必须使能 4 字节地址模式 */
        temp = norflash_read_sr(3);                         /* 读状态寄存器 3 判断地址模式 */
        if ((temp & 0X01) == 0)                             /* 当前不是 4 字节地址模式 */
        {
            norflash_write_enable();                        /* 写使能 */
            temp |= 1 << 1;                                 /* ADP = 1 */
            norflash_write_sr(3, temp);                     /* 写 SR3 */

            NORFLASH_CS(0);
            spi5_read_write_byte(FLASH_Enable4ByteAddr);    /* 使能 4 字节地址指令 */
            NORFLASH_CS(1);
        }
    }
}

/**
 * @brief  等待芯片空闲(BUSY 位为 0)
 */
static void norflash_wait_busy(void)
{
    while ((norflash_read_sr(1) & 0x01) == 0x01);
}

/**
 * @brief  写使能(把 S1 的 WEL 置位)
 */
void norflash_write_enable(void)
{
    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_WriteEnable);
    NORFLASH_CS(1);
}

/**
 * @brief  发送地址(25Q256 发 4 字节, 其它发 3 字节)
 */
static void norflash_send_address(uint32_t address)
{
    if (g_norflash_type == W25Q256 || g_norflash_type == BY25Q256)
    {
        spi5_read_write_byte((uint8_t)((address) >> 24));
    }
    spi5_read_write_byte((uint8_t)((address) >> 16));
    spi5_read_write_byte((uint8_t)((address) >> 8));
    spi5_read_write_byte((uint8_t)(address));
}

/**
 * @brief  读状态寄存器
 * @param  regno : 1~3
 * @retval 状态寄存器值
 */
uint8_t norflash_read_sr(uint8_t regno)
{
    uint8_t byte = 0, command = 0;

    switch (regno)
    {
        case 1:  command = FLASH_ReadStatusReg1; break;
        case 2:  command = FLASH_ReadStatusReg2; break;
        case 3:  command = FLASH_ReadStatusReg3; break;
        default: command = FLASH_ReadStatusReg1; break;
    }

    NORFLASH_CS(0);
    spi5_read_write_byte(command);
    byte = spi5_read_write_byte(0XFF);
    NORFLASH_CS(1);

    return byte;
}

/**
 * @brief  写状态寄存器
 * @param  regno : 1~3
 * @param  sr    : 要写入的值
 */
void norflash_write_sr(uint8_t regno, uint8_t sr)
{
    uint8_t command = 0;

    switch (regno)
    {
        case 1:  command = FLASH_WriteStatusReg1; break;
        case 2:  command = FLASH_WriteStatusReg2; break;
        case 3:  command = FLASH_WriteStatusReg3; break;
        default: command = FLASH_WriteStatusReg1; break;
    }

    NORFLASH_CS(0);
    spi5_read_write_byte(command);
    spi5_read_write_byte(sr);
    NORFLASH_CS(1);
}

/**
 * @brief  读芯片 ID(0x90 命令)
 * @retval 2 字节 ID, 如 W25Q256 = 0xEF18, BY25Q256 = 0x6818
 */
uint16_t norflash_read_id(void)
{
    uint16_t deviceid;

    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_ManufactDeviceID);   /* 发送 0x90 */
    spi5_read_write_byte(0);                        /* 3 字节哑元地址 */
    spi5_read_write_byte(0);
    spi5_read_write_byte(0);
    deviceid  = spi5_read_write_byte(0xFF) << 8;    /* 厂商码 */
    deviceid |= spi5_read_write_byte(0xFF);         /* 容量码 */
    NORFLASH_CS(1);

    return deviceid;
}

/**
 * @brief  读数据
 */
void norflash_read(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t i;

    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_ReadData);
    norflash_send_address(addr);
    for (i = 0; i < datalen; i++)
    {
        pbuf[i] = spi5_read_write_byte(0XFF);
    }
    NORFLASH_CS(1);
}

/**
 * @brief  在 1 页内写(不超过 256 字节, 不跨页)
 */
static void norflash_write_page(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t i;

    norflash_write_enable();

    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_PageProgram);
    norflash_send_address(addr);
    for (i = 0; i < datalen; i++)
    {
        spi5_read_write_byte(pbuf[i]);
    }
    NORFLASH_CS(1);
    norflash_wait_busy();
}

/**
 * @brief  无校验写(自动换页), 要求目标区已擦除(全 0xFF)
 */
static void norflash_write_nocheck(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint16_t pageremain;

    pageremain = 256 - addr % 256;          /* 本页剩余字节数 */
    if (datalen <= pageremain)
    {
        pageremain = datalen;
    }

    while (1)
    {
        norflash_write_page(pbuf, addr, pageremain);

        if (datalen == pageremain)          /* 写完了 */
        {
            break;
        }
        else
        {
            pbuf     += pageremain;
            addr     += pageremain;
            datalen  -= pageremain;

            if (datalen > 256)
            {
                pageremain = 256;
            }
            else
            {
                pageremain = datalen;
            }
        }
    }
}

/**
 * @brief  写数据(带擦除, 自动保留同扇区其它数据)
 * @param  pbuf    : 数据
 * @param  addr    : 起始地址
 * @param  datalen : 字节数(<=65535)
 */
void norflash_write(uint8_t *pbuf, uint32_t addr, uint16_t datalen)
{
    uint32_t secpos;
    uint16_t secoff;
    uint16_t secremain;
    uint16_t i;
    uint8_t *norflash_buf = g_norflash_buf;

    secpos    = addr / 4096;                /* 扇区号 */
    secoff    = addr % 4096;                /* 扇区内偏移 */
    secremain = 4096 - secoff;              /* 扇区剩余空间 */

    if (datalen <= secremain)
    {
        secremain = datalen;
    }

    while (1)
    {
        norflash_read(norflash_buf, secpos * 4096, 4096);       /* 读出整个扇区 */

        for (i = 0; i < secremain; i++)                         /* 检查是否已是 0xFF */
        {
            if (norflash_buf[secoff + i] != 0XFF)
            {
                break;                                          /* 需要擦除 */
            }
        }

        if (i < secremain)                                      /* 需要擦除: 读-改-擦-写 */
        {
            norflash_erase_sector(secpos);

            for (i = 0; i < secremain; i++)
            {
                norflash_buf[i + secoff] = pbuf[i];
            }

            norflash_write_nocheck(norflash_buf, secpos * 4096, 4096);
        }
        else                                                    /* 已擦除区直接写 */
        {
            norflash_write_nocheck(pbuf, addr, secremain);
        }

        if (datalen == secremain)
        {
            break;
        }
        else
        {
            secpos++;
            secoff = 0;

            pbuf    += secremain;
            addr    += secremain;
            datalen -= secremain;

            if (datalen > 4096)
            {
                secremain = 4096;
            }
            else
            {
                secremain = datalen;
            }
        }
    }
}

/**
 * @brief  整片擦除(耗时非常长, 慎用)
 */
void norflash_erase_chip(void)
{
    norflash_write_enable();
    norflash_wait_busy();
    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_ChipErase);
    NORFLASH_CS(1);
    norflash_wait_busy();
}

/**
 * @brief  擦除一个扇区
 * @note   参数是扇区号, 不是字节地址! 内部会 ×4096
 * @param  saddr : 扇区号
 */
void norflash_erase_sector(uint32_t saddr)
{
    saddr *= 4096;
    norflash_write_enable();
    norflash_wait_busy();

    NORFLASH_CS(0);
    spi5_read_write_byte(FLASH_SectorErase);
    norflash_send_address(saddr);
    NORFLASH_CS(1);
    norflash_wait_busy();
}
