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
    /* 重新交给 I2C2 复用: 必须先 DeInit 把 State 置回 RESET,
     * 否则 HAL_I2C_Init 不会重跑 MspInit, PH4/PH5 会一直停在 GPIO 模式 */
    HAL_I2C_DeInit(&hi2c2);
    (void)HAL_I2C_Init(&hi2c2);
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

/* ---- 电子变焦: 只缩小传感器开窗(0x3800~0x3807), 输出尺寸不动 ----
 * 原理: OV5640 的 ISP 会把"开窗区域"缩放到 0x3808/0x3809 指定的输出尺寸。
 *      所以开窗取中央 1/N, 输出仍 1024x600 -> 画面被放大 N 倍(视野缩小)。
 * ★ 为什么不改输出尺寸: 本工程 DMA 按 1024 像素一行双缓冲, camera_line_cb()
 *   的 src/dst 映射也写死了 FRAME_W。改输出宽高会连带破坏这两处, 改开窗不会。
 * ★ 为什么改 ROI 框大小没有这个效果: 框内是 1:1 像素映射, 框变小只是看得少,
 *   不会放大画面。真正的"变焦"只能靠改传感器开窗(或换镜头)。
 * 注意: 开窗变小后人脸在 48x48 缩略图里占比变大, 取图范围随之改变
 *       => 变焦后必须重录全部样本。 */
static void ov5640_dzoom_set(uint8_t z)
{
    /* 传感器有效阵列: 见 init 表 0x3804/0x3805=0x0A3F(=2623)、0x3806/0x3807=0x06A9(=1705) */
    const uint16_t fullw = 2624, fullh = 1706;
    uint16_t w, h, hs, vs, hw, vh;

    if (z < 1) z = 1;
    if (z > 4) z = 4;
    if (z == 1) return;                        /* 全视场: 保持 init 表原值, 一个寄存器都不碰 */

    /* YCbCr 4:2:2 输出要求宽高与起始坐标为偶数, 否则色度错位 */
    w  = (uint16_t)((fullw / z) & 0xFFFE);
    h  = (uint16_t)((fullh / z) & 0xFFFE);
    hs = (uint16_t)(((fullw - w) / 2) & 0xFFFE);
    vs = (uint16_t)(((fullh - h) / 2) & 0xFFFE);
    hw = (uint16_t)(hs + w - 1);               /* 开窗结束列(含) */
    vh = (uint16_t)(vs + h - 1);               /* 开窗结束行(含) */

    ov5640_write_reg(0x3212, 0x03);            /* 开始组3: 与 outsize 同一保护组 */
    ov5640_write_reg(0x3800, hs >> 8);         /* HS 开窗起始列 */
    ov5640_write_reg(0x3801, hs & 0xFF);
    ov5640_write_reg(0x3802, vs >> 8);         /* VS 开窗起始行 */
    ov5640_write_reg(0x3803, vs & 0xFF);
    ov5640_write_reg(0x3804, hw >> 8);         /* HW 开窗结束列 */
    ov5640_write_reg(0x3805, hw & 0xFF);
    ov5640_write_reg(0x3806, vh >> 8);         /* VH 开窗结束行 */
    ov5640_write_reg(0x3807, vh & 0xFF);
    ov5640_write_reg(0x3212, 0x13);            /* 结束组3 */
    ov5640_write_reg(0x3212, 0xa3);            /* 启用组3 */
}

/* ===================== 对外接口 ===================== */
static volatile uint8_t g_frame_ready = 0;

/* 行缓冲(双缓冲): 第 0/1 块各 CAM_LINE_WORDS 个 32 位字 = 一整行 1024 像素 */
static uint32_t g_cam_line_buf[2][CAM_LINE_WORDS];
/* 当前写入取景框的行号(0 ~ ROI_H-1) */
static volatile uint16_t g_cam_curline = 0;

/* ---- 取景框在帧缓冲里的位置(不额外分配缓冲, 直接写 LCD 主帧缓冲) ----
 * 帧缓冲 = 0xC0000000, 行跨度 FRAME_W(=1024)。取景框左上角像素(x=272,y=48)的
 * 字节地址 = 0xC0000000 + (48*1024 + 272) * 2 = 0xC0018220。
 * 摄像头只写这 480x480 区域, 框外(UI/白底)一个字节都不碰。 */
#define LCD_FB_ADDR     0xC0000000UL
#define LCD_FB_ROW      ((uint32_t)FRAME_W)              /* 一行像素数 */

/* ★ 这里算的是"字节地址", 偏移量必须 ×2(RGB565 一个像素 2 字节) ★
 * 漏乘 2 时: 0xC0000000 + 48*1024 + 272 = 0xC000C110, 当指针用等于第
 * 24712 个像素 = (x=136, y=24) —— 不是取景框左上角 (272,48)。
 * 后果(已上机复现):
 *   1) camera_start() 首次填白把 (136,24)-(615,503) 刷成白色,
 *      正好盖掉状态栏下半行、左侧标题/设备标识/按键提示的右半边、
 *      以及取景框的上边框和左边框 -> 第一次进界面"字体和格式不全";
 *      而 g_cam_started 只填一次, 再按一次 K0 整页重画就恢复正常;
 *   2) camera_roi_ptr() 也跟着错 -> 录入/识别取到的 48x48 人脸图整块偏
 *      左上 136/24 像素, 分数必然全乱。
 * 正确字节地址 = 0xC0000000 + (48*1024 + 272) * 2 = 0xC0018220。 */
#define ROI_FB_ADDR     (LCD_FB_ADDR + (((uint32_t)ROI_Y0 * LCD_FB_ROW) + (uint32_t)ROI_X0) * 2UL)

static uint8_t g_cam_started = 0;         /* 是否已启动过一次(首次要清框内区域) */

/* DMA2D 硬件拷贝一行 RGB565 像素(内存到内存)。
 * 之前用 CPU memcpy, 在 -O0 编译 + SDRAM 读写带宽竞争下, 拷贝一行 2048 字节
 * 的速度赶不上摄像头行速率(约 36us/行), 行中断积压, 每帧只有约 2/3 的行被
 * 及时写入, 剩余 1/3 区域显示残留数据并闪烁。DMA2D 硬件突发传输 + 不占 CPU,
 * 速度远快于 memcpy, 与官方例程的 ltdc_color_fill 一致。
 * 现在只搬取景框那 480 像素(960 字节), 一帧总量 450KB(改版前是全屏 1.2MB)。 */
static void cam_line_copy(uint16_t *dst, const uint16_t *src, uint16_t w)
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
    DMA2D->NLR     = ((uint32_t)1UL << 16) | (uint32_t)w;  /* NL=1 行, PL=w 像素/行 */
    DMA2D->CR      = DMA2D_CR_START;                /* 启动传输 */
    while ((DMA2D->ISR & DMA2D_ISR_TCIF) == 0)      /* 等待完成 */
    {
        if (++timeout > 0xFFFFF) break;
    }
    DMA2D->IFCR = DMA2D_IFCR_CTCIF;                 /* 清完成标志 */
}

/**
 * @brief 取景框图像在帧缓冲里的起始指针(框内左上角像素)
 * @note  行跨度不是 ROI_W 而是 FRAME_W(1024)! 取图时必须一起传 stride,
 *        详见 img_roi_to_48() 的第二个参数。
 */
const uint16_t *camera_roi_ptr(void)
{
    return (const uint16_t *)ROI_FB_ADDR;
}

/**
 * @brief 取景框图像的行跨度(= 整屏行宽 1024)
 */
uint16_t camera_roi_stride(void)
{
    return (uint16_t)FRAME_W;
}

/**
 * @brief 取景框内画面的平均亮度(16 像素网格抽样)
 * @note  480/16=30 -> 30x30=900 个采样点, 主循环里跑一次不到 1ms。
 *        不做加权(只用整数近似 Rec.601), 精确度对"判断曝光是否稳定"完全够。
 */
uint16_t camera_roi_mean(void)
{
    const uint16_t *p = camera_roi_ptr();
    uint32_t sum = 0;
    uint16_t n = 0, x, y;

    for (y = 0; y < ROI_H; y += 16)
    {
        const uint16_t *row = p + (uint32_t)y * LCD_FB_ROW;
        for (x = 0; x < ROI_W; x += 16)
        {
            uint16_t c = row[x];
            uint16_t r = (uint16_t)((c >> 11) & 0x1F);
            uint16_t g = (uint16_t)((c >> 5)  & 0x3F);
            uint16_t b = (uint16_t)(c & 0x1F);
            /* (r<<3)*77 + (g<<2)*150 + (b<<3)*29, 再 >>8 */
            sum += (((uint32_t)r << 3) * 77U + ((uint32_t)g << 2) * 150U
                    + ((uint32_t)b << 3) * 29U) >> 8;
            n++;
        }
    }
    if (n == 0) return 0;
    return (uint16_t)(sum / n);
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

    /* 8. 电子变焦(CAM_DZOOM=1 时本函数直接 return, 保持全视场) */
    ov5640_dzoom_set(CAM_DZOOM);

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

    /* 首次启动: 把取景框区域先填白(首帧到达前不显示上电残留的垃圾数据)。
     * 只填 480x480 那一块, 不碰 UI。用 lcd_fill 而不是自己算指针, 让它统一
     * 处理行跨度/越界裁剪, 避免再出一次"地址算错把 UI 刷掉"的低级错误。 */
    if (!g_cam_started)
    {
        lcd_fill(ROI_X0, ROI_Y0, ROI_W, ROI_H, WHITE);
        g_cam_started = 1;
    }

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

/* 只停采集, 不动显示(供 camera_stop / camera_freeze 复用)
 * ★ 这里的等待必须带超时: CAPTURE 位要等"当前帧结束"才由硬件清零, 若摄像头
 *   没有输出像素时钟(排线松动/未上电/OV5640 配置失败), 该位可能永远不清,
 *   没有超时就会死在这一行 —— 表现为界面停止刷新、摄像头也没有图像。 */
static void cam_capture_stop(void)
{
    uint32_t t0 = HAL_GetTick();

    hdcmi.Instance->CR &= ~DCMI_CR_CAPTURE;         /* 停止捕获 */
    while (hdcmi.Instance->CR & DCMI_CR_CAPTURE)    /* 等当前帧结束, 最多等 100ms */
    {
        if (HAL_GetTick() - t0 > 100U) break;
    }
    __HAL_DMA_DISABLE_IT(&hdma_dcmi, DMA_IT_TC);
    __HAL_DCMI_DISABLE_IT(&hdcmi, DCMI_IT_FRAME);
    __HAL_DCMI_DISABLE(&hdcmi);
    __HAL_DMA_DISABLE(&hdma_dcmi);
    __HAL_DMA_CLEAR_FLAG(&hdma_dcmi, DMA_FLAG_TCIF1_5);   /* 清可能残留的行中断标志 */
    g_frame_ready = 0;
}

void camera_stop(void)
{
    cam_capture_stop();
    /* 无需操作 LTDC: 画面就写在帧缓冲里, 回首页时 home_page_draw() 整屏重画即可 */
}

/* 冻结: 停采集。画面就留在帧缓冲里, 不再刷新, 自然定格。
 * UI 画在同一块帧缓冲的框外区域, 本来就不受摄像头影响 -> 文字照常显示。 */
void camera_freeze(void)
{
    cam_capture_stop();
}

/* DMA 每行传输完成回调(在 DMA2_Stream1_IRQHandler 里调用):
 * 判 CT 位(bit19) 找"刚传满"的那块行缓冲; 只处理落在取景框内的行,
 * 且只搬 ROI 那 480 像素 —— 直接写进 LCD 主帧缓冲的框内区域。
 * 框外的行一个字节都不写, 所以 UI 与白底永远不会被覆盖。 */
void camera_line_cb(void)
{
    uint16_t *src;
    uint16_t *dst;
    uint16_t y = g_cam_curline;

    if (hdma_dcmi.Instance->CR & DMA_SxCR_CT)
        src = (uint16_t *)g_cam_line_buf[0];        /* DMA 正用 buf1 -> 读 buf0 */
    else
        src = (uint16_t *)g_cam_line_buf[1];        /* DMA 正用 buf0 -> 读 buf1 */

    g_cam_curline = (uint16_t)(y + 1);

    if (y < ROI_Y0 || y >= (uint16_t)(ROI_Y0 + ROI_H))
        return;                                     /* 框外的行直接丢弃, 不写内存 */

    /* 目标 = 帧缓冲 + y*1024 + ROI_X0 */
    dst = (uint16_t *)LCD_FB_ADDR + (uint32_t)y * LCD_FB_ROW + ROI_X0;
    cam_line_copy(dst, src + ROI_X0, ROI_W);        /* DMA2D 只搬框内 480 像素 */
}

/* DCMI 帧完成回调(HAL_DCMI_FrameEventCallback -> 本函数):
 * 一帧的 480 行已全部写进帧缓冲, 这里只需要把行号归零。
 * 关键: HAL_DCMI_IRQHandler 处理完帧中断会自动关闭 DCMI_IT_FRAME,
 * 必须在这里重新使能并清标志, 否则帧中断只触发一次 -> 行号永不归零 -> 画面错位。 */
void camera_on_frame(void)
{
    /* 清帧中断标志(HAL 不会自动清) */
    __HAL_DCMI_CLEAR_FLAG(&hdcmi, DCMI_FLAG_FRAMERI);

    g_frame_ready = 1;
    g_cam_curline = 0;          /* 行号归零(与 VSYNC 同步, 防漂移) */

    /* 重新使能帧中断(否则只触发一次) */
    __HAL_DCMI_ENABLE_IT(&hdcmi, DCMI_IT_FRAME);
}

/**
 * @brief 取走"新帧已就绪"标志
 * @retval 1 = 自上次调用以来又写完了整一帧, 0 = 还没有
 * @note  拍照前用它连续丢几帧(见 face_app.c 的 snapshot()):
 *        刚 camera_start() 时第一帧可能只有半幅是新数据, 且 OV5640 的
 *        自动曝光/自动增益要几帧才收敛, 直接抓会把亮度抖动算进分数里。
 */
uint8_t camera_new_frame(void)
{
    if (!g_frame_ready) return 0;
    g_frame_ready = 0;
    return 1;
}
