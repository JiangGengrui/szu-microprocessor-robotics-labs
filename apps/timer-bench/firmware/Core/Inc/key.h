#ifndef __KEY_H
#define __KEY_H

#include "main.h"

/* 按键定义: KEY0=PH3, KEY1=PH2 (上拉, 按下=0); WKUP=PA0 (下拉, 按下=1) */
#define KEY0    1
#define KEY1    2
#define WK_UP   3

void    key_init(void);
uint8_t key_scan(uint8_t mode);   /* mode0: 单击(松开前只报一次); mode1: 支持连按 */

#endif
