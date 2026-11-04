#include "tasks.h"
#include "lcd.h"

/* LCD 静态横幅: 7寸屏白底黑字居中显示 项目名称/设备标识
 * 三行内容, 按运行时分辨率 g_lcd_width/g_lcd_height 居中
 * (UTF-8中文字符串, 见 chinesefont.h 的 24/32 号字库)
 */
void lcd_show_banner(void)
{
    uint16_t w = g_lcd_width, h = g_lcd_height;
    uint16_t y0 = (h > 116) ? (h - 116) / 2 : 0;   /* 内容块总高116px, 垂直居中 */

    lcd_clear(WHITE);
    lcd_show_cn_string((w - 6*32) / 2, y0,       32, "定时器与显示", BLACK); /* 6字x32=192 */
    lcd_show_string   ((w - 8*12) / 2, y0 + 44,  24, (char *)"EDGE LAB",  BLACK); /* 8字符x12=96 */
    lcd_show_string   ((w - 9*12) / 2, y0 + 80, 24, (char *)"DEMO-0001",  BLACK); /* 9字符x12=108 */
}
