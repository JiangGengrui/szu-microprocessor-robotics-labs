#ifndef __ALGO_PCA_H
#define __ALGO_PCA_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 算法三: SVD / PCA (Eigenfaces 特征脸)
 *   1. 求训练集均值图 mean, 每张图中心化 a_i = x_i - mean
 *   2. 样本数 N(=25) << 像素维数(2304), 因此不分解 2304x2304,
 *      而构造样本 Gram 矩阵 G = A^T A (N x N), 用小矩阵特征分解代替
 *   3. 对 G 做 Jacobi 旋转法特征分解, 取前 k 个有效主成分(k <= N-1, 本项目 k=12)
 *   4. 特征脸 u_j = (1/sqrt(lambda_j)) * sum_i v_{j,i} * a_i
 *   5. 训练图/查询图投影到 k 维子空间, 低维欧氏距离最近邻 + 阈值拒识
 *   6. lambda 接近 0 时跳过该主成分, 避免除零
 * ========================================================================= */

extern float g_th_pca;                 /* 低维空间拒识阈值(需标定) */
extern int   g_pca_k;                  /* 实际使用的主成分个数 */

/**
 * @brief 用当前内存训练集训练特征脸子空间
 * @retval 0 = 成功; 1 = 样本不足或矩阵退化
 */
int algo_pca_train(void);

/**
 * @brief 把查询图投影到子空间并做低维欧氏距离最近邻
 * @retval 1 = 通过; 0 = 拒识(含未训练/无样本)
 */
int algo_pca_match(const uint8_t *gray, uint8_t *pid, float *score);

#endif
