#include "lcd_ui.h"
#include "lcd.h"
#include "alert.h"
#include <stdio.h>

/* 数值区固定布局(避免整屏刷新) */
void lcd_ui_init(void)
{
    lcd_clear(WHITE);
    /* 标题"智能家居中控"7 字 x 32px = 224px, 1024 屏居中 x=(1024-224)/2=400 */
    lcd_show_cn_string(400, 20, 32, CFG_TITLE, BLACK);
    /* 设备标识: 名称 8 字符 x 12px = 96px, 编号 9 字符 x 12px = 108px */
    lcd_show_string(390, 70, 24, (char *)CFG_STU_NAME, BLACK);
    lcd_show_string(510, 70, 24, (char *)CFG_STU_ID, BLACK);

    lcd_show_string(40, 130, 24, (char *)"TEMP:", BLACK);
    lcd_show_string(40, 170, 24, (char *)"HUMI:", BLACK);
    lcd_show_string(40, 210, 24, (char *)"VOLT:", BLACK);
    lcd_show_string(40, 250, 24, (char *)"ADC :", BLACK);
    lcd_show_string(40, 300, 24, (char *)"LIMIT T30 H70 V3000mV", BLACK);
}

/* 只更新数值区: 先填同色背景覆盖旧值(避免透明字符堆叠), 再写新值。
 * 温度/湿度用"整数.小数"分离显示, 全程整数格式化, 不用 float/%f(省 Flash 且无 _printf_float 报错) */
void lcd_ui_update(const dht11_data_t *d, uint32_t mv, uint16_t raw,
                   uint8_t dht_ok, const char *cam_st, uint8_t alert_state)
{
    char s[28];

    /* 温度/湿度数值区(x=170 起, 每行宽 96px, 高 24px) */
    if (d && d->valid) {
        lcd_fill(170, 130, 96, 24, WHITE);
        sprintf(s, "%d.%d C", d->temp_int, d->temp_dec);
        lcd_show_string(170, 130, 24, s, BLACK);

        lcd_fill(170, 170, 96, 24, WHITE);
        sprintf(s, "%d.%d %%", d->humi_int, d->humi_dec);
        lcd_show_string(170, 170, 24, s, BLACK);
    } else {
        lcd_fill(170, 130, 96, 24, WHITE);
        lcd_show_string(170, 130, 24, (char *)"--.- C", BLACK);
        lcd_fill(170, 170, 96, 24, WHITE);
        lcd_show_string(170, 170, 24, (char *)"--.- %", BLACK);
    }

    /* 电压数值区 */
    lcd_fill(170, 210, 96, 24, WHITE);
    sprintf(s, "%lu.%02lu V", (unsigned long)(mv / 1000),
            (unsigned long)(mv % 1000 / 10));        /* 电压固定 2 位小数 */
    lcd_show_string(170, 210, 24, s, BLACK);

    /* ADC 原始值 */
    lcd_fill(170, 250, 96, 24, WHITE);
    sprintf(s, "%u", raw);
    lcd_show_string(170, 250, 24, s, BLACK);

    /* 状态行: DHT 状态(左) + 预警状态(右) */
    lcd_fill(40, 360, 240, 24, WHITE);
    sprintf(s, "DHT:%s", dht_ok ? "OK" : "FAIL");
    lcd_show_string(40, 360, 24, s, BLACK);

    lcd_fill(560, 360, 160, 24, WHITE);
    const char *st = (alert_state == ALERT_OK)   ? "NORMAL" :
                     (alert_state == ALERT_LIMIT) ? "OVER!"  : "FAULT!";
    lcd_show_string(560, 360, 24, (char *)st, BLACK);

    /* 摄像头状态/诊断独立一行(空间足够放完整诊断串) */
    lcd_fill(40, 400, 480, 24, WHITE);
    sprintf(s, "CAM:%s", cam_st ? cam_st : "?");
    lcd_show_string(40, 400, 24, s, BLACK);
}
