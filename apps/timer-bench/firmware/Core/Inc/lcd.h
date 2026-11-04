#ifndef __LCD_H
#define __LCD_H

#include "main.h"

#define LCD_WIDTH   800
#define LCD_HEIGHT  480

/* 运行时面板分辨率(由屏ID自动识别后填充) */
extern uint16_t g_lcd_width;
extern uint16_t g_lcd_height;

/* RGB565 常用颜色 */
#define WHITE       0xFFFF
#define BLACK       0x0000
#define BLUE        0x001F
#define RED         0xF800
#define GREEN       0x07E0
#define CYAN        0x07FF
#define MAGENTA     0xF81F
#define YELLOW      0xFFE0
#define BROWN       0xBC40
#define BRRED       0xFC07
#define GRAY        0x8430
#define DARKBLUE    0x01CF

void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_show_char(uint16_t x, uint16_t y, char ch, uint8_t size, uint16_t color);
void lcd_show_string(uint16_t x, uint16_t y, uint8_t size, char *str, uint16_t color);
void lcd_show_cn_string(uint16_t x, uint16_t y, uint8_t size, const char *str, uint16_t color);

#endif
