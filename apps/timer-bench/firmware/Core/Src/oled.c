#include "oled.h"
#include "oledfont.h"
#include "chinesefont16.h"

static uint8_t g_oled_gram[128][8];   /* 虚拟显存: 128列 * 8页 */

/* 数据线输出(引脚分散: D0~D7 = PC6,PC7,PC8,PC9,PC11,PD3,PB8,PB9) */
static void oled_data_out(uint8_t data)
{
    uint16_t dat = data & 0x0F;
    GPIOC->ODR &= ~(0xF << 6);                    /* 清 PC[9:6] */
    GPIOC->ODR |= dat << 6;                       /* D[3:0] -> PC[9:6] */
    GPIOC->ODR &= ~(0x1 << 11);                   /* 清 PC11 */
    GPIOC->ODR |= ((data >> 4) & 0x01) << 11;     /* D4 -> PC11 */
    GPIOD->ODR &= ~(0x1 << 3);                    /* 清 PD3 */
    GPIOD->ODR |= ((data >> 5) & 0x01) << 3;      /* D5 -> PD3 */
    GPIOB->ODR &= ~(0x3 << 8);                    /* 清 PB[9:8] */
    GPIOB->ODR |= ((data >> 6) & 0x01) << 8;      /* D6 -> PB8 */
    GPIOB->ODR |= ((data >> 7) & 0x01) << 9;      /* D7 -> PB9 */
}

/* 写一个字节: 8080 并口时序 */
static void oled_wr_byte(uint8_t data, uint8_t cmd)
{
    oled_data_out(data);
    OLED_RS(cmd);
    OLED_CS(0);
    OLED_WR(0);
    OLED_WR(1);
    OLED_CS(1);
    OLED_RS(1);
}

/* GPIO 初始化 */
static void oled_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio.Pin = GPIO_PIN_7 | GPIO_PIN_4 | GPIO_PIN_3;  /* PB7=CS, PB4=RS, PB3=RD */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_8;                            /* PH8=WR */
    HAL_GPIO_Init(GPIOH, &gpio);
    gpio.Pin = GPIO_PIN_15;                           /* PA15=RST */
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;               /* D7=PB9, D6=PB8 */
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_6 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_11; /* D0~D3=PC6~9, D4=PC11 */
    HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_3;                            /* D5=PD3 */
    HAL_GPIO_Init(GPIOD, &gpio);

    OLED_RD(1);    /* RD 常高(本项目只写不读) */
}

/* 初始化 */
void oled_init(void)
{
    oled_gpio_init();

    OLED_RST(0);                        /* 硬复位 */
    HAL_Delay(100);
    OLED_RST(1);
    HAL_Delay(100);

    oled_wr_byte(0xAE, OLED_CMD);       /* 关闭显示 */
    oled_wr_byte(0xD5, OLED_CMD); oled_wr_byte(0x50, OLED_CMD); /* 时钟分频 */
    oled_wr_byte(0xA8, OLED_CMD); oled_wr_byte(0x3F, OLED_CMD); /* 复用率 64 */
    oled_wr_byte(0xD3, OLED_CMD); oled_wr_byte(0x00, OLED_CMD); /* 显示偏移 */
    oled_wr_byte(0x40, OLED_CMD);       /* 起始行 */
    oled_wr_byte(0x8D, OLED_CMD); oled_wr_byte(0x14, OLED_CMD); /* 开电荷泵 */
    oled_wr_byte(0x20, OLED_CMD); oled_wr_byte(0x02, OLED_CMD); /* 页地址模式 */
    oled_wr_byte(0xA1, OLED_CMD);       /* 段重定义 */
    oled_wr_byte(0xC8, OLED_CMD);       /* COM 扫描方向 */
    oled_wr_byte(0xDA, OLED_CMD); oled_wr_byte(0x12, OLED_CMD); /* COM 硬件配置 */
    oled_wr_byte(0x81, OLED_CMD); oled_wr_byte(0xEF, OLED_CMD); /* 对比度 */
    oled_wr_byte(0xD9, OLED_CMD); oled_wr_byte(0xF1, OLED_CMD); /* 预充电 */
    oled_wr_byte(0xDB, OLED_CMD); oled_wr_byte(0x30, OLED_CMD); /* VCOM 电平 */
    oled_wr_byte(0xA4, OLED_CMD);       /* 全局显示 */
    oled_wr_byte(0xA6, OLED_CMD);       /* 正常显示 */
    oled_wr_byte(0xAF, OLED_CMD);       /* 开显示 */
    oled_clear();
}

/* 整屏刷新 */
void oled_refresh_gram(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
    {
        oled_wr_byte(0xB0 + i, OLED_CMD);  /* 页地址 */
        oled_wr_byte(0x00, OLED_CMD);      /* 列低位 */
        oled_wr_byte(0x10, OLED_CMD);      /* 列高位 */
        for (n = 0; n < 128; n++)
            oled_wr_byte(g_oled_gram[n][i], OLED_DATA);
    }
}

/* 画点 */
void oled_draw_point(uint8_t x, uint8_t y, uint8_t dot)
{
    uint8_t pos, bx, temp = 0;
    if (x > 127 || y > 63) return;
    pos  = y / 8;
    bx   = y % 8;
    temp = 1 << bx;
    if (dot) g_oled_gram[x][pos] |=  temp;
    else     g_oled_gram[x][pos] &= ~temp;
}

/* 清屏 */
void oled_clear(void)
{
    uint8_t i, n;
    for (i = 0; i < 8; i++)
        for (n = 0; n < 128; n++)
            g_oled_gram[n][i] = 0;
    oled_refresh_gram();
}

/* 显示单个字符 */
void oled_show_char(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode)
{
    uint8_t temp, t, t1, y0 = y;
    uint8_t *pfont = 0;
    uint8_t csize = (size / 8 + ((size % 8) ? 1 : 0)) * (size / 2);
    chr = chr - ' ';
    if      (size == 12) pfont = (uint8_t *)oled_asc2_1206[chr];
    else if (size == 16) pfont = (uint8_t *)oled_asc2_1608[chr];
    else if (size == 24) pfont = (uint8_t *)oled_asc2_2412[chr];
    else return;
    for (t = 0; t < csize; t++)
    {
        temp = pfont[t];
        for (t1 = 0; t1 < 8; t1++)
        {
            if (temp & 0x80) oled_draw_point(x, y, mode);
            else             oled_draw_point(x, y, !mode);
            temp <<= 1;
            y++;
            if ((y - y0) == size) { y = y0; x++; break; }
        }
    }
}

/* 显示字符串 */
void oled_show_string(uint8_t x, uint8_t y, uint8_t *p, uint8_t size)
{
    while ((*p <= '~') && (*p >= ' '))
    {
        if (x > (128 - size / 2)) { y += size; x = 0; }
        if (y > (64 - size)) break;
        oled_show_char(x, y, *p, size, 1);
        x += size / 2;
        p++;
    }
}

static uint32_t oled_pow(uint8_t m, uint8_t n)
{
    uint32_t result = 1;
    while (n--) result *= m;
    return result;
}

/* 显示数字(十进制) */
void oled_show_num(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size)
{
    uint8_t t, temp, enshow = 0;
    for (t = 0; t < len; t++)
    {
        temp = (num / oled_pow(10, len - t - 1)) % 10;
        if (enshow == 0 && t < (len - 1))
        {
            if (temp == 0) { oled_show_char(x + (size / 2) * t, y, ' ', size, 1); continue; }
            else enshow = 1;
        }
        oled_show_char(x + (size / 2) * t, y, temp + '0', size, 1);
    }
}

/* 显示单个16x16中文字(字库数据32字节, 逐列式/高位在前) */
static void oled_show_cn_char16(uint8_t x, uint8_t y, const uint8_t *data)
{
    uint8_t t, t1, temp, y0 = y;
    for (t = 0; t < 32; t++)
    {
        temp = data[t];
        for (t1 = 0; t1 < 8; t1++)
        {
            if (temp & 0x80) oled_draw_point(x, y, 1);
            else             oled_draw_point(x, y, 0);
            temp <<= 1;
            y++;
            if ((y - y0) == 16) { y = y0; x++; break; }   /* 一列16点(2字节)画完, 移下一列 */
        }
    }
}

/* 显示中文字符串(UTF-8, 16x16): 中文3字节查表, ASCII半角按8px占位 */
void oled_show_cn16(uint8_t x, uint8_t y, const char *str)
{
    while (*str)
    {
        if (((uint8_t)*str & 0x80) == 0)        /* ASCII单字节: 用ASCII字库显示 */
        {
            oled_show_char(x, y, *str, 16, 1);
            x += 8;
            str++;
        }
        else                                    /* 中文: UTF-8三字节查表 */
        {
            uint8_t i;
            for (i = 0; i < CN16_FONT_COUNT; i++)
            {
                if ((uint8_t)str[0] == (uint8_t)cn16_font[i].name[0] &&
                    (uint8_t)str[1] == (uint8_t)cn16_font[i].name[1] &&
                    (uint8_t)str[2] == (uint8_t)cn16_font[i].name[2])
                {
                    oled_show_cn_char16(x, y, cn16_font[i].data);
                    break;
                }
            }
            x += 16;
            str += 3;
        }
    }
}

/* 显示有符号整数(自动位数, 无前导零), 封装给上层直接调用的整数显示接口 */
void oled_show_int(uint8_t x, uint8_t y, int32_t num, uint8_t size)
{
    char buf[12];
    uint8_t i = 0, j;
    uint32_t u;

    if (num < 0) { u = (uint32_t)(-num); }
    else         { u = (uint32_t)num; }

    if (u == 0) buf[i++] = '0';
    while (u) { buf[i++] = (char)('0' + (u % 10)); u /= 10; }
    if (num < 0) buf[i++] = '-';

    for (j = 0; j < i; j++)                    /* 逆序输出 */
    {
        oled_show_char(x + (size / 2) * j, y, (uint8_t)buf[i - 1 - j], size, 1);
    }
}
