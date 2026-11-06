#ifndef __CAMERA_H
#define __CAMERA_H
#include "main.h"
#include "face_cfg.h"

/* 摄像头逐行采集架构(参考正点原子摄像头例程):
 *  OV5640 输出设为 1024x600(与 LCD 一致) -> DCMI 每收一行发一次 DMA 请求
 *  -> DMA 双缓冲搬到行缓冲 -> 每次行完成中断里把该行写入目标缓冲。
 *  这样绕开 F4 DMA 的 16 位长度限制(65535), 也不需要 2MB 中间帧缓冲。
 *
 * ★ exp4 显示方案(单图层, 只写取景框内):
 *   只用 LTDC Layer1, 帧缓冲就是 lcd.c 的 0xC0000000。UI 与摄像头画面共用同一
 *   块帧缓冲, 靠"写入范围"划分, 互不干扰:
 *
 *     ┌────────────────────── 1024x600 帧缓冲 ───────────────────────┐
 *     │ 状态栏 / 标题 / 设备标识 / 模式面板 / 底部提示  ← 只由 lcd_xxx 写 │
 *     │              ┌──────────────┐                                 │
 *     │              │ 480x480 取景框 │  ← 只有这块被摄像头逐行改写      │
 *     │              └──────────────┘                                 │
 *     │ 取景框边框 / 结果行 / 标定分数行  ← 只由 lcd_xxx 写            │
 *     └───────────────────────────────────────────────────────────────┘
 *
 *   camera_line_cb() 只处理 y ∈ [ROI_Y0, ROI_Y0+ROI_H) 的行, 每行只搬 ROI_W(480)
 *   个像素, 目标 = 帧缓冲 + y*1024 + ROI_X0。
 *   => 框外一个像素都不写: UI 常显、框外永远是 lcd_clear(WHITE) 的白底。
 *      不需要 Layer2 / 不需要垂直消隐切页 / 不需要 window 配置, 链路最短最可靠。
 *
 *   代价: 写的是"正在被 LTDC 扫描"的缓冲, 框内可能出现一条水平撕裂线。
 *        只影响框内 480 行, 换取的是一条不会再出错的显示通路。 */
#define CAM_LINE_WORDS    (1024 / 2)    /* 每行 32 位字数: 1024 像素, 每 word 含 2 像素 */

/* 摄像头初始化错误码 */
enum {
    CAM_ERR_NONE     = 0,   /* 成功 */
    CAM_ERR_PCF8574  = 1,   /* PCF8574 无应答 */
    CAM_ERR_SCCB     = 2,   /* SCCB/OV5640 通信失败 */
    CAM_ERR_ID       = 3,   /* OV5640 ID 校验失败 */
    CAM_ERR_DCMI     = 4,   /* DCMI/DMA 启动失败 */
};

uint8_t camera_init(void);               /* 初始化 PCF8574+SCCB+OV5640 */
void    camera_start(void);              /* 启动采集: 逐行写入帧缓冲的取景框区域 */
void    camera_stop(void);               /* 停止采集(回首页前调用, 首页会整屏重画) */
void    camera_freeze(void);             /* 只停采集: 框内画面定格, UI 不受任何影响 */
void    camera_line_cb(void);            /* DMA 每行完成回调(由 DMA2_Stream1 中断调用) */
void    camera_on_frame(void);           /* DCMI 帧完成回调 */

/* 是否有"一整帧已写完"的新画面; 有则返回 1 并清标志(供拍照前等帧稳定用)
 * 用途: OV5640 的 AEC/AGC 需要连续几帧才收敛, 且刚 camera_start() 的那一帧
 *       可能只有半幅是新数据 -> 拍照前先丢几帧, 分数重复性会明显变好。 */
uint8_t camera_new_frame(void);

/* 取景框图像在帧缓冲里的起始指针(框内左上角像素)与行跨度。
 * 录入/识别: 先 camera_freeze(), 再
 *     img_roi_to_48(camera_roi_ptr(), camera_roi_stride(), out48); */
const uint16_t *camera_roi_ptr(void);
uint16_t        camera_roi_stride(void);

/* 取景框内画面的平均亮度(0~255, 16x16 网格抽样, 约 900 点)。
 * 用途: 拍照前判断"曝光是否已经收敛" —— 连续两帧均值差 <=2 才算稳定。
 *       只在主循环调用(会读正在写的帧缓冲, 允许轻微撕裂, 只用于统计)。 */
uint16_t        camera_roi_mean(void);

/* 最近一次初始化的详细错误码(调试用) */
extern uint8_t g_cam_err;
/* 读回的 OV5640 ID 实际值(调试用, 应=0x5640) */
extern uint16_t g_cam_id;
/* SCCB 诊断: bit0=空闲SCL电平, bit1=空闲SDA电平, bit2=写地址0x78有ACK */
extern uint8_t g_sccb_diag;
/* PCF8574 读回值(诊断 PWDN 是否拉低): 正常应=0xFB; 0xEE=读失败 */
extern uint8_t g_cam_pcf;

/* 引用 CubeMX 生成的 DCMI/DMA/I2C/LTDC 句柄(在 main.c 里定义) */
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;
extern I2C_HandleTypeDef  hi2c2;
extern LTDC_HandleTypeDef hltdc;

#endif
