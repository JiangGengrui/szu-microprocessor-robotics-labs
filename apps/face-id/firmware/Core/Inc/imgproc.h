#ifndef __IMGPROC_H
#define __IMGPROC_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 图像预处理(face-id 阶段四)
 *   取景框图像(480x480 RGB565, 行跨度为整屏行宽 1024) --块平均--> 48x48 RGB565
 *                                                          --灰度--> 48x48 uint8
 *   录入与识别必须使用完全相同的裁剪/缩放方式, 否则分数不可比。
 * ========================================================================= */

/* RGB565 单像素 -> 8 位灰度 (ITU-R BT.601 整数近似) */
uint8_t rgb565_to_gray(uint16_t px);

/* 把取景框图像块平均缩放为 48x48, 写入 out48[FACE_PIX]
 *   src    : 取景框左上角像素 = camera_roi_ptr()
 *   stride : 取景框所在缓冲的行跨度(像素) = camera_roi_stride()
 *            (取景框直接存在于整屏帧缓冲里, 所以行跨度是 1024, 不是 480) */
void img_roi_to_48(const uint16_t *src, uint16_t stride, uint16_t *out48);

/* 48x48 RGB565 -> 48x48 灰度 */
void img_48_to_gray(const uint16_t *rgb48, uint8_t *gray48);

/* 在给定 RGB565 帧缓冲上把 48x48 灰度图放大回显(5x5 块), 便于调试验证预处理 */
void img_show_gray_zoom(uint16_t *fb, const uint8_t *gray48, uint16_t x, uint16_t y, uint16_t scale);

#endif
