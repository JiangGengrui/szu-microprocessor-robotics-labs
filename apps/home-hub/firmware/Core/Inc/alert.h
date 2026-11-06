#ifndef __ALERT_H
#define __ALERT_H
#include "main.h"

/* 阈值全部用整数(避免 float/_printf_float)：
 * 温度用 0.1℃ 单位, 湿度用 0.1%RH 单位, 电压用 mV */
#define CFG_TEMP_MAX    300     /* 温度上限 30.0℃  */
#define CFG_HUMI_MAX    700     /* 湿度上限 70.0%RH */
#define CFG_VOLT_MAX_MV 3000U   /* 电压上限 mV     */
#define CFG_HYST_TEMP   10      /* 回差 1.0℃       */
#define CFG_HYST_HUMI   20      /* 回差 2.0%RH      */
#define CFG_HYST_VOLT   100U    /* 回差 100mV       */

typedef enum { ALERT_OK = 0, ALERT_LIMIT, ALERT_FAULT } alert_state_t;

void          alert_init(void);
/* temp/humi 传 0.1 度/0.1%RH 的整数(如 25.6℃ -> 256) */
void          alert_poll(int16_t temp, uint8_t temp_ok,
                         int16_t humi, uint8_t humi_ok,
                         uint32_t mv, uint8_t adc_ok);
alert_state_t alert_get(void);       /* 当前状态, 供 UI 显示 */
uint8_t       alert_temp_over(void); /* 各单项是否越限 */
uint8_t       alert_humi_over(void);
uint8_t       alert_volt_over(void);
uint8_t       alert_dht_fault(void);
#endif
