#ifndef __DHT11_H
#define __DHT11_H
#include "main.h"

typedef struct {
    uint8_t humi_int, humi_dec;   /* 湿度 整数/小数 */
    uint8_t temp_int, temp_dec;   /* 温度 整数/小数 */
    uint8_t valid;                /* 1=有效, 0=失败 */
} dht11_data_t;

void    dht11_init(void);
uint8_t dht11_read(dht11_data_t *d);   /* 0=成功; 1=超时; 2=校验错 */
#endif
