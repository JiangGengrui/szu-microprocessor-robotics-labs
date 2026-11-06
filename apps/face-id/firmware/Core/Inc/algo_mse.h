#ifndef __ALGO_MSE_H
#define __ALGO_MSE_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 算法一: MSE 模板匹配
 *   把 48x48 灰度图当作 2304 维向量, 对库中每个模板求逐像素差的平方平均,
 *   取最小者作为候选; 仅当最小 MSE <= 阈值时判"通过"。
 *   MSE(q,t) = (1/2304) * sum( (q[p]-t[p])^2 )
 *   特点: 直接比较像素灰度, 对亮度变化与位置偏移极敏感。
 * ========================================================================= */

extern float g_th_mse;                 /* 拒识阈值(必须用验证图标定) */

/* ---- 分数分解诊断(供 UI 显示, 用来区分"分数高是亮度还是长相") ----
 * MSE = 亮度分量 + 结构分量, 三者满足:
 *      MSE_total = (dmean)^2 + var
 *    dmean = 查询图与最佳模板的平均灰度差(有符号; 正=画面更亮)
 *    var   = 扣掉整体亮度差之后剩下的差异(位置/尺度/姿态/纹理)
 * 判读: 若 (dmean)^2 占了 MSE 的一大半 -> 是曝光/亮度问题, 不是长相问题,
 *       该怀疑"拍照时 AEC 没收敛"或"补光变了", 而不是去调阈值。
 * 每次 algo_mse_match() 结束后更新。 */
extern int      g_mse_dmean;           /* 亮度差 (-255~255) */
extern uint16_t g_mse_var;             /* 结构差异(去均值后的 MSE) */
extern uint8_t  g_mse_qmean;           /* 查询图自身平均灰度 0~255 */

/**
 * @brief 与图库做 MSE 最近邻匹配
 * @param gray  : 查询图 48x48 灰度
 * @param pid   : 输出候选人员编号(1~5; 无样本时 0)
 * @param score : 输出最小 MSE(越小越像)
 * @retval 1 = 通过; 0 = 不通过(拒识)
 */
int algo_mse_match(const uint8_t *gray, uint8_t *pid, float *score);

#endif
