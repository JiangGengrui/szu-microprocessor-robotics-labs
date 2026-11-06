#include "algo_lbp.h"
#include "face_db.h"
#include <string.h>

/* =========================================================================
 * 算法二实现: LBP + 分区直方图 (卡方距离) / 简化颜色直方图 (卡方距离)
 * ========================================================================= */

uint16_t (*g_lbp_feat)[LBP_DIM]     = (uint16_t (*)[LBP_DIM])LBP_FEAT_BASE;
uint16_t (*g_color_feat)[COLOR_DIM] = (uint16_t (*)[COLOR_DIM])COLOR_FEAT_BASE;

/* 拒识阈值(2026-10-19 上机调整: LBP 初值 120 -> 200 -> 9000 -> 5000,
 *                     COL 初值 60 -> 450 -> 150)
 * 判据 distance <= 阈值 -> 通过, 所以越小越像, 阈值必须落在
 * "本人最大分 < 阈值 < 外人最小分"之间。改完仍要用验证图复测确认。 */
float g_th_lbp   = 5000.0f;
float g_th_color = 150.0f;

static uint8_t g_lbp_map[256];                  /* 原始 LBP 码(0~255) -> 0~58 */

/* 均匀模式判定: 8 位循环中 0<->1 跳变次数 <= 2 */
static int lbp_is_uniform(uint8_t x)
{
    int t = 0;
    for (int i = 0; i < 8; i++) {
        int a = (x >> i) & 1;
        int b = (x >> ((i + 1) & 7)) & 1;
        if (a != b) t++;
    }
    return (t <= 2);
}

/**
 * @brief 建立 256 -> 59 类映射表(均匀模式共 58 类, 非均匀归为第 59 类)
 */
void algo_lbp_init_map(void)
{
    int next = 0;
    for (int i = 0; i < 256; i++)
        g_lbp_map[i] = lbp_is_uniform((uint8_t)i) ? (uint8_t)(next++) : 0xFF;
    for (int i = 0; i < 256; i++)
        if (g_lbp_map[i] == 0xFF) g_lbp_map[i] = 58;
}

/* 仅对 x,y in [1,46] 调用(无需边界检查) */
static uint8_t lbp_code(const uint8_t *img, int x, int y)
{
    static const int dx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
    static const int dy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};
    int c = img[y * FACE_W + x];
    int code = 0;
    for (int k = 0; k < 8; k++)
        if (img[(y + dy[k]) * FACE_W + (x + dx[k])] >= c) code |= (1 << k);
    return (uint8_t)code;
}

/**
 * @brief LBP 分区直方图特征(16 个 12x12 分区, 每区 59 bin, 归一化到 1000)
 */
void algo_lbp_extract(const uint8_t *gray, uint16_t *feat)
{
    const int blk = FACE_W / LBP_BLK;              /* 12 */

    for (int by = 0; by < LBP_BLK; by++)
    {
        for (int bx = 0; bx < LBP_BLK; bx++)
        {
            uint16_t cnt[LBP_CLASS];
            memset(cnt, 0, sizeof(cnt));
            int total = 0;

            for (int y = by * blk; y < (by + 1) * blk; y++)
            {
                if (y < 1 || y > FACE_H - 2) continue;      /* 跳过边界(无 8 邻域) */
                for (int x = bx * blk; x < (bx + 1) * blk; x++)
                {
                    if (x < 1 || x > FACE_W - 2) continue;
                    cnt[g_lbp_map[lbp_code(gray, x, y)]]++;
                    total++;
                }
            }

            uint16_t *f = feat + (by * LBP_BLK + bx) * LBP_CLASS;
            for (int k = 0; k < LBP_CLASS; k++)
                f[k] = total ? (uint16_t)((uint32_t)cnt[k] * 1000U / (uint32_t)total) : 0;
        }
    }
}

/**
 * @brief 简化颜色直方图特征(3 x 16 bin, 归一化到 1000)
 */
void algo_color_extract(const uint16_t *rgb48, uint16_t *feat)
{
    uint16_t h[3][COLOR_BIN];
    memset(h, 0, sizeof(h));

    for (int i = 0; i < FACE_PIX; i++)
    {
        uint16_t p = rgb48[i];
        uint32_t r = (p >> 11) & 0x1F;             /* 0..31 */
        uint32_t g = (p >> 5)  & 0x3F;             /* 0..63 */
        uint32_t b =  p        & 0x1F;             /* 0..31 */
        h[0][(r * COLOR_BIN) >> 5]++;
        h[1][(g * COLOR_BIN) >> 6]++;
        h[2][(b * COLOR_BIN) >> 5]++;
    }

    for (int c = 0; c < 3; c++)
        for (int k = 0; k < COLOR_BIN; k++)
            feat[c * COLOR_BIN + k] = (uint16_t)((uint32_t)h[c][k] * 1000U / FACE_PIX);
}

/* 卡方距离 */
static float chi2_dist(const uint16_t *a, const uint16_t *b, int dim)
{
    float s = 0;
    for (int i = 0; i < dim; i++) {
        float d = (float)a[i] - (float)b[i];
        s += d * d / ((float)a[i] + (float)b[i] + 1.0f);
    }
    return s;
}

/**
 * @brief 图库变化后重建全部 LBP 特征(颜色特征由 face_db 同步维护)
 */
void algo_feat_rebuild_all(void)
{
    for (int i = 0; i < g_train_cnt; i++)
        algo_lbp_extract(g_train_gray[i], g_lbp_feat[i]);
}

int algo_lbp_match(const uint8_t *gray, uint8_t *pid, float *score)
{
    static uint16_t q[LBP_DIM];                    /* 静态, 不放栈上 */
    algo_lbp_extract(gray, q);

    float best = 1e30f; int bi = -1;
    for (int i = 0; i < g_train_cnt; i++) {
        float d = chi2_dist(q, g_lbp_feat[i], LBP_DIM);
        if (d < best) { best = d; bi = i; }
    }
    if (bi < 0) { *pid = 0; *score = 0.0f; return 0; }
    *pid = g_train_pid[bi]; *score = best;
    return (best <= g_th_lbp) ? 1 : 0;
}

int algo_color_match(const uint16_t *rgb48, uint8_t *pid, float *score)
{
    uint16_t q[COLOR_DIM];
    algo_color_extract(rgb48, q);

    float best = 1e30f; int bi = -1;
    for (int i = 0; i < g_train_cnt; i++) {
        float d = chi2_dist(q, g_color_feat[i], COLOR_DIM);
        if (d < best) { best = d; bi = i; }
    }
    if (bi < 0) { *pid = 0; *score = 0.0f; return 0; }
    *pid = g_train_pid[bi]; *score = best;
    return (best <= g_th_color) ? 1 : 0;
}
