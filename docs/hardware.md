# 硬件与接线

## 1. 物料清单

| 部件 | 型号 | 数量 | 用于 | 备注 |
|------|------|:----:|------|------|
| 核心板 | 正点原子 阿波罗 V2（STM32F429IGT6） | 1 | 全部 | Cortex-M4F，180 MHz，1 MiB Flash / 192 KiB SRAM |
| 扩展底板 | DNF429-EB | 1 | 全部 | 承载 7 寸屏接口、OV5640 座、SDRAM、NOR Flash、DHT11、RV1 |
| RGB 屏 | 7 寸 1024×600 RGB565，40 pin FPC | 1 | home-hub / face-id | LTDC 直驱，不需要驱动 IC |
| 摄像头 | OV5640 模块（DVP 8 bit） | 1 | home-hub / face-id | 插 P6；SCCB 地址 0x3C（写 0x78） |
| 温湿度传感器 | DHT11 | 1 | home-hub | 单总线，需 4.7 kΩ 上拉 |
| 调试器 | CMSIS-DAP（DAP-Link） | 1 | 全部 | 不支持 ST-Link 的 `st-flash` 流程 |
| 面包板 | 830 孔 | 1 | led-marquee / timer-bench | — |
| LED | 3 mm 直插，红/绿/蓝/黄 | 8 + 3 | led-marquee / timer-bench | 长脚为正极 |
| 电阻 | 1 kΩ，1/4 W | 11 | led-marquee / timer-bench | 每个 LED 串一个 |
| 按键 | 6×6 直插 | 4（可选） | led-marquee | 板上已有 KEY0/1/2 与 WK_UP，可不外接 |
| 杜邦线 | 公对公 / 母对母 | 若干 | 全部 | 劣质线是常见故障源 |

> 底板已集成 SDRAM、NOR Flash、PCF8574、DHT11 座、RV1 电位器与 7 寸屏接口，因此除面包板部分外不需要额外模块。

## 2. 板载资源

| 资源 | 连接 | 说明 |
|------|------|------|
| SDRAM | FMC Bank1，32 MiB | 帧缓冲与算法工作区 |
| NOR Flash | SPI5 | 25 系列，人脸图库 |
| PCF8574 | I²C2（PH4=SCL / PH5=SDA，100 kHz） | 7 位地址 0x20；P0 接蜂鸣器，P1~P7 接网络复位等，**写其余位必须保持 1** |
| 板载 LED | LED0=PB1（红）、LED1=PB0（绿） | 低电平点亮 |
| 按键 | KEY0=PH3、KEY1=PH2、KEY2=PC13、WK_UP=PA0 | 前三者低有效（内部上拉），WK_UP 高有效（内部下拉） |
| 串口 | USART1 = PA9/PA10（CH340） | 115200-8-N-1，可选调试输出 |
| SWD | PA13 / PA14 | 烧录与调试 |

## 3. 各应用引脚

### led-marquee

| 功能 | 引脚 | 有效电平 |
|------|------|----------|
| LED0 ~ LED7 | PG14 / PA1 / PC4 / PH13 / PH15 / PI1 / PG11 / PI5 | 低电平点亮 |
| 蜂鸣器 | PCF8574 P0 | 写 `0xFE` 鸣叫，`0xFF` 停止 |

接线：`3.3 V → 1 kΩ → LED 正极 → 负极 → GPIO`。

### timer-bench

| 功能 | 定时器 / 通道 | 引脚 | 参数 |
|------|---------------|------|------|
| 1 ms 基准 | TIM6 | 内部 | PSC=89，ARR=999 @90 MHz |
| 红 / 绿 PWM | TIM3_CH1 / CH2 | PA6 / PA7 | 1 kHz |
| 蓝 PWM | TIM4_CH1 | PB6 | 1 kHz |
| 频率/占空比输出 | TIM3_CH1 | PA6 | PSC=89，ARR=9999 / 999 / 99 |
| 频率/占空比捕获 | TIM4_CH1（PWM Input） | PB6 | PSC=89，ARR=0xFFFF |
| N 次闪光 | TIM1_CH1 | PA8 | PSC=17999，ARR=4999 → 2 Hz |
| 硬件急停 | TIM1 BKIN | PA6 | 低有效 |
| OLED | 8080 并口 | 见 `oled.c` | 0.96"，128×64 |

PA6/PB6 在子应用间分时复用：进入某个子应用前先反初始化上一个占用的外设。

### home-hub

| 功能 | 接口 | 引脚 | 说明 |
|------|------|------|------|
| DHT11 | 单总线 | PB12 | 开漏 + 上拉，采样间隔 ≥1 s |
| 电压表 | ADC1_IN5 | PA5 | RV1 经杜邦线接入，0~3.3 V |
| RGB LCD | LTDC 20 线 | 见板级引脚表 | AF14，GPIO HIGH/VERY_HIGH |
| 摄像头 D0~D7 | DCMI | PC6~PC9、PC11、PD3、PB8、PB9 | AF13 |
| 摄像头 SCCB | 软件 IIC | PB4=SCL / PB3=SDA | 读 ID 0x5640 |

### face-id

| 功能 | 接口 | 引脚 | 说明 |
|------|------|------|------|
| 摄像头 | DCMI + DMA2_Stream1 | 同上 | 1024×600 RGB565 |
| LCD | LTDC Layer1 | — | 帧缓冲 0xC0000000 |
| NOR Flash | SPI5 | PB2=SCK / PB14=MISO / PB15=MOSI / PB12=CS | 25 系列 |

## 4. 电源与信号注意

- **共地**：面包板与外接模块必须与核心板共地，否则按键与 PWM 行为不可预测。
- **限流**：任何直插 LED 必须串 1 kΩ；直接接 3.3 V 会烧 LED 或拉低端口。
- **禁止输出对输出**：把 PWM 输出跳接到捕获输入前，确认该引脚已配为复用输入，不可让两个推挽输出直连。
- **BKIN 只做逻辑演示**：接 3.3 V 电平，不接电机或功率负载。
- **ADC 量程**：只测 RV1 的 0~3.3 V，不要接 5 V 或未知电压。
- **插拔顺序**：摄像头与屏排线必须在断电状态下插拔，并按丝印确认方向。

## 5. 常见硬件故障定位

| 现象 | 优先检查 |
|------|----------|
| 个别 LED 不亮 | 该路杜邦线是否内部断线（换线验证）、LED 极性、电阻是否虚接 |
| 上电无任何反应 | BOOT0 是否在 0/GND、电源指示灯、DAP 是否被识别 |
| 屏背光亮但黑屏 | LTDC 引脚映射是否与板上走线一致、像素时钟与层参数 |
| 摄像头读不到 ID | P6 是否插反、SCCB 上拉、模块供电时序 |
| 按键一直触发 | 有效电平方向写反（WK_UP 与其余三键相反） |
| 蜂鸣器长鸣不停 | PCF8574 写入时把 P1~P7 置 0，误动了其他外设 |
