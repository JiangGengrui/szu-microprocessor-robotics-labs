#include "ui.h"
#include "lcd.h"
#include "face_db.h"
#include "algo_pca.h"       /* 只为在状态栏显示 PCA 实际主成分个数 g_pca_k */
#include <stdio.h>

/* =========================================================================
 * 界面层实现(白底黑字版)
 *   本工程 lcd.h 的 API 签名(与官方 7 参数版不同, 注意):
 *     void lcd_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);  x,y=左上角, w,h=宽高
 *     void lcd_show_string(uint16_t x, uint16_t y, uint8_t size, char *str, uint16_t color);
 *     void lcd_show_cn_string(uint16_t x, uint16_t y, uint8_t size, const char *str, uint16_t color);
 *         size = 24 或 32; 支持中英文混排
 *
 * ★ 显示分工(见 camera.h):
 *   本文件所有绘制都落在帧缓冲(0xC0000000), 摄像头只写取景框那 480x480 一块。
 *   两者写入范围互不重叠, 所以:
 *     - 文字可以随时画, 不会被下一帧摄像头数据冲掉(UI 常显, 不闪);
 *     - 取景框以外永远是 lcd_clear(WHITE) 的白底。
 *   页面配色: 白底 + 黑/深蓝字, 视觉上"框外一律白底"。
 * ========================================================================= */

#define UI_GRAY_LINE    0xC618      /* 浅灰分隔线 */
#define UI_TITLE_COLOR  DARKBLUE

const char *ui_algo_name(algo_t a)
{
    switch (a)
    {
    case ALG_MSE:   return "MSE";
    case ALG_LBP:   return "LBP";
    case ALG_COLOR: return "COL";
    default:        return "PCA";
    }
}

const char *ui_mode_name(app_mode_t m)
{
    switch (m)
    {
    case MODE_ENROLL: return "录入";
    case MODE_RECOG:  return "识别";
    case MODE_CALIB:  return "标定";
    case MODE_BROWSE: return "回放";
    default:          return "预览";
    }
}

/**
 * @brief 取景框颜色: 白底上必须挑深色, 否则框看不见
 */
uint16_t ui_box_color(app_mode_t m)
{
    switch (m)
    {
    case MODE_ENROLL: return GREEN;
    case MODE_RECOG:  return RED;
    case MODE_CALIB:  return DARKBLUE;
    case MODE_BROWSE: return BROWN;
    default:          return BLACK;
    }
}

/**
 * @brief 顶部状态栏(白底黑字 + 一条浅灰下划线)
 */
void ui_status_bar(app_mode_t m, int pid, int cnt, algo_t a)
{
    char s[64];

    lcd_fill(0, 0, FRAME_W, UI_STATUS_H, WHITE);              /* 只清顶部那一条 */
    lcd_fill(0, UI_STATUS_H - 2, FRAME_W, 2, UI_GRAY_LINE);

    lcd_show_cn_string(8, 6, 24, ui_mode_name(m), BLACK);

    if (m == MODE_ENROLL)
    {
        sprintf(s, " %d号  第%d/%d张", pid, cnt, FACE_IMG_PER_PERSON);
        lcd_show_cn_string(150, 6, 24, s, BLACK);
    }
    else if (m == MODE_BROWSE)
    {
        /* 回放: 分母用"该人实际已录张数", 不是上限 5 */
        sprintf(s, " %d号  共%d张", pid, cnt);
        lcd_show_cn_string(150, 6, 24, s, BLACK);
    }
    else if (m == MODE_RECOG || m == MODE_CALIB)
    {
        if (a == ALG_PCA)
            sprintf(s, " 算法 PCA k=%d", g_pca_k);            /* k = 实际使用的主成分个数(表 9.4) */
        else
            sprintf(s, " 算法 %s", ui_algo_name(a));
        lcd_show_cn_string(150, 6, 24, s, BLACK);
    }
    else
    {
        lcd_show_cn_string(150, 6, 24, " 按K1进入下一模式", BLACK);
    }

    sprintf(s, "图库 %d/%d", g_train_cnt, FACE_MAX_SLOTS);
    lcd_show_cn_string(620, 6, 24, s, BLACK);

    lcd_show_cn_string(880, 6, 24, g_db_flash_ok ? "存储正常" : "仅内存",
                       g_db_flash_ok ? BLACK : RED);
}

/**
 * @brief 取景框左右两侧的信息面板(标题 / 设备标识 / 当前模式参数)
 * @note  480x480 取景框占 x=272~751(含边框 264~759), 所以左边 x<=236、
 *        右边 x>=776 是可用的白底区(各留 16px 呼吸边)。
 */
void ui_side_panel(app_mode_t m, int pid, int cnt, algo_t a)
{
    char s[48];

    /* ---- 左: 标题 + 设备标识 ---- */
    lcd_show_cn_string(40, 92,  32, "人脸识别", UI_TITLE_COLOR);
    lcd_show_cn_string(40, 132, 32, "系统", UI_TITLE_COLOR);
    lcd_show_string(40, 196, 24, (char *)CFG_STU_NAME, BLACK);
    lcd_show_string(40, 230, 24, (char *)CFG_STU_ID,   BLACK);
    lcd_show_string(40, 286, 24, (char *)"STM32F429", BLACK);
    lcd_show_string(40, 320, 24, (char *)"OV5640", BLACK);

    /* ---- 右: 当前模式 + 参数 ---- */
    lcd_show_cn_string(776, 92, 32, ui_mode_name(m), UI_TITLE_COLOR);

    if (m == MODE_ENROLL)
    {
        sprintf(s, "%d号 %d/%d张", pid, cnt, FACE_IMG_PER_PERSON);
        lcd_show_cn_string(776, 148, 24, s, BLACK);
        lcd_show_cn_string(776, 182, 24, "对准方框按K0", BLACK);
    }
    else if (m == MODE_RECOG || m == MODE_CALIB)
    {
        sprintf(s, "算法 %s", ui_algo_name(a));
        lcd_show_cn_string(776, 148, 24, s, BLACK);
        lcd_show_cn_string(776, 182, 24, "对准方框按K0", BLACK);
    }
    else if (m == MODE_BROWSE)
    {
        sprintf(s, "%d号 共%d张", pid, cnt);
        lcd_show_cn_string(776, 148, 24, s, BLACK);
        lcd_show_cn_string(776, 182, 24, "库内原图", BLACK);
    }
    else
    {
        lcd_show_cn_string(776, 148, 24, "实时预览", BLACK);
        lcd_show_cn_string(776, 182, 24, "K1切换模式", BLACK);
    }
}

/**
 * @brief 按键提示(4 行, 竖排在取景框左侧的空白面板里)
 * @note  480x480 取景框把屏幕纵向几乎占满(40~536), 底部再放一条横排提示会
 *        和"结果行"抢位置, 所以改到左侧面板 STM32F429/OV5640 下方竖排。
 *        函数名沿用 ui_bottom_hint, 调用点不用改。
 */
void ui_bottom_hint(app_mode_t m)
{
    const char *l0, *l1, *l2, *l3;

    if (m == MODE_ENROLL)
    {
        l0 = "K0 拍照保存"; l1 = "K1 切模式";
        l2 = "K2 换人";     l3 = "WK长按 清空图库";
    }
    else if (m == MODE_RECOG || m == MODE_CALIB)
    {
        l0 = "K0 拍照识别"; l1 = "K1 切模式";
        l2 = "K2 换算法";   l3 = "WK长按 清空图库";
    }
    else if (m == MODE_BROWSE)
    {
        l0 = "K0 下一张";   l1 = "K1 切模式";
        l2 = "K2 换人";     l3 = "WK长按 回预览";
    }
    else
    {
        l0 = "K0 无";       l1 = "K1 切模式";
        l2 = "K2 无";       l3 = "WK长按 回首页";
    }

    lcd_fill(40, UI_HINT_Y - 14, 200, 2, UI_GRAY_LINE);      /* 与上方信息隔开 */
    lcd_show_cn_string(40, UI_HINT_Y + 0 * UI_HINT_DY, 24, l0, BLACK);
    lcd_show_cn_string(40, UI_HINT_Y + 1 * UI_HINT_DY, 24, l1, BLACK);
    lcd_show_cn_string(40, UI_HINT_Y + 2 * UI_HINT_DY, 24, l2, BLACK);
    lcd_show_cn_string(40, UI_HINT_Y + 3 * UI_HINT_DY, 24, l3, BLACK);
}

/**
 * @brief 取景框(3 像素粗的方框, 画在 ROI 之外 -> 不会被摄像头画面盖住)
 */
void ui_roi_box(uint16_t color)
{
    lcd_fill(ROI_BOX_X0,             ROI_BOX_Y0,                ROI_BOX_W, 3, color);     /* 上边 */
    lcd_fill(ROI_BOX_X0,             ROI_BOX_Y0 + ROI_BOX_H - 3, ROI_BOX_W, 3, color);    /* 下边 */
    lcd_fill(ROI_BOX_X0,             ROI_BOX_Y0,                3, ROI_BOX_H, color);      /* 左边 */
    lcd_fill(ROI_BOX_X0 + ROI_BOX_W - 3, ROI_BOX_Y0,            3, ROI_BOX_H, color);      /* 右边 */
}

/**
 * @brief 提示信息(白底, 颜色字)
 */
void ui_message(const char *s, uint16_t color)
{
    lcd_fill(0, UI_MSG_Y - 4, FRAME_W, 34, WHITE);
    lcd_show_cn_string(8, UI_MSG_Y, 24, s, color);
}

/**
 * @brief 识别结果行
 * @param dmean 与最佳模板的亮度差(有符号); -1 = 本算法不做分解, 只显示分数
 * @param var   扣掉亮度差后的结构差异
 * @note  为什么要多显示这两个数: MSE 分数 = 亮度分量 + 结构分量。同一张脸出现
 *        "分数差好几倍"时, 看这两个数就能立刻定性 —— 亮度分量占大头说明是曝光/
 *        光照变了(该查 AEC), 结构分量占大头才是姿势/位置/尺度变了。
 */
void ui_result_line(const char *tag, int pid, int score, int pass, int dmean, int var)
{
    char s[96];

    if (dmean >= 0)
        sprintf(s, "%s 编号%d 分数%d 亮度%s%d 噪声%d %s", tag, pid, score,
                dmean < 0 ? "-" : "+", dmean < 0 ? -dmean : dmean, var,
                pass ? "通过" : "拒绝");
    else
        sprintf(s, "%s 编号%d 分数%d %s", tag, pid, score, pass ? "通过" : "拒绝");

    lcd_fill(0, UI_MSG_Y - 4, FRAME_W, 34, WHITE);
    lcd_show_cn_string(8, UI_MSG_Y, 24, s, pass ? GREEN : RED);
}

/**
 * @brief 标定模式的分数行(只报分数, 不判通过/拒绝)
 */
void ui_calib_line(const char *tag, int pid, int score, int dmean, int var)
{
    char s[96];

    if (dmean >= 0)
        sprintf(s, "%s 编号%d 分数%d 亮度%s%d 噪声%d 标定", tag, pid, score,
                dmean < 0 ? "-" : "+", dmean < 0 ? -dmean : dmean, var);
    else
        sprintf(s, "%s 编号%d 分数%d 标定", tag, pid, score);

    lcd_fill(0, UI_MSG_Y - 4, FRAME_W, 34, WHITE);
    lcd_show_cn_string(8, UI_MSG_Y, 24, s, DARKBLUE);
}
