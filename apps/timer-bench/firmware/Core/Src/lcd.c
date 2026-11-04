#include "lcd.h"
#include "font.h"
#include "chinesefont.h"

/* 帧缓冲定位在 SDRAM 首地址 0xC0000000 */
#define LCD_FRAMEBUF_ADDR   0xC0000000

static volatile uint16_t *g_lcd_framebuf = (volatile uint16_t *)LCD_FRAMEBUF_ADDR;

/* 运行时面板分辨率, 默认800x480, 由屏ID识别后覆盖 */
uint16_t g_lcd_width  = LCD_WIDTH;
uint16_t g_lcd_height = LCD_HEIGHT;

/* 画点(内部函数) */
static void lcd_draw_point(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= g_lcd_width || y >= g_lcd_height) return;
    g_lcd_framebuf[(uint32_t)y * g_lcd_width + x] = color;
}

/* 点亮背光(PB5)并清屏 */
void lcd_init(void)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();
    gpio_init_struct.Pin   = GPIO_PIN_5;           /* LCD_BL */
    gpio_init_struct.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio_init_struct);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);  /* 背光点亮 */

    lcd_clear(BLACK);                              /* 初始清屏为黑 */
}

/* 整屏单色填充 */
void lcd_clear(uint16_t color)
{
    uint32_t i;
    for (i = 0; i < (uint32_t)g_lcd_width * g_lcd_height; i++)
    {
        g_lcd_framebuf[i] = color;
    }
}

/* 显示单个字符: 字库偏移32, 逐列式/高位在前, 透明背景只画前景 */
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint8_t size, uint16_t color)
{
    const uint8_t *pfont = 0;
    uint8_t w, ceil8, c, r;

    if (ch < ' ' || ch > '~') return;
    ch = ch - ' ';                           /* 字库从空格开始 */

    if      (size == 12) pfont = asc2_1206[(uint8_t)ch];
    else if (size == 16) pfont = asc2_1608[(uint8_t)ch];
    else if (size == 24) pfont = asc2_2412[(uint8_t)ch];
    else if (size == 32) pfont = asc2_3216[(uint8_t)ch];
    else return;

    w     = size / 2;                        /* 字符点阵宽度 */
    ceil8 = (size + 7) / 8;                  /* 每列占字节数 */

    for (c = 0; c < w; c++)
    {
        for (r = 0; r < size; r++)
        {
            uint8_t dat = pfont[c * ceil8 + r / 8];
            if ((dat >> (7 - (r % 8))) & 0x01)     /* 该位为1才画前景 */
            {
                lcd_draw_point(x + c, y + r, color);
            }
        }
    }
}

/* 显示字符串 */
void lcd_show_string(uint16_t x, uint16_t y, uint8_t size, char *str, uint16_t color)
{
    while (*str)
    {
        lcd_show_char(x, y, *str, size, color);
        x += size / 2;                       /* 右移一个字符宽度 */
        str++;
    }
}

/* 显示单个中文字(UTF-8, 3字节): 在字库表中查3字节匹配 */
static void lcd_show_cn_char(uint16_t x, uint16_t y, const char *cn, uint8_t size, uint16_t color)
{
    uint8_t c, r, i, ceil8;
    const uint8_t *pfont = 0;
    uint8_t count = (size == 32) ? CN32_FONT_COUNT : CN24_FONT_COUNT;

    for (i = 0; i < count; i++)
    {
        const char *name = (size == 32) ? cn32_font[i].name : cn24_font[i].name;
        if ((uint8_t)cn[0] == (uint8_t)name[0] &&
            (uint8_t)cn[1] == (uint8_t)name[1] &&
            (uint8_t)cn[2] == (uint8_t)name[2])
        {
            pfont = (size == 32) ? cn32_font[i].data : cn24_font[i].data;
            break;
        }
    }
    if (pfont == 0) return;                  /* 字库中不存在则跳过 */

    ceil8 = (size + 7) / 8;                  /* 每列占字节数 */
    for (c = 0; c < size; c++)               /* 中文字是正方形: 宽=高=size */
    {
        for (r = 0; r < size; r++)
        {
            uint8_t dat = pfont[c * ceil8 + r / 8];
            if ((dat >> (7 - (r % 8))) & 0x01)
            {
                lcd_draw_point(x + c, y + r, color);
            }
        }
    }
}

/* 显示中文字符串(UTF-8): size取24或32 */
void lcd_show_cn_string(uint16_t x, uint16_t y, uint8_t size, const char *str, uint16_t color)
{
    while (*str)
    {
        if (((uint8_t)*str & 0x80) == 0)     /* ASCII 单字节(如空格): 跳过占位 */
        {
            x += size / 2;
            str++;
        }
        else                                 /* 中文: UTF-8 三字节 */
        {
            lcd_show_cn_char(x, y, str, size, color);
            x += size;
            str += 3;
        }
    }
}
