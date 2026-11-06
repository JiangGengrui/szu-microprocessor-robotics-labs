#include "imgproc.h"

/* =========================================================================
 * 图像预处理实现
 * ========================================================================= */

/**
 * @brief  RGB565 -> 8 位灰度
 * @note   BT.601 整数近似: Y = (77*R + 150*G + 29*B) >> 8
 */
uint8_t rgb565_to_gray(uint16_t px)
{
    uint32_t r = (px >> 11) & 0x1F;             /* 5 位 */
    uint32_t g = (px >> 5)  & 0x3F;             /* 6 位 */
    uint32_t b =  px        & 0x1F;             /* 5 位 */
    uint32_t r8 = (r << 3) | (r >> 2);          /* 扩到 8 位 */
    uint32_t g8 = (g << 2) | (g >> 4);
    uint32_t b8 = (b << 3) | (b >> 2);
    return (uint8_t)((r8 * 77U + g8 * 150U + b8 * 29U) >> 8);
}

/**
 * @brief  取景框图像块平均缩放到 48x48
 * @param  src    : 取景框左上角像素(= camera_roi_ptr())
 * @param  stride : 行跨度(像素)。取景框直接存在于整屏帧缓冲里, 所以是 1024
 *                  (= camera_roi_stride()), 不是 480 —— 这一点写错会取到
 *                  相邻行的像素, 图像会斜切, 识别分数全乱。
 * @param  out48  : 输出 48x48 RGB565, 共 FACE_PIX 个 uint16_t
 * @note  ROI_W/FACE_W = 10, 每个输出像素由 10x10 = 100 个输入像素取平均,
 *         等效一次 10x10 均值滤波, 明显抑制摄像头噪声, 比"最近邻抽点"稳。
 *         ★ 代价: 48x48 = 2304 个像素, 只有取景框 480x480 = 230400 个的 1%,
 *           所以回放时看到的"马赛克块"就是这 100:1 压缩的必然结果(每块 10x10),
 *           不是显示 bug。
 */
void img_roi_to_48(const uint16_t *src, uint16_t stride, uint16_t *out48)
{
    for (int oy = 0; oy < FACE_H; oy++)
    {
        int sy0 = oy * ROI_H / FACE_H;
        int sy1 = (oy + 1) * ROI_H / FACE_H;
        for (int ox = 0; ox < FACE_W; ox++)
        {
            int sx0 = ox * ROI_W / FACE_W;
            int sx1 = (ox + 1) * ROI_W / FACE_W;
            uint32_t sr = 0, sg = 0, sb = 0, n = 0;

            for (int y = sy0; y < sy1; y++)
            {
                const uint16_t *row = src + (uint32_t)y * stride;
                for (int x = sx0; x < sx1; x++)
                {
                    uint16_t p = row[x];
                    sr += (p >> 11) & 0x1F;
                    sg += (p >> 5)  & 0x3F;
                    sb +=  p        & 0x1F;
                    n++;
                }
            }
            if (n == 0) n = 1;
            uint16_t r = (uint16_t)(sr / n);
            uint16_t g = (uint16_t)(sg / n);
            uint16_t b = (uint16_t)(sb / n);
            out48[oy * FACE_W + ox] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

/**
 * @brief  48x48 RGB565 -> 48x48 灰度
 */
void img_48_to_gray(const uint16_t *rgb48, uint8_t *gray48)
{
    for (int i = 0; i < FACE_PIX; i++)
        gray48[i] = rgb565_to_gray(rgb48[i]);
}

/**
 * @brief  把 48x48 灰度图按 scale 倍放大画到帧缓冲(调试用, 目视检查裁剪是否正确)
 */
void img_show_gray_zoom(uint16_t *fb, const uint8_t *gray48,
                        uint16_t x, uint16_t y, uint16_t scale)
{
    for (int oy = 0; oy < FACE_H; oy++)
    {
        for (int ox = 0; ox < FACE_W; ox++)
        {
            uint8_t g = gray48[oy * FACE_W + ox];
            uint16_t y8 = (uint16_t)((g >> 3) & 0x1F);
            uint16_t c  = (uint16_t)((y8 << 11) | ((g >> 2) << 5) | y8);   /* 灰度 RGB565 */
            for (uint16_t dy = 0; dy < scale; dy++)
            {
                uint16_t *row = fb + (uint32_t)(y + oy * scale + dy) * FRAME_W + x + ox * scale;
                for (uint16_t dx = 0; dx < scale; dx++) row[dx] = c;
            }
        }
    }
}
