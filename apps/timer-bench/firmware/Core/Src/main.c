/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "gtim.h"
#include "atim.h"
#include "oled.h"
#include "lcd.h"
#include "key.h"
#include <stdio.h>
#include "tasks.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
#if ENABLE_TFT_LCD
DMA2D_HandleTypeDef hdma2d;

LTDC_HandleTypeDef hltdc;

SDRAM_HandleTypeDef hsdram1;
#endif

/* USER CODE BEGIN PV */
uint16_t g_lcd_id = 0;      /* 检测到的RGB屏ID: 0x7084=7寸800x480, 0x7016=7寸1024x600 等 */
uint32_t g_pllsain = 396;   /* 像素时钟PLLSAIN, 按屏ID选择 */
uint32_t g_pllsair = 3;     /* 像素时钟PLLSAIR */
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
#if ENABLE_TFT_LCD
static void MX_DMA2D_Init(void);
static void MX_FMC_Init(void);
static void MX_LTDC_Init(void);
#endif
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
/* 任务选择菜单
 * 上电后、以及每次从任务中退出后都会进入这里。
 *   KEY0(PH3)  -> 切换到下一个任务
 *   WK_UP(PA0) -> 启动选中的任务
 * 返回 1~5, 分别对应 task1_run()~task5_run()。
 * 这样 5 个任务都在同一个固件里, 现场演示不需要改宏/重新编译/重新烧录。 */
static uint8_t task_menu(void)
{
    static const char *task_name[5] = {
        "1 OLED BASE",      /* task1: OLED基础显示 */
        "2 REACT TEST",     /* task2: 随机反应速度测试器 */
        "3 RGB BREATH",     /* task3: 三色流水呼吸灯 */
        "4 PWM METER",      /* task4: 频率与占空比自测仪 */
        "5 N FLASH+BKIN",   /* task5: N次闪光与硬件急停 */
    };
    uint8_t sel = 0;                    /* 0~4 */
    uint8_t need_refresh = 1;
    uint8_t key;

    oled_init();
    key_init();

    while (1)
    {
        if (need_refresh)
        {
            oled_clear();
            oled_show_string(0,  0, (uint8_t *)"SELECT TASK", 16);
            oled_show_string(0, 16, (uint8_t *)"KEY0: NEXT", 16);
            oled_show_string(0, 32, (uint8_t *)"WKUP: START", 16);
            oled_show_string(0, 48, (uint8_t *)task_name[sel], 16);
            oled_refresh_gram();
            need_refresh = 0;
        }

        key = key_scan(0);
        if (key == KEY0)
        {
            sel = (uint8_t)((sel + 1) % 5);
            need_refresh = 1;
        }
        else if (key == WK_UP)
        {
            return (uint8_t)(sel + 1);
        }
        HAL_Delay(20);
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
#if ENABLE_TFT_LCD
  MX_DMA2D_Init();
  MX_FMC_Init();
  MX_LTDC_Init();
#endif
  /* USER CODE BEGIN 2 */
  /* 本项目的显示载体是 0.96" OLED, 由 task_menu() 和各任务自行初始化(oled_init)。
   * 7寸RGB屏(LTDC/SDRAM/DMA2D)只在 ENABLE_TFT_LCD=1 时初始化, 见 main.h。 */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /* 运行时任务选择: KEY0(PH3) 切换任务, WK_UP(PA0) 启动;
     * 任务运行中按 KEY1(PH2) 退出并回到本菜单。
     * 5个任务都在同一个固件里, 不需要改宏/重新编译/重新烧录。 */
    switch (task_menu())
    {
      case 1: task1_run(); break;   /* 任务1: OLED基础显示 */
      case 2: task2_run(); break;   /* 任务2: 随机反应速度测试器 */
      case 3: task3_run(); break;   /* 任务3: 三色流水呼吸灯 */
      case 4: task4_run(); break;   /* 任务4: 频率与占空比自测仪 */
      case 5: task5_run(); break;   /* 任务5: N次闪光与硬件急停 */
      default: break;               /* 不会发生: task_menu() 只返回 1~5 */
    }
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 25;
  RCC_OscInitStruct.PLL.PLLN = 360;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Activate the Over-Drive mode
  */
  if (HAL_PWREx_EnableOverDrive() != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

#if ENABLE_TFT_LCD
/**
  * @brief DMA2D Initialization Function
  * @param None
  * @retval None
  */
static void MX_DMA2D_Init(void)
{

  /* USER CODE BEGIN DMA2D_Init 0 */

  /* USER CODE END DMA2D_Init 0 */

  /* USER CODE BEGIN DMA2D_Init 1 */

  /* USER CODE END DMA2D_Init 1 */
  hdma2d.Instance = DMA2D;
  hdma2d.Init.Mode = DMA2D_M2M;
  hdma2d.Init.ColorMode = DMA2D_OUTPUT_ARGB8888;
  hdma2d.Init.OutputOffset = 0;
  hdma2d.LayerCfg[1].InputOffset = 0;
  hdma2d.LayerCfg[1].InputColorMode = DMA2D_INPUT_ARGB8888;
  hdma2d.LayerCfg[1].AlphaMode = DMA2D_NO_MODIF_ALPHA;
  hdma2d.LayerCfg[1].InputAlpha = 0;
  if (HAL_DMA2D_Init(&hdma2d) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_DMA2D_ConfigLayer(&hdma2d, 1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DMA2D_Init 2 */

  /* USER CODE END DMA2D_Init 2 */

}

/**
  * @brief LTDC Initialization Function
  * @param None
  * @retval None
  */
static void MX_LTDC_Init(void)
{

  /* USER CODE BEGIN LTDC_Init 0 */
  /* 在LTDC复用引脚前读取屏ID: R7/G7/B7(PG6/PI2/PI7)上拉输入读电平编码
   * M2:M1:M0 -> 001=7寸800x480(0x7084), 010=7寸1024x600(0x7016),
   *            100=4.3寸800x480(0x4384), 000=4.3寸480x272(0x4342) */
  {
    GPIO_InitTypeDef gpio_init_struct = {0};
    uint8_t idx = 0;

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOI_CLK_ENABLE();

    gpio_init_struct.Mode  = GPIO_MODE_INPUT;
    gpio_init_struct.Pull  = GPIO_PULLUP;
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;

    gpio_init_struct.Pin = GPIO_PIN_6;              /* PG6 = R7(M0) */
    HAL_GPIO_Init(GPIOG, &gpio_init_struct);
    gpio_init_struct.Pin = GPIO_PIN_2 | GPIO_PIN_7; /* PI2=G7(M1), PI7=B7(M2) */
    HAL_GPIO_Init(GPIOI, &gpio_init_struct);

    idx  = (uint8_t)HAL_GPIO_ReadPin(GPIOG, GPIO_PIN_6);
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_2) << 1;
    idx |= (uint8_t)HAL_GPIO_ReadPin(GPIOI, GPIO_PIN_7) << 2;

    switch (idx)
    {
      case 1:  g_lcd_id = 0x7084; break;  /* 7寸 800x480  */
      case 2:  g_lcd_id = 0x7016; break;  /* 7寸 1024x600 */
      case 4:  g_lcd_id = 0x4384; break;  /* 4.3寸 800x480 */
      case 0:  g_lcd_id = 0x4342; break;  /* 4.3寸 480x272 */
      default: g_lcd_id = 0;       break; /* 未知屏 */
    }
  }
  /* USER CODE END LTDC_Init 0 */

  LTDC_LayerCfgTypeDef pLayerCfg = {0};

  /* USER CODE BEGIN LTDC_Init 1 */
  /* 按屏ID选择面板时序参数与像素时钟(参考正点原子ltdc_init) */
  {
    uint32_t hsw, vsw, hbp, vbp, hfp, vfp, pwidth, pheight;

    if (g_lcd_id == 0x7016)          /* 7寸 1024x600 */
    {
      pwidth = 1024; pheight = 600;
      hsw = 20;  vsw = 3;
      hbp = 140; vbp = 20;
      hfp = 160; vfp = 12;
      g_pllsain = 360; g_pllsair = 2;   /* 像素时钟45MHz: 1M*360/2/4 */
    }
    else if (g_lcd_id == 0x4384)     /* 4.3寸 800x480 */
    {
      pwidth = 800; pheight = 480;
      hsw = 48;  vsw = 3;
      hbp = 88;  vbp = 32;
      hfp = 40;  vfp = 13;
      g_pllsain = 396; g_pllsair = 3;   /* 33MHz */
    }
    else if (g_lcd_id == 0x4342)     /* 4.3寸 480x272 */
    {
      pwidth = 480; pheight = 272;
      hsw = 1;   vsw = 1;
      hbp = 40;  vbp = 8;
      hfp = 5;   vfp = 8;
      g_pllsain = 288; g_pllsair = 4;   /* 9MHz: 1M*288/4/8 */
    }
    else                             /* 默认: 7寸 800x480 (0x7084) */
    {
      pwidth = 800; pheight = 480;
      hsw = 1;   vsw = 1;
      hbp = 46;  vbp = 23;
      hfp = 210; vfp = 22;
      g_pllsain = 396; g_pllsair = 3;   /* 33MHz */
    }

    /* 更新运行时分辨率(lcd.c绘制用) */
    extern uint16_t g_lcd_width, g_lcd_height;
    g_lcd_width  = (uint16_t)pwidth;
    g_lcd_height = (uint16_t)pheight;

    /* LTDC时序寄存器 = 各段累计值-1 */
    hltdc.Init.HorizontalSync      = hsw - 1;
    hltdc.Init.VerticalSync        = vsw - 1;
    hltdc.Init.AccumulatedHBP      = hsw + hbp - 1;
    hltdc.Init.AccumulatedVBP      = vsw + vbp - 1;
    hltdc.Init.AccumulatedActiveW  = hsw + hbp + pwidth - 1;
    hltdc.Init.AccumulatedActiveH  = vsw + vbp + pheight - 1;
    hltdc.Init.TotalWidth          = hsw + hbp + pwidth + hfp - 1;
    hltdc.Init.TotalHeigh          = vsw + vbp + pheight + vfp - 1;
  }
  /* USER CODE END LTDC_Init 1 */
  hltdc.Instance = LTDC;
  hltdc.Init.HSPolarity = LTDC_HSPOLARITY_AL;
  hltdc.Init.VSPolarity = LTDC_VSPOLARITY_AL;
  hltdc.Init.DEPolarity = LTDC_DEPOLARITY_AL;
  hltdc.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
  hltdc.Init.Backcolor.Blue = 0;
  hltdc.Init.Backcolor.Green = 0;
  hltdc.Init.Backcolor.Red = 0;
  if (HAL_LTDC_Init(&hltdc) != HAL_OK)
  {
    Error_Handler();
  }
  pLayerCfg.WindowX0 = 0;
  pLayerCfg.WindowX1 = g_lcd_width;
  pLayerCfg.WindowY0 = 0;
  pLayerCfg.WindowY1 = g_lcd_height;
  pLayerCfg.PixelFormat = LTDC_PIXEL_FORMAT_RGB565;
  pLayerCfg.Alpha = 255;
  pLayerCfg.Alpha0 = 0;
  pLayerCfg.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
  pLayerCfg.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
  pLayerCfg.FBStartAdress = 0xC0000000;
  pLayerCfg.ImageWidth = g_lcd_width;
  pLayerCfg.ImageHeight = g_lcd_height;
  pLayerCfg.Backcolor.Blue = 0;
  pLayerCfg.Backcolor.Green = 0;
  pLayerCfg.Backcolor.Red = 0;
  if (HAL_LTDC_ConfigLayer(&hltdc, &pLayerCfg, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN LTDC_Init 2 */

  /* USER CODE END LTDC_Init 2 */

}

/* FMC initialization function */
static void MX_FMC_Init(void)
{

  /* USER CODE BEGIN FMC_Init 0 */

  /* USER CODE END FMC_Init 0 */

  FMC_SDRAM_TimingTypeDef SdramTiming = {0};

  /* USER CODE BEGIN FMC_Init 1 */

  /* USER CODE END FMC_Init 1 */

  /** Perform the SDRAM1 memory initialization sequence
  */
  hsdram1.Instance = FMC_SDRAM_DEVICE;
  /* hsdram1.Init */
  hsdram1.Init.SDBank = FMC_SDRAM_BANK1;
  hsdram1.Init.ColumnBitsNumber = FMC_SDRAM_COLUMN_BITS_NUM_9;
  hsdram1.Init.RowBitsNumber = FMC_SDRAM_ROW_BITS_NUM_13;
  hsdram1.Init.MemoryDataWidth = FMC_SDRAM_MEM_BUS_WIDTH_16;
  hsdram1.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;
  hsdram1.Init.CASLatency = FMC_SDRAM_CAS_LATENCY_2;
  hsdram1.Init.WriteProtection = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
  hsdram1.Init.SDClockPeriod = FMC_SDRAM_CLOCK_PERIOD_2;
  hsdram1.Init.ReadBurst = FMC_SDRAM_RBURST_ENABLE;
  hsdram1.Init.ReadPipeDelay = FMC_SDRAM_RPIPE_DELAY_0;
  /* SdramTiming */
  SdramTiming.LoadToActiveDelay = 2;
  SdramTiming.ExitSelfRefreshDelay = 7;
  SdramTiming.SelfRefreshTime = 6;
  SdramTiming.RowCycleDelay = 6;
  SdramTiming.WriteRecoveryTime = 4;
  SdramTiming.RPDelay = 2;
  SdramTiming.RCDDelay = 2;

  if (HAL_SDRAM_Init(&hsdram1, &SdramTiming) != HAL_OK)
  {
    Error_Handler( );
  }

  /* USER CODE BEGIN FMC_Init 2 */
  /* SDRAM 鍒濆鍖栧懡浠ゅ簭鍒?(蹇呴渶, 鍚﹀垯SDRAM鏃犳硶璇诲啓) */
  {
    FMC_SDRAM_CommandTypeDef Command = {0};

    /* 1. 鏃堕挓閰嶇疆浣胯兘 */
    Command.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber      = 1;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);
    HAL_Delay(1);                            /* 鑷冲皯寤舵椂 200us */

    /* 2. 瀵规墍鏈夊瓨鍌ㄥ尯棰勫厖鐢? */
    Command.CommandMode            = FMC_SDRAM_CMD_PALL;
    Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber      = 1;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

    /* 3. 鑷埛鏂? 8 娆? */
    Command.CommandMode            = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber      = 8;
    Command.ModeRegisterDefinition = 0;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

    /* 4. 鍔犺浇妯″紡瀵勫瓨鍣?:
     *    bit9=1 鍐欑獊鍙戞ā寮忓崟鐐?, bit6~4=010 CAS=2, 鍏朵綑0
     *    绐佸彂闀垮害1(浣?2~0=000) + 椤哄簭绐佸彂(浣?3=0) + 鏍囧噯妯″紡(浣?8~7=00) */
    Command.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    Command.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    Command.AutoRefreshNumber      = 1;
    Command.ModeRegisterDefinition = 0x220;
    HAL_SDRAM_SendCommand(&hsdram1, &Command, 0x1000);

    /* 5. 璁剧疆鍒锋柊棰戠巼: COUNT = 64ms*1000*96MHz/8192琛? - 20 = 730 */
    HAL_SDRAM_ProgramRefreshRate(&hsdram1, 730);
  }
  /* USER CODE END FMC_Init 2 */
}
#endif /* ENABLE_TFT_LCD */

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOE_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOI_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOG_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* HAL 定时器回调: 按外设实例号分发到对应任务的钩子。
 * 三个外设互不重叠(TIM6=任务2 / TIM4=任务4 / TIM1=任务5), 所以可以常规定义,
 * 不再依赖编译期的任务宏; 只有被实际初始化的外设才会产生中断。 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM6) task2_on_ms();
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM4) task4_on_capture(htim);
}

void HAL_TIMEx_BreakCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM1) task5_on_break();
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
