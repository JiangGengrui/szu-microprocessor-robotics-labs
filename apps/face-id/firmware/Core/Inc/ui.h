#ifndef __UI_H
#define __UI_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 界面层(face-id)
 *   ★ 显示分工: UI 与摄像头画面共用帧缓冲(0xC0000000), 摄像头只写取景框那
 *     480x480 区域 —— 写入范围互不重叠, 所以文字可以随时画、不会被下一帧冲掉,
 *     取景框以外永远是白底。不需要"停-画-续"(freeze/draw/restart)那套操作。
 *
 *   ★ 480x480 取景框下的垂直预算(共 600 行, 几乎占满, 必须精确分配):
 *       0  ~ 36   状态栏
 *       40 ~ 536  取景框(含外扩 8px 边框), 内部 ROI 48~528
 *       540~ 574  结果行 / 分数行 / 提示行
 *       574~ 600  留白
 *     按键提示因为底部没位置了, 移到取景框左侧的空白面板里(见 ui_bottom_hint)。
 * ========================================================================= */

typedef enum { ALG_MSE = 0, ALG_LBP, ALG_COLOR, ALG_PCA, ALG_NUM } algo_t;
typedef enum { MODE_PREVIEW = 0, MODE_ENROLL, MODE_RECOG, MODE_CALIB, MODE_BROWSE, MODE_NUM } app_mode_t;

#define UI_STATUS_H    36                         /* 状态栏高度(原 38, 为省一行空间压到 36) */
#define UI_MSG_Y       (ROI_BOX_Y0 + ROI_BOX_H + 8)   /* 取景框下边框 + 8 = 544 */
#define UI_HINT_Y      366                        /* 左侧面板按键提示起始行 */
#define UI_HINT_DY     32                         /* 按键提示行距(4 行) */

const char *ui_algo_name(algo_t a);               /* "MSE"/"LBP"/"COL"/"PCA" */
const char *ui_mode_name(app_mode_t m);           /* "预览"/"录入"/"识别"/"标定"/"回放" */

/* 取景框颜色(白底上必须用深色): 预览黑/录入绿/识别红/标定深蓝/回放棕 */
uint16_t ui_box_color(app_mode_t m);

/* 顶部状态栏(白底黑字) */
void ui_status_bar(app_mode_t m, int pid, int cnt, algo_t a);

/* 取景框两侧信息面板: 左边标题+设备标识, 右边当前模式/算法 */
void ui_side_panel(app_mode_t m, int pid, int cnt, algo_t a);

/* 底部按键提示行(白底黑字) */
void ui_bottom_hint(app_mode_t m);

/* 取景框(画在 Layer1 的 ROI 之外, 不会被摄像头画面盖住) */
void ui_roi_box(uint16_t color);

/* 提示信息(白底, 彩色字) */
void ui_message(const char *s, uint16_t color);

/* 识别结果行: tag="MSE"/"LBP"/"COL"/"PCA"
 * 注意: 本工程用 --specs=nano.specs, 默认关闭 printf 的浮点格式化(%f 打不出来),
 *       所以分数在这里先四舍五入成整数再显示, 标定时记录整数分数即可。
 * dmean/var: MSE 的分数分解(见 algo_mse.h)。传 -1 表示该算法不做分解, 不显示。 */
void ui_result_line(const char *tag, int pid, int score, int pass, int dmean, int var);

/* 标定模式的分数行: 只报分数, 不做通过/拒绝判定(用于阈值标定记录) */
void ui_calib_line(const char *tag, int pid, int score, int dmean, int var);

#endif
