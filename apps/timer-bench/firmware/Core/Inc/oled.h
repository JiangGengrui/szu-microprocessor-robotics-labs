#ifndef __OLED_H
#define __OLED_H

#include "main.h"

#define OLED_CMD  0
#define OLED_DATA 1

/* 控制引脚: RS(DC)=PB4, CS=PB7, WR=PH8, RD=PB3, RST=PA15 */
#define OLED_RS(x)   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4,  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_CS(x)   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_7,  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_WR(x)   HAL_GPIO_WritePin(GPIOH, GPIO_PIN_8,  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_RD(x)   HAL_GPIO_WritePin(GPIOB, GPIO_PIN_3,  (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)
#define OLED_RST(x)  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_15, (x) ? GPIO_PIN_SET : GPIO_PIN_RESET)

void oled_init(void);
void oled_refresh_gram(void);
void oled_draw_point(uint8_t x, uint8_t y, uint8_t dot);
void oled_clear(void);
void oled_show_char(uint8_t x, uint8_t y, uint8_t chr, uint8_t size, uint8_t mode);
void oled_show_string(uint8_t x, uint8_t y, uint8_t *p, uint8_t size);
void oled_show_num(uint8_t x, uint8_t y, uint32_t num, uint8_t len, uint8_t size);
void oled_show_cn16(uint8_t x, uint8_t y, const char *str);   /* 16x16中文(UTF-8), 需调refresh刷新 */
void oled_show_int(uint8_t x, uint8_t y, int32_t num, uint8_t size); /* 有符号整数显示接口 */

#endif
