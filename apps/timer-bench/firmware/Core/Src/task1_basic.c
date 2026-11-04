#include "tasks.h"
#include "oled.h"
#include "key.h"

/* 任务1: OLED 基础显示
 * 要求: OLED初始化/清屏/字符/字符串/十进制数字显示;
 *       屏幕显示项目名称与设备标识, 并动态更新递增计数值;
 *       底层写命令/写数据与上层文本显示分离, 封装清屏/字符串/整数三个接口。
 * 实现: oled.c 已实现底层分离(oled_wr_byte/oled_data_out 为底层,
 *       oled_clear/oled_show_string/oled_show_int 为上层接口)。
 * 布局: 128x64, 每行16px共4行。
 * 退出: 运行中按 KEY1(PH2) 返回任务菜单。
 */
void task1_run(void)
{
    uint32_t count = 0;

    oled_init();
    key_init();
    oled_clear();                                        /* 接口1: 清屏 */

    oled_show_cn16(16, 0, "定时器与显示");              /* 6字x16=96px, 居中 */
    oled_show_string(0, 16, (uint8_t *)"EDGE LAB", 16);
    oled_show_string(0, 32, (uint8_t *)"DEMO-0001", 16);  /* 接口2: 指定位置显示字符串 */
    oled_refresh_gram();

    while (1)
    {
        uint8_t i;

        oled_show_cn16(0, 48, "计数:");
        oled_show_int(48, 48, (int32_t)count, 16);      /* 接口3: 显示整数 */
        oled_refresh_gram();
        count++;

        /* 计数每0.5s递增一次; 期间以10ms间隔扫键, 便于按KEY1退出到任务菜单 */
        for (i = 0; i < 50; i++)
        {
            if (key_scan(0) == KEY1) return;
            HAL_Delay(10);
        }
    }
}
