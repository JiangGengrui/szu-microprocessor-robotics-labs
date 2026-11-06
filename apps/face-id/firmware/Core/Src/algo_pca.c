#include "algo_pca.h"
#include "face_db.h"
#include <math.h>

/* =========================================================================
 * 算法三实现: 样本 Gram 矩阵 + Jacobi 特征分解 + 特征脸投影
 * ========================================================================= */

float g_th_pca = 1000.0f;              /* 拒识阈值(2026-10-19 上机调整: 初值 3000 -> 1000) */
int   g_pca_k  = 0;

#define PCA_K_MAX  24                  /* 不少于 PCA_K(12) 即可 */

static float g_mean[FACE_PIX];                                  /* 均值图 2304 */
static float g_gram[FACE_MAX_SLOTS][FACE_MAX_SLOTS];            /* 25x25 */
static float g_eigval[FACE_MAX_SLOTS];
static float g_eigvec[FACE_MAX_SLOTS][FACE_MAX_SLOTS];          /* 列向量 */
static float g_train_proj[FACE_MAX_SLOTS][PCA_K_MAX];           /* 训练图投影 */
static int   g_sort_idx[FACE_MAX_SLOTS];
static int   g_N = 0;
/* 特征脸(12 x 2304)放在 SDRAM 工作区 */
static float (*g_eigenface)[FACE_PIX] = (float (*)[FACE_PIX])PCA_WORK_BASE;

/* ---------------- 对称矩阵 Jacobi 特征分解 ----------------
 * 输入 A(n x n, 对称, 原地被破坏), 输出 eigval[0..n-1] 与列向量矩阵 V */
static void jacobi_eigen(float A[][FACE_MAX_SLOTS], int n,
                         float *eigval, float V[][FACE_MAX_SLOTS])
{
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            V[i][j] = (i == j) ? 1.0f : 0.0f;

    for (int sweep = 0; sweep < 100; sweep++)
    {
        float off = 0;
        for (int i = 0; i < n - 1; i++)
            for (int j = i + 1; j < n; j++) off += A[i][j] * A[i][j];
        if (off < 1e-9f) break;                        /* 已对角化 */

        for (int p = 0; p < n - 1; p++)
        {
            for (int q = p + 1; q < n; q++)
            {
                if (fabsf(A[p][q]) < 1e-12f) continue;

                float theta = (A[q][q] - A[p][p]) / (2.0f * A[p][q]);
                float t = ((theta >= 0.0f) ? 1.0f : -1.0f) /
                          (fabsf(theta) + sqrtf(theta * theta + 1.0f));
                float c = 1.0f / sqrtf(t * t + 1.0f);
                float s = t * c;

                for (int k = 0; k < n; k++) {
                    if (k != p && k != q) {
                        float akp = A[k][p], akq = A[k][q];
                        A[k][p] = A[p][k] = c * akp - s * akq;
                        A[k][q] = A[q][k] = s * akp + c * akq;
                    }
                }
                float app = A[p][p], aqq = A[q][q], apq = A[p][q];
                A[p][p] = c * c * app - 2.0f * s * c * apq + s * s * aqq;
                A[q][q] = s * s * app + 2.0f * s * c * apq + c * c * aqq;
                A[p][q] = A[q][p] = 0.0f;

                for (int k = 0; k < n; k++) {
                    float vkp = V[k][p], vkq = V[k][q];
                    V[k][p] = c * vkp - s * vkq;
                    V[k][q] = s * vkp + c * vkq;
                }
            }
        }
    }
    for (int i = 0; i < n; i++) eigval[i] = A[i][i];
}

/* ---------------- 训练 ---------------- */
int algo_pca_train(void)
{
    int N = g_train_cnt;
    if (N < 2) { g_pca_k = 0; return 1; }              /* 样本不足 */
    g_N = N;

    /* 1. 均值图 */
    for (int p = 0; p < FACE_PIX; p++)
    {
        float s = 0;
        for (int i = 0; i < N; i++) s += (float)g_train_gray[i][p];
        g_mean[p] = s / (float)N;
    }

    /* 2. 样本 Gram 矩阵 G = A^T A (只算上三角, 再对称填) */
    for (int i = 0; i < N; i++)
    {
        for (int j = i; j < N; j++)
        {
            const uint8_t *xi = g_train_gray[i];
            const uint8_t *xj = g_train_gray[j];
            float acc = 0;
            for (int p = 0; p < FACE_PIX; p++)
            {
                float a = (float)xi[p] - g_mean[p];
                float b = (float)xj[p] - g_mean[p];
                acc += a * b;
            }
            g_gram[i][j] = g_gram[j][i] = acc;
        }
    }

    /* 3. 特征分解 */
    jacobi_eigen(g_gram, N, g_eigval, g_eigvec);

    /* 4. 特征值降序排序(选择排序, N 很小) */
    for (int i = 0; i < N; i++) g_sort_idx[i] = i;
    for (int i = 0; i < N - 1; i++)
        for (int j = i + 1; j < N; j++)
            if (g_eigval[g_sort_idx[j]] > g_eigval[g_sort_idx[i]]) {
                int t = g_sort_idx[i]; g_sort_idx[i] = g_sort_idx[j]; g_sort_idx[j] = t;
            }

    /* 5. 取前 k 个有效主成分(k <= N-1 且 lambda > eps) */
    int k = (PCA_K < N - 1) ? PCA_K : (N - 1);
    g_pca_k = 0;
    for (int j = 0; j < k; j++) {
        float lam = g_eigval[g_sort_idx[j]];
        if (lam < 1e-3f) break;                        /* 跳过近零特征值, 防除零 */
        g_pca_k++;
    }
    if (g_pca_k < 1) return 1;

    /* 6. 特征脸 + 训练图投影 */
    for (int j = 0; j < g_pca_k; j++)
    {
        int oj = g_sort_idx[j];
        float inv = 1.0f / sqrtf(g_eigval[oj]);
        for (int p = 0; p < FACE_PIX; p++) {
            float s = 0;
            for (int i = 0; i < N; i++)
                s += g_eigvec[i][oj] * ((float)g_train_gray[i][p] - g_mean[p]);
            g_eigenface[j][p] = s * inv;
        }
        for (int i = 0; i < N; i++)
            g_train_proj[i][j] = sqrtf(g_eigval[oj]) * g_eigvec[i][oj];
    }
    return 0;
}

/* ---------------- 识别 ---------------- */
int algo_pca_match(const uint8_t *gray, uint8_t *pid, float *score)
{
    if (g_pca_k < 1 || g_N < 2) { *pid = 0; *score = 0.0f; return 0; }

    /* 查询图投影到 k 维子空间 */
    float wq[PCA_K_MAX];
    for (int j = 0; j < g_pca_k; j++)
    {
        float s = 0;
        for (int p = 0; p < FACE_PIX; p++)
            s += g_eigenface[j][p] * ((float)gray[p] - g_mean[p]);
        wq[j] = s;
    }

    /* 低维欧氏距离最近邻 */
    float best = 1e30f; int bi = -1;
    for (int i = 0; i < g_N; i++)
    {
        float d = 0;
        for (int j = 0; j < g_pca_k; j++) {
            float t = wq[j] - g_train_proj[i][j];
            d += t * t;
        }
        d = sqrtf(d);
        if (d < best) { best = d; bi = i; }
    }
    if (bi < 0) { *pid = 0; *score = 0.0f; return 0; }
    *pid = g_train_pid[bi]; *score = best;
    return (best <= g_th_pca) ? 1 : 0;
}
