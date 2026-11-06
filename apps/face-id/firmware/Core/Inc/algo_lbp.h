#ifndef __ALGO_LBP_H
#define __ALGO_LBP_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 算法二: 手工特征直方图
 *   主特征 = 3x3 邻域 LBP(59 类均匀模式) + 4x4 分区归一化直方图(16x59 = 944 维)
 *            + 卡方距离 chi2(a,b) = sum( (a-b)^2 / (a+b) )
 *   对照特征 = 简化颜色直方图(R/G/B 各 16 bin, 共 48 维), 同样用卡方距离
 *   为什么分区: 全局颜色直方图丢失五官位置信息, 分区保留局部纹理的位置关系。
 *
 *   注: g_color_feat 由 face_db.c 在"保存/加载一条记录"时同步填充
 *       (颜色特征需要 RGB565 原图, 而顺序重建只能从灰度图做 LBP)。
 * ========================================================================= */

extern uint16_t (*g_lbp_feat)[LBP_DIM];       /* 25 x 944, 位于 SDRAM */
extern uint16_t (*g_color_feat)[COLOR_DIM];   /* 25 x 48,  位于 SDRAM */
extern float g_th_lbp;                        /* LBP 卡方距离阈值(需标定) */
extern float g_th_color;                      /* 颜色卡方距离阈值(需标定) */

void algo_lbp_init_map(void);                                 /* 建立 256 -> 59 的映射表 */
void algo_lbp_extract(const uint8_t *gray, uint16_t *feat);   /* LBP 特征提取 */
void algo_color_extract(const uint16_t *rgb48, uint16_t *feat);/* 颜色特征提取 */
void algo_feat_rebuild_all(void);                             /* 图库变化后重建 LBP 特征 */

/**
 * @brief LBP / 颜色 卡方距离最近邻匹配
 * @retval 1 = 通过; 0 = 拒识
 */
int algo_lbp_match(const uint8_t *gray, uint8_t *pid, float *score);
int algo_color_match(const uint16_t *rgb48, uint8_t *pid, float *score);

#endif
