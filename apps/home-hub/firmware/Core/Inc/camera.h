#ifndef __CAMERA_H
#define __CAMERA_H
#include "main.h"

/* 摄像头逐行采集架构(参考正点原子摄像头例程):
 *  OV5640 输出设为 1024x600(与 LCD 一致) -> DCMI 每收一行发一次 DMA 请求
 *  -> DMA 双缓冲搬到行缓冲 -> 每次行完成中断里把该行写入 LCD 帧缓冲。
 *  这样绕开 F4 DMA 的 16 位长度限制(65535), 也不需要 2MB 中间帧缓冲。 */
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
void    camera_start(void);              /* 启动 DCMI+DMA 连续采集(逐行写入LCD) */
void    camera_stop(void);               /* 停止采集(切页时用) */
void    camera_line_cb(void);            /* DMA 每行完成回调(由 DMA2_Stream1 中断调用) */
void    camera_on_frame(void);           /* DCMI 帧完成回调 */

/* 最近一次初始化的详细错误码(调试用) */
extern uint8_t g_cam_err;
/* 读回的 OV5640 ID 实际值(调试用, 应=0x5640) */
extern uint16_t g_cam_id;
/* SCCB 诊断: bit0=空闲SCL电平, bit1=空闲SDA电平, bit2=写地址0x78有ACK */
extern uint8_t g_sccb_diag;
/* PCF8574 读回值(诊断 PWDN 是否拉低): 正常应=0xFB; 0xEE=读失败 */
extern uint8_t g_cam_pcf;

/* 引用 CubeMX 生成的 DCMI/DMA/I2C 句柄(在 main.c 里定义) */
extern DCMI_HandleTypeDef hdcmi;
extern DMA_HandleTypeDef  hdma_dcmi;
extern I2C_HandleTypeDef  hi2c2;

#endif
