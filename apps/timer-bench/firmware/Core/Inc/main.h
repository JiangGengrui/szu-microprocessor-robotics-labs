/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */
extern uint32_t g_pllsain;   /* LTDC像素时钟PLLSAIN, 按屏ID动态选择(main.c定义) */
extern uint32_t g_pllsair;   /* LTDC像素时钟PLLSAIR */
extern uint16_t g_lcd_id;    /* 检测到的RGB屏ID */
/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */
/* timer-bench (定时器与显示) 配置开关 -------------------------------------------------
 * ENABLE_TFT_LCD: 1 = 初始化正点原子7寸RGB屏(LTDC+SDRAM+DMA2D);
 *                 0 = 关闭(默认)。
 *
 * 本项目的显示载体是 0.96" OLED, 5个任务都不需要 RGB 屏。关闭后可去掉
 * "RGB屏 + SDRAM 必须正常"的隐含依赖, 也省掉上电时几十万次外部 SDRAM 写。
 *
 * 注意: LTDC/FMC/DMA2D 已从 exp2.ioc 中移除 —— 它们会把 PA6/PB0/PB1/PB8/PB9/
 * PC7/PD6/PH2/PH3/PH8 抢占为 LTDC 复用引脚, 与 OLED 数据线、按键、TIM 输出冲突。
 * 要恢复 7寸屏演示, 请先在 CubeMX 里重新启用这三个外设并 Generate Code,
 * 再把本宏改成 1。
 *
 * 任务切换: 运行时菜单(见 main.c 的 task_menu()), KEY0 切换 / WK_UP 启动 /
 *           任务内按 KEY1 返回菜单, 无需重新编译。 */
#define ENABLE_TFT_LCD   0
/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
