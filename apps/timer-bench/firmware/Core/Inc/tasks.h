#ifndef __TASKS_H
#define __TASKS_H

#include "main.h"

/* timer-bench (定时器与显示) 5个任务入口。
 * 运行约定: main() 主循环反复调用 task_menu() 选任务, 选中后调用对应的 taskN_run();
 *           任务运行中按 KEY1(PH2) 退出(退出前停掉自己用到的外设), 返回后重新进入菜单。
 *           所以 taskN_run() 是"可被 KEY1 中断返回"的循环, 不再是永不返回的死循环。 */
void task1_run(void);   /* 任务1: OLED基础显示          退出: KEY1 */
void task2_run(void);   /* 任务2: 随机反应速度测试器     退出: KEY1 */
void task3_run(void);   /* 任务3: 三色流水呼吸灯         退出: KEY1 */
void task4_run(void);   /* 任务4: 频率与占空比自测仪     退出: KEY1 */
void task5_run(void);   /* 任务5: N次闪光与硬件急停      退出: KEY1 */

/* 任务中断钩子(由 main.c 的 HAL 回调按外设实例号分发调用) */
void task2_on_ms(void);
void task4_on_capture(TIM_HandleTypeDef *htim);
void task5_on_break(void);
/* 任务3 三路均为硬件PWM(TIM3_CH1/CH2 + TIM4_CH1), 不需要中断钩子 */

/* 7寸RGB屏静态横幅(仅 ENABLE_TFT_LCD=1 时使用, 本项目默认不用) */
void lcd_show_banner(void);

#endif
