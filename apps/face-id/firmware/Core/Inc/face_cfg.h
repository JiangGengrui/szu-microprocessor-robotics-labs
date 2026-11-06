#ifndef __FACE_CFG_H
#define __FACE_CFG_H
#include "main.h"
#include "lcd.h"

/* =========================================================================
 * face-id 人脸识别系统 —— 全局配置
 *   平台: DNF429-EB 扩展底板(阿波罗 V2 / STM32F429IGT6 / 7寸 1024x600 RGB屏 / OV5640)
 *   注意: 本工程的摄像头输出分辨率 == LCD 分辨率(1024x600), 见 camera.c 的
 *         ov5640_outsize_set(4, 0, 1024, 600); 因此所有帧缓冲尺寸都用 LCD_* 宏。
 * ========================================================================= */

/* ===== 人脸图规格(SDRAM 存图 / 算法输入) ===== */
#define FACE_W              48
#define FACE_H              48
#define FACE_PIX            (FACE_W * FACE_H)        /* 2304 个像素 */
#define FACE_RGB_BYTES      (FACE_PIX * 2)           /* 4608 字节(RGB565) */

/* ===== 摄像头帧(与 LCD 同尺寸)与取景框 ===== */
#define FRAME_W             LCD_WIDTH                 /* 1024 */
#define FRAME_H             LCD_HEIGHT                /* 600  */
#define ROI_W               480                       /* 取景框宽(240 放大一倍) */
#define ROI_H               480                       /* 取景框高(240 放大一倍) */
#define ROI_X0              ((FRAME_W - ROI_W) / 2)   /* 272: 屏幕水平居中 */
/* 垂直方向不再取几何居中((600-480)/2=60), 而是手定 48:
 * 上方要给状态栏(高 36)和 8px 外扩边框留位, 下方要给结果行留一行。 */
#define ROI_Y0              48
#define ROI_SCALE           (ROI_W / FACE_W)          /* = 10: 每个目标像素由 10x10 块平均得到 */

/* ===== 摄像头电子变焦(只缩传感器开窗, 输出仍 1024x600) =====
 *  1 = 全视场(默认, 与之前完全一致, 已录样本仍可用)
 *  2 = 中央 1/2 窗口 -> 视野减半, 人脸在画面里放大 2 倍
 *  3 = 中央 1/3 窗口 -> 放大 3 倍(AWB/AEC 取样区变小, 低光下收敛更慢)
 * ★ 改了这里, 之前录入的任何人脸样本都必须重录(取景范围变了)。
 * ★ 传感器是定焦的: "远近"只能靠电子变焦 + 人走动, 不能拧镜头。 */
#define CAM_DZOOM           1

/* 屏幕上看到的"取景框"画在 ROI 之外(外扩 MARGIN), 否则框线会被一起裁进人脸图里 */
#define ROI_BOX_MARGIN      8
#define ROI_BOX_X0          (ROI_X0 - ROI_BOX_MARGIN)
#define ROI_BOX_Y0          (ROI_Y0 - ROI_BOX_MARGIN)
#define ROI_BOX_W           (ROI_W + 2 * ROI_BOX_MARGIN)
#define ROI_BOX_H           (ROI_H + 2 * ROI_BOX_MARGIN)

/* ===== 人脸库规模 ===== */
#define FACE_MAX_PEOPLE     5
#define FACE_IMG_PER_PERSON 5
#define FACE_MAX_SLOTS      (FACE_MAX_PEOPLE * FACE_IMG_PER_PERSON)   /* 25 条 */

/* ===== 特征维度 ===== */
#define LBP_BLK             4                         /* 4x4 分区 */
#define LBP_CLASS           59                        /* 均匀 LBP 59 类 */
#define LBP_DIM             (LBP_BLK * LBP_BLK * LBP_CLASS)   /* 944 */
#define COLOR_BIN           16                        /* 每通道 16 bin */
#define COLOR_DIM           (3 * COLOR_BIN)           /* 48 */

/* ===== PCA(特征脸)参数 ===== */
#define PCA_K               12                        /* 主成分个数, 不超过 N-1 */

/* ===== SDRAM 内存布局(32MB: 0xC0000000 ~ 0xC2000000) =====
 *  0xC0000000 + 1.2MB(0x12C000) : LCD 帧缓冲 = 唯一显示层
 *                                  既画 UI(框外), 也放摄像头画面(框内 480x480),
 *                                  靠"写入范围"分工, 见 camera.h
 *  0xC0300000 起                 : 空闲(原 Layer2 取景框缓冲已废弃, 可另作他用)
 *  0xC0700000 + 512KB           : 本项目算法工作区(下面这一组)
 */
#define LCD_FB_BASE         0xC0000000UL              /* 仅作参考: lcd.c 里定义 */
#define WORK_BASE           0xC0700000UL              /* 算法工作区基址 */

#define TRAIN_GRAY_BASE     (WORK_BASE + 0x00000000UL)  /* 25 x 2304 = 57.6 KB */
#define LBP_FEAT_BASE       (WORK_BASE + 0x00010000UL)  /* 25 x 944 x 2 = 46.1 KB */
#define COLOR_FEAT_BASE     (WORK_BASE + 0x00020000UL)  /* 25 x 48 x 2 = 2.3 KB */
#define PCA_WORK_BASE       (WORK_BASE + 0x00030000UL)  /* 特征脸 12 x 2304 x 4 = 108 KB */

/* ===== NOR Flash 图库区 ===== */
#define FACE_SLOT_SIZE      8192U                     /* 每条记录独占 8KB(2 个扇区) */
#define FACE_FLASH_BASE     0x00C00000UL              /* 32MB 芯片的 12MB 处;
                                                       * 小容量芯片由 face_db.c 自动下移 */

/* ===== 设备标识 ===== */
#define CFG_STU_NAME        "EDGE LAB"
#define CFG_STU_ID          "DEMO-0001"

#endif
