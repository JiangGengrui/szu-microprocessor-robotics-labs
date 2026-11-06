#ifndef __LCD_UI_H
#define __LCD_UI_H
#include "main.h"
#include "dht11.h"

#define CFG_TITLE     "智能家居中控"
#define CFG_STU_NAME  "EDGE LAB"
#define CFG_STU_ID    "DEMO-0001"

void lcd_ui_init(void);                       /* 画一次固定文字 */

/* 只更新数值区(避免整屏刷新, 防闪烁)。
 * d : DHT11 数据(整数/小数分离, 无 float, 避免 _printf_float);
 *     传 NULL 表示温湿度无效;
 * mv : 电压(mV); raw : ADC 原始值; dht_ok : DHT11 是否在线;
 * cam_st : 摄像头状态字符串(NULL/其他); alert_state : 预警状态 */
void lcd_ui_update(const dht11_data_t *d, uint32_t mv, uint16_t raw,
                   uint8_t dht_ok, const char *cam_st, uint8_t alert_state);
#endif

