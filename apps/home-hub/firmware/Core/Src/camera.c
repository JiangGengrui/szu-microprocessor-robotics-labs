#include "camera.h"
#include "lcd.h"
#include "ov5640cfg.h"
#include <string.h>

/* =========================================================================
 * OV5640 摄像头驱动 —— 移植自正点原子阿波罗F429 摄像头例程(第50章)
 *  - PWDN : PCF8574 的 P2(大底板 DNF429P 上), 经硬件 I2C2(PH4/PH5), 7位地址 0x20
 *           ★ PCF8574 上电默认全高 = 摄像头掉电, 必须显式拉低 P2 上电 ★
 *  - RESET: PA15(推挽输出)
 *  - SCCB : 软件 IIC, PB4(SCL 推挽) / PB3(SDA 开漏), 大底板有 4.7K 上拉
 *  - 数据 : DCMI 8 位 RGB565, PIXCLK=PA6, VSYNC=PB7, HSYNC=PH8
 *           (引脚由 CubeMX 的 HAL_DCMI_MspInit 配置)
 * ========================================================================= */

/* ---- SCCB 引脚 ---- */
#define SCCB_SCL_PORT   GPIOB
#define SCCB_SCL_PIN    GPIO_PIN_4
#define SCCB_SDA_PORT   GPIOB
#define SCCB_SDA_PIN    GPIO_PIN_3

#define SCCB_SCL_H()    HAL_GPIO_WritePin(SCCB_SCL_PORT, SCCB_SCL_PIN, GPIO_PIN_SET)
#define SCCB_SCL_L()    HAL_GPIO_WritePin(SCCB_SCL_PORT, SCCB_SCL_PIN, GPIO_PIN_RESET)
#define SCCB_SDA_H()    HAL_GPIO_WritePin(SCCB_SDA_PORT, SCCB_SDA_PIN, GPIO_PIN_SET)
#define SCCB_SDA_L()    HAL_GPIO_WritePin(SCCB_SDA_PORT, SCCB_SDA_PIN, GPIO_PIN_RESET)
#define SCCB_READ()     HAL_GPIO_ReadPin(SCCB_SDA_PORT, SCCB_SDA_PIN)

/* ---- OV5640 ---- */
#define OV5640_ID       0x5640
#define OV5640_ADDR     0x78         /* 8 位写地址(7 位 0x3C << 1) */
#define OV5640_CHIPIDH  0x300A
#define OV5640_CHIPIDL  0x300B
#define OV_RESET_PORT   GPIOA
#define OV_RESET_PIN    GPIO_PIN_15

/* 精确微秒延时(用 DWT 计数器, 180MHz 主频下 1us=180 计数) */
static void cam_delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < ticks) { }
}

/* ===================== PCF8574(硬件 I2C2) =====================
 * 大底板 DNF429P 上的 PCF8574T, 7 位地址为 0x20(A0A1A2 全接地)。
 * STM32F4 HAL 的 DevAddress 参数要求传入已经左移一位的地址，
 * 因此这里必须使用 0x20 << 1 = 0x40。 */
#define PCF8574_ADDR    (0x20U << 1)

/* PCF8574 读回值(诊断 PWDN): 正常写 0xFB 后应读回 0xFB(P2=0); 0xEE=读失败 */
uint8_t g_cam_pcf = 0;

/* I2C 总线恢复: 若总线被拉死, 手动发 9 个 SCL 时钟 + STOP 解锁 */
static void i2c_bus_recover(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOH_CLK_ENABLE();
    g.Pin = GPIO_PIN_4 | GPIO_PIN_5;
    g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOH, &g);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, GPIO_PIN_SET);   /* SDA 高 */
    for (int i = 0; i < 9; i++)                            /* 9 个 SCL 脉冲 */
    {
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_SET);
        cam_delay_us(5);
        HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_RESET);
        cam_delay_us(5);
    }
    /* STOP: SDA 低->高 while SCL 高 */
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, GPIO_PIN_RESET); cam_delay_us(5);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_4, GPIO_PIN_SET);   cam_delay_us(5);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_5, GPIO_PIN_SET);   cam_delay_us(5);
    /* 重新交给 I2C2 复用(再次调用 HAL_I2C_Init 会重配 MSP) */
    HAL_I2C_Init(&hi2c2);
}

/* 写探测: 0x20 有应答即认为 PCF8574 在位(已验证可行的方式) */
static uint8_t pcf8574_probe(void)
{
    uint8_t v = 0xFF;
    if (HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &v, 1, 100) != HAL_OK)
        return 1;
    return 0;
}

static void pcf8574_write_byte(uint8_t val)
{
    HAL_I2C_Master_Transmit(&hi2c2, PCF8574_ADDR, &val, 1, 100);
    HAL_Delay(10);
}

/* 读回 PCF8574 当前输出锁存值(诊断 PWDN 是否真被拉低) */
static void pcf8574_readback(void)
{
    uint8_t v = 0;
    if (HAL_I2C_Master_Receive(&hi2c2, PCF8574_ADDR, &v, 1, 100) == HAL_OK)
        g_cam_pcf = v;
    else
        g_cam_pcf = 0xEE;      /* 读失败标记 */
}

static void pcf8574_write_bit(uint8_t bit, uint8_t sta)
{
    uint8_t data = 0xFF;
    data &= ~(1u << bit);
    if (sta) data |= (1u << bit);
    pcf8574_write_byte(data);
}


/* ===================== SCCB 软件 IIC ===================== */
static void sccb_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();

    g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = SCCB_SCL_PIN;
    HAL_GPIO_Init(SCCB_SCL_PORT, &g);          /* SCL 推挽 */

    g.Mode = GPIO_MODE_OUTPUT_OD; g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    g.Pin = SCCB_SDA_PIN;
    HAL_GPIO_Init(SCCB_SDA_PORT, &g);          /* SDA 开漏 */

    SCCB_SCL_H(); SCCB_SDA_H();
}

static void sccb_start(void)
{
    SCCB_SDA_H(); SCCB_SCL_H(); cam_delay_us(5);
    SCCB_SDA_L(); cam_delay_us(5);
    SCCB_SCL_L();
}

static void sccb_stop(void)
{
    SCCB_SDA_L(); cam_delay_us(5);
    SCCB_SCL_H(); cam_delay_us(5);
    SCCB_SDA_H(); cam_delay_us(5);
}

static uint8_t sccb_send_byte(uint8_t dat)
{
    uint8_t i, ack;
    for (i = 0; i < 8; i++)
    {
        (dat & 0x80) ? SCCB_SDA_H() : SCCB_SDA_L();
        dat <<= 1;
        cam_delay_us(5);
        SCCB_SCL_H(); cam_delay_us(5);
        SCCB_SCL_L();
    }
    SCCB_SDA_H(); cam_delay_us(5);             /* 释放 SDA, 读 ACK */
    SCCB_SCL_H(); cam_delay_us(5);
    ack = SCCB_READ();
    SCCB_SCL_L();
    return ack;                                /* 0=ACK, 1=NACK */
}

static uint8_t sccb_read_byte(void)
{
    uint8_t i, dat = 0;
    SCCB_SDA_H();
    for (i = 0; i < 8; i++)
    {
        SCCB_SCL_H(); cam_delay_us(5);
        dat = (dat << 1) | SCCB_READ();
        SCCB_SCL_L(); cam_delay_us(5);
    }
    return dat;
}

/* 读字节后发 NACK: SCL 高时 SDA 保持高(第9位时钟) */
static void sccb_nack(void)
{
    SCCB_SDA_H();
    cam_delay_us(5);
    SCCB_SCL_H();
    cam_delay_us(5);
    SCCB_SCL_L();
    cam_delay_us(5);
}

/* SCCB 3-Phase Write: 每相第9位是 Don't-Care, 主机不得据其判断 ACK(SCCB规范3.2.2.1),
 * 故写操作不检查返回值, 只保证把 9 个时钟打完整。 */
static void sccb_write_reg(uint16_t reg, uint8_t val)
{
    sccb_start();
    sccb_send_byte(OV5640_ADDR);
    sccb_send_byte(reg >> 8);
    sccb_send_byte(reg & 0xFF);
    sccb_send_byte(val);
    sccb_stop();
}

static uint8_t sccb_read_reg(uint16_t reg)
{
    uint8_t val;
    sccb_start();
    sccb_send_byte(OV5640_ADDR);
    sccb_send_byte(reg >> 8);
    sccb_send_byte(reg & 0xFF);
    sccb_stop();

    sccb_start();
    sccb_send_byte(OV5640_ADDR | 0x01);
    val = sccb_read_byte();
    sccb_nack();                    /* 单字节读后发 NACK(官方做法) */
    sccb_stop();
    return val;
}

static void ov5640_write_reg(uint16_t reg, uint8_t val)
{
    sccb_write_reg(reg, val);
}

static uint8_t ov5640_read_reg(uint16_t reg)
{
    return sccb_read_reg(reg);
}

/* 设置 OV5640 输出尺寸(官方组3写法): 输出 w x h, 取图偏移 offx/offy */
static void ov5640_outsize_set(uint16_t offx, uint16_t offy, uint16_t w, uint16_t h)
{
    ov5640_write_reg(0x3212, 0x03);            /* 开始组3 */
    ov5640_write_reg(0x3808, w >> 8);          /* 输出宽度高字节 */
    ov5640_write_reg(0x3809, w & 0xFF);        /* 输出宽度低字节 */
    ov5640_write_reg(0x380a, h >> 8);          /* 输出高度高字节 */
    ov5640_write_reg(0x380b, h & 0xFF);        /* 输出高度低字节 */
    ov5640_write_reg(0x3810, offx >> 8);       /* X offset */
    ov5640_write_reg(0x3811, offx & 0xFF);
    ov5640_write_reg(0x3812, offy >> 8);       /* Y offset */
    ov5640_write_reg(0x3813, offy & 0xFF);
    ov5640_write_reg(0x3212, 0x13);            /* 结束组3 */
    ov5640_write_reg(0x3212, 0xa3);            /* 启用组3设置 */
}

/* ===================== 对外接口 ===================== */
static volatile uint8_t g_frame_ready = 0;

/* 行缓冲(双缓冲): 第 0/1 块各 CAM_LINE_WORDS 个 32 位字 = 一整行 1024 像素 */
static uint32_t g_cam_line_buf[2][CAM_LINE_WORDS];
/* 当前写入帧缓冲的行号(0 ~ LCD_HEIGHT-1) */
static volatile uint16_t g_cam_curline = 0;

/* ---- 三缓冲显示(彻底消除撕裂/滚动) ----
 * 若逐行直写"正在被 LTDC 扫描的帧缓冲", 写入与扫描不同步 → 画面撕裂;
 * 而帧中断丢失会让行号不再按 VSYNC 归零 → 行号漂移 → 画面滚动。
 * 改为三缓冲轮转:
 *  - 摄像头逐行写入"后缓冲"(g_cam_wbuf), 后缓冲永不正在显示;
 *  - 一帧写满(FRAME 中断, 与 VSYNC 同步)后, 请求 LTDC 在下一个垂直消隐期(VBR)
 *    切换到刚写满的缓冲显示 —— 换页只发生在帧间隙, 屏幕每帧都是完整画面, 无撕裂;
 *  - 行号在 FRAME 中断里归零, 与 VSYNC 严格同步 → 无滚动。
 * 三块缓冲放在 SDRAM 空闲区(避开 0xC0000000 的 LCD 主帧缓冲)。 */
#define CAM_FB_SIZE     (LCD_WIDTH * LCD_HEIGHT)        /* 像素数(半字数) */
#define CAM_FB0_ADDR    0xC0300000UL                    /* 每块间隔 0x140000(1.25MB) > 1.2MB */
#define CAM_FB1_ADDR    0xC0440000UL
#define CAM_FB2_ADDR    0xC0580000UL

static uint16_t *const g_cam_fb[3] =
{
    (uint16_t *)CAM_FB0_ADDR,
    (uint16_t *)CAM_FB1_ADDR,
    (uint16_t *)CAM_FB2_ADDR,
};
static volatile uint8_t g_cam_wbuf = 1;   /* 正在写入的后缓冲号(0~2) */
static volatile uint8_t g_cam_disp = 0;   /* 正在显示的缓冲号(0~2) */

/* DMA2D 硬件拷贝一行 RGB565 像素(内存到内存)。
 * 之前用 CPU memcpy, 在 -O0 编译 + SDRAM 读写带宽竞争下, 拷贝一行 2048 字节
 * 的速度赶不上摄像头行速率(约 36us/行), 行中断积压, 每帧只有约 2/3 的行被
 * 及时写入, 剩余 1/3 区域显示残留数据并闪烁。DMA2D 硬件突发传输 + 不占 CPU,
 * 速度远快于 memcpy, 与官方例程的 ltdc_color_fill 一致。 */
static void cam_line_copy(uint16_t *dst, const uint16_t *src)
{
    uint32_t timeout = 0;

    if ((RCC->AHB1ENR & RCC_AHB1ENR_DMA2DEN) == 0)
        RCC->AHB1ENR |= RCC_AHB1ENR_DMA2DEN;        /* 使能 DMA2D 时钟(仅首次) */

    DMA2D->CR      = 0x00000000;                    /* MODE=00 内存到内存 */
    DMA2D->FGPFCCR = 2;                             /* 源颜色模式 = RGB565 */
    DMA2D->OPFCCR  = 2;                             /* 目标颜色模式 = RGB565 */
    DMA2D->FGMAR   = (uint32_t)src;                 /* 源地址 */
    DMA2D->OMAR    = (uint32_t)dst;                 /* 目标地址 */
    DMA2D->FGOR    = 0;                             /* 源行偏移 */
    DMA2D->OOR     = 0;                             /* 目标行偏移 */
    DMA2D->NLR     = ((uint32_t)LCD_WIDTH << 16) | 1UL; /* NL=1 行, PL=LCD_WIDTH 像素/行 */
    DMA2D->CR      = DMA2D_CR_START;                /* 启动传输 */
    while ((DMA2D->ISR & DMA2D_ISR_TCIF) == 0)      /* 等待完成 */
    {
        if (++timeout > 0xFFFFF) break;
    }
    DMA2D->IFCR = DMA2D_IFCR_CTCIF;                 /* 清完成标志 */
}

/* 最近一次初始化的详细错误码(供调试, 0=成功) */
uint8_t g_cam_err = 0;
/* 读回的 OV5640 芯片 ID 实际值(供调试, 应=0x5640) */
uint16_t g_cam_id = 0;
/* SCCB 诊断: bit0=SCCB空闲SCL电平, bit1=空闲SDA电平, bit2=写地址0x78的ACK(0=ACK) */
uint8_t g_sccb_diag = 0;

uint8_t camera_init(void)
{
    uint16_t id = 0, i;

    /* 使能 DWT 周期计数器(用于 SCCB 精确延时) */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 0. I2C 总线恢复(防上次异常遗留 BUSY) + PCF8574 探测 */
    if (pcf8574_probe() != 0)
    {
        i2c_bus_recover();                      /* 总线解锁后重试一次 */
        if (pcf8574_probe() != 0) { g_cam_err = 1; return CAM_ERR_PCF8574; }
    }
    pcf8574_write_byte(0xFF);                   /* 默认全高(摄像头先掉电) */

    /* 1. RESET 引脚 */
    {
        GPIO_InitTypeDef g = {0};
        __HAL_RCC_GPIOA_CLK_ENABLE();
        g.Pin = OV_RESET_PIN; g.Mode = GPIO_MODE_OUTPUT_PP;
        g.Pull = GPIO_PULLUP; g.Speed = GPIO_SPEED_FREQ_HIGH;
        HAL_GPIO_Init(OV_RESET_PORT, &g);
    }

    /* 2. 复位时序: RST 低 -> PWDN 上电(P2=0) -> RST 高 */
    HAL_GPIO_WritePin(OV_RESET_PORT, OV_RESET_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    pcf8574_write_bit(2, 0);                    /* P2=0 -> POWER ON */
    pcf8574_readback();                         /* 诊断: 应读回 0xFB */
    HAL_Delay(20);                              /* OV5640 内部 LDO 稳定 */
    HAL_GPIO_WritePin(OV_RESET_PORT, OV_RESET_PIN, GPIO_PIN_SET);
    HAL_Delay(20);

    /* 3. SCCB 初始化 + 总线电平自检 */
    sccb_init();
    HAL_Delay(10);

    /* 自检: SCCB 空闲时 SCL/SDA 都应为高(4.7K 上拉); 若为低说明总线被拉死 */
    g_sccb_diag = 0;
    if (HAL_GPIO_ReadPin(SCCB_SCL_PORT, SCCB_SCL_PIN)) g_sccb_diag |= 0x01;
    if (HAL_GPIO_ReadPin(SCCB_SDA_PORT, SCCB_SDA_PIN)) g_sccb_diag |= 0x02;

    /* 探测: 只发设备地址看 ACK(不写寄存器), 判断摄像头是否在线 */
    sccb_start();
    {
        uint8_t ack = sccb_send_byte(OV5640_ADDR);   /* 0=ACK */
        if (ack == 0) g_sccb_diag |= 0x04;
    }
    sccb_stop();

    /* 上电后先软复位一次, 让 OV5640 寄存器恢复默认 */
    ov5640_write_reg(0x3008, 0x82);             /* 软复位 */
    HAL_Delay(20);

    id  = (uint16_t)ov5640_read_reg(OV5640_CHIPIDH) << 8;
    id |= ov5640_read_reg(OV5640_CHIPIDL);
    g_cam_id = id;
    if (id != OV5640_ID)
    {
        /* 失败重试一次(时序抖动/首读不稳定) */
        HAL_Delay(20);
        id  = (uint16_t)ov5640_read_reg(OV5640_CHIPIDH) << 8;
        id |= ov5640_read_reg(OV5640_CHIPIDL);
        g_cam_id = id;
    }
    if (id != OV5640_ID) { g_cam_err = 2; return CAM_ERR_ID; }

    /* 5. 初始化序列(官方流程): 软复位 -> 写 init 表 */
    ov5640_write_reg(0x3103, 0x11);            /* system clock from pad */
    ov5640_write_reg(0x3008, 0x82);            /* 软复位 */
    HAL_Delay(10);
    for (i = 0; i < sizeof(ov5640_init_reg_tbl) / 4; i++)
        ov5640_write_reg(ov5640_init_reg_tbl[i][0], ov5640_init_reg_tbl[i][1]);

    /* 6. RGB565 模式 */
    for (i = 0; i < sizeof(ov5640_rgb565_reg_tbl) / 4; i++)
        ov5640_write_reg(ov5640_rgb565_reg_tbl[i][0], ov5640_rgb565_reg_tbl[i][1]);
    ov5640_write_reg(0x3821, 0x06);            /* RGB 屏水平镜像 */

    /* 7. 输出尺寸设为 LCD 分辨率(1024x600), 供逐行 DMA 采集(官方组3) */
    ov5640_outsize_set(4, 0, 1024, 600);

    g_cam_err = 0;
    return CAM_ERR_NONE;
}

void camera_start(void)
{
    /* DMA 配置: DCMI -> 行缓冲, 双缓冲, 每次一整行(512 个 32 位字) */
    __HAL_DCMI_DISABLE(&hdcmi);
    HAL_DMA_DeInit(&hdma_dcmi);
    hdma_dcmi.Instance                 = DMA2_Stream1;
    hdma_dcmi.Init.Channel             = DMA_CHANNEL_1;
    hdma_dcmi.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_dcmi.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_dcmi.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_dcmi.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dcmi.Init.MemDataAlignment    = DMA_MDATAALIGN_HALFWORD;
    hdma_dcmi.Init.Mode                = DMA_CIRCULAR;
    hdma_dcmi.Init.Priority            = DMA_PRIORITY_HIGH;
    hdma_dcmi.Init.FIFOMode            = DMA_FIFOMODE_ENABLE;
    hdma_dcmi.Init.FIFOThreshold       = DMA_FIFO_THRESHOLD_HALFFULL;
    hdma_dcmi.Init.MemBurst            = DMA_MBURST_SINGLE;
    hdma_dcmi.Init.PeriphBurst         = DMA_PBURST_SINGLE;
    HAL_DMA_Init(&hdma_dcmi);
    __HAL_LINKDMA(&hdcmi, DMA_Handle, hdma_dcmi);

    g_cam_curline = 0;
    g_frame_ready = 0;

    /* 三缓冲初始化: 三块全填白(0xFF=RGB565 白), 未覆盖区域显示白底不闪烁;
     * 显示缓冲 buf0, 摄像头从 buf1 开始写 */
    memset((void *)g_cam_fb[0], 0xFF, CAM_FB_SIZE * 2);
    memset((void *)g_cam_fb[1], 0xFF, CAM_FB_SIZE * 2);
    memset((void *)g_cam_fb[2], 0xFF, CAM_FB_SIZE * 2);
    g_cam_disp = 0;
    g_cam_wbuf = 1;
    LTDC_Layer1->CFBAR = (uint32_t)g_cam_fb[0];     /* LTDC 层指向 buf0 */
    LTDC->SRCR = LTDC_SRCR_IMR;                     /* 立即重载: 切页即显示白底 */

    /* 清历史中断标志, 防止启动瞬间误触发一次 */
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI);
    __HAL_DMA_CLEAR_FLAG(&hdma_dcmi, DMA_FLAG_TCIF1_5);

    /* 官方经验: 启动前先解锁, 避免 HAL 因锁定返回 BUSY */
    __HAL_UNLOCK(&hdma_dcmi);

    /* 双缓冲启动: 每次传完一行触发 TC 中断, 且自动切到另一块缓冲 */
    HAL_DMAEx_MultiBufferStart(&hdma_dcmi, (uint32_t)&hdcmi.Instance->DR,
                               (uint32_t)g_cam_line_buf[0],
                               (uint32_t)g_cam_line_buf[1],
                               CAM_LINE_WORDS);
    __HAL_DMA_ENABLE_IT(&hdma_dcmi, DMA_IT_TC);     /* 行完成中断 */

    __HAL_DCMI_ENABLE(&hdcmi);
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);    /* 帧完成中断(重置行号) */
    hdcmi.Instance->CR |= DCMI_CR_CAPTURE;          /* 开始捕获 */
}

void camera_stop(void)
{
    hdcmi.Instance->CR &= ~DCMI_CR_CAPTURE;         /* 停止捕获 */
    while (hdcmi.Instance->CR & DCMI_CR_CAPTURE);   /* 等当前行结束 */
    __HAL_DMA_DISABLE_IT(&hdma_dcmi, DMA_IT_TC);
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_FRAME);
    __HAL_DCMI_DISABLE(&hdcmi);
    __HAL_DMA_DISABLE(&hdma_dcmi);
    g_frame_ready = 0;

    /* 显示切回首页 UI 帧缓冲(0xC0000000), 立即重载 */
    LTDC_Layer1->CFBAR = (uint32_t)lcd_get_framebuf();
    LTDC->SRCR = LTDC_SRCR_IMR;
}

/* DMA 每行传输完成回调(在 DMA2_Stream1_IRQHandler 里调用):
 * 判 CT 位(bit19) 找"刚传满"的那块缓冲, 将该行写入"后缓冲"(g_cam_wbuf) */
void camera_line_cb(void)
{
    uint16_t *src;
    uint16_t *dst;

    if (hdma_dcmi.Instance->CR & DMA_SxCR_CT)
        src = (uint16_t *)g_cam_line_buf[0];        /* DMA 正用 buf1 -> 读 buf0 */
    else
        src = (uint16_t *)g_cam_line_buf[1];        /* DMA 正用 buf0 -> 读 buf1 */

    if (g_cam_curline < LCD_HEIGHT)
    {
        dst = g_cam_fb[g_cam_wbuf] + (uint32_t)g_cam_curline * LCD_WIDTH;
        cam_line_copy(dst, src);                    /* DMA2D 硬件拷贝一行 */
        g_cam_curline++;
    }
    /* 超出屏高的多余行直接丢弃, 由 FRAME 中断统一归零 */
}

/* DCMI 帧完成回调(HAL_DCMI_FrameEventCallback -> 本函数):
 * 一帧已完整写入后缓冲 g_cam_wbuf, 请求 LTDC 在下个垂直消隐期切到它显示,
 * 然后轮转到下一块缓冲继续写、行号归零。
 * 关键: HAL_DCMI_IRQHandler 处理完帧中断会自动关闭 DCMI_IT_FRAME,
 * 必须在这里重新使能并清标志, 否则帧中断只触发一次 → 行号永不归零 → 画面滚动。 */
void camera_on_frame(void)
{
    /* 清帧中断标志(HAL 不会自动清) */
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI);

    /* 刚写满的缓冲提交给 LTDC 显示: 垂直消隐重载(VBR), 换页无撕裂 */
    LTDC_Layer1->CFBAR = (uint32_t)g_cam_fb[g_cam_wbuf];
    LTDC->SRCR = LTDC_SRCR_VBR;

    /* 三块轮转: 写入缓冲指向下一块(上一显示缓冲已释放) */
    g_cam_disp = g_cam_wbuf;
    g_cam_wbuf = (uint8_t)((g_cam_wbuf + 1) % 3);

    g_frame_ready = 1;
    g_cam_curline = 0;          /* 行号归零(与 VSYNC 同步, 防漂移) */

    /* 重新使能帧中断(否则只触发一次) */
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);
}
