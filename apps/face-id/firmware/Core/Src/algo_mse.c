#include "algo_mse.h"
#include "face_db.h"

/* 拒识阈值(2026-10-19 上机调整: 初值 800 -> 1000)
 * 判据 distance <= 阈值 -> 通过。注意: 在"本人 2185 / 外人 745"那组实测下
 * 两类区间是重叠的, 任何单一阈值都无法同时满足
 * "本人通过 + 外人拒绝"; 这里的 1000 只是放宽后的当前值, 仍需用验证图复测。 */
float g_th_mse = 1000.0f;

/* 诊断: 把分数拆成"亮度分量"和"结构分量", 见 algo_mse.h 的说明 */
int      g_mse_dmean = 0;
uint16_t g_mse_var   = 0;
uint8_t  g_mse_qmean = 0;

/* =========================================================================
 * 抗光照开关(只改这一行, 然后重新 Build; 别的地方不用动)
 *   0 = 原始灰度 MSE  —— "算法一基线"就是这个, 分数越大越不像
 *   1 = 零均值归一化  —— 先把查询图/模板各自的灰度均值减掉再算 MSE,
 *                        即"只比长相结构, 不比亮不亮"
 *
 * 为什么要这个开关: 灰度 MSE 对整体亮度(曝光、灯的位置)极敏感 —— 同一张脸
 * 亮 10 个灰阶, MSE 大约就 +100; 反过来"外人 vs 模板"只要亮度接近, MSE 就
 * 很小。上机实测出现过 本人最大 2185 > 外人最小 745 的"类间不可分", 任何阈值
 * 都没法用。置 1 先把亮度这个干扰项排除掉, 再看两类分数是不是能分开。
 *
 * 注意: 置 1 后分数的量纲会变(通常整体变小), 阈值必须重新标定。
 * ========================================================================= */
#define MSE_USE_ZM   0

int algo_mse_match(const uint8_t *gray, uint8_t *pid, float *score)
{
    float    best    = 1e30f;
    int      bi      = -1;
    uint32_t best_s  = 0;
    int32_t  best_sd = 0;                   /* 与最佳模板的灰度差之和 */

#if MSE_USE_ZM
    int qsum = 0;
    for (int p = 0; p < FACE_PIX; p++) qsum += (int)gray[p];
    const int qmean = qsum / FACE_PIX;      /* 查询图的整体亮度 */
#endif

    for (int i = 0; i < g_train_cnt; i++)
    {
        const uint8_t *t = g_train_gray[i];
        uint32_t s  = 0;                    /* 平方差之和 */
        int32_t  sd = 0;                    /* 差值之和(用来算亮度分量) */
        float    mse;

#if MSE_USE_ZM
        {
            int tsum = 0;
            for (int p = 0; p < FACE_PIX; p++) tsum += (int)t[p];
            const int tmean = tsum / FACE_PIX;   /* 模板的整体亮度 */
            for (int p = 0; p < FACE_PIX; p++)
            {
                int d = ((int)gray[p] - qmean) - ((int)t[p] - tmean);
                s += (uint32_t)(d * d);
            }
        }
#else
        /* 一遍循环同时算 sum(d^2) 和 sum(d): 后者几乎不要钱, 却能告诉我们是
         * "整体亮度差"还是"长相不同"在撑高分数(见下方 g_mse_dmean/g_mse_var)。 */
        for (int p = 0; p < FACE_PIX; p++)
        {
            int d = (int)gray[p] - (int)t[p];
            s  += (uint32_t)(d * d);
            sd += d;
        }
#endif
        mse = (float)s / (float)FACE_PIX;

        if (mse < best) { best = mse; bi = i; best_s = s; best_sd = sd; }
    }

    /* 查询图自身亮度(每次匹配只算一次, 2304 次加法) */
    {
        uint32_t qs = 0;
        for (int p = 0; p < FACE_PIX; p++) qs += gray[p];
        g_mse_qmean = (uint8_t)(qs / FACE_PIX);
    }

    if (bi < 0) { *pid = 0; *score = 0.0f; g_mse_dmean = 0; g_mse_var = 0; return 0; }

#if MSE_USE_ZM
    g_mse_dmean = 0;                        /* 亮度已在算距离前消掉 */
    g_mse_var   = (uint16_t)((float)best_s / (float)FACE_PIX + 0.5f);
#else
    /* MSE_total = (dmean)^2 + var  ->  var = MSE - (dmean)^2 */
    g_mse_dmean = (int)(best_sd / FACE_PIX);
    {
        float var = (float)best_s / (float)FACE_PIX
                  - (float)g_mse_dmean * (float)g_mse_dmean;
        if (var < 0.0f) var = 0.0f;
        g_mse_var = (uint16_t)(var + 0.5f);
    }
#endif

    *pid   = g_train_pid[bi];
    *score = best;
    return (best <= g_th_mse) ? 1 : 0;      /* 1=通过, 0=不通过(拒识) */
}
