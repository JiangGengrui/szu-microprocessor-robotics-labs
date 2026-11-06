#ifndef __KEY_H
#define __KEY_H
#include "main.h"
#define KEY0   1
#define KEY1   2
#define KEY2   3
#define WK_UP  4
void    key_init(void);
uint8_t key_scan(void);     /* 0=无; 单击(松手再按才算新一次) */
#endif
