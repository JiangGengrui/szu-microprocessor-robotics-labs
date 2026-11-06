#include "face_app.h"
#include "camera.h"
#include "key.h"
#include "lcd.h"
#include "imgproc.h"
#include "face_db.h"
#include "algo_mse.h"
#include "algo_lbp.h"
#include "algo_pca.h"
#include <stdio.h>
#include <string.h>

/* =========================================================================
 * 应用状态机实现
 *   显示策略(单图层, 靠"写入范围"分工):
 *     UI 与摄像头画面共用同一块帧缓冲(0xC0000000)。摄像头只写取景框那
 *     480x480 区域(camera_line_cb 里限定 y/宽), 框外一个像素都不碰。
 *     因此: 文字随时可画、永远常显; 框外始终是白底; 摄像头不受 UI 绘制影响。
 *   只有"要抓一张稳定照片"时(录入/识别)才 camera_freeze() 停采集,
 *     取完图、画完结果、让用户看清后将 camera_start() 恢复实时预览。
 * ========================================================================= */

static app_mode_t g_mode      = MODE_PREVIEW;
static algo_t     g_alg       = ALG_MSE;
static uint8_t    g_pid       = 1;        /* 录入对象(1~5) */
static uint8_t    g_bpid      = 1;        /* 回放: 当前查看的人(1~5) */
static uint8_t    g_bidx      = 0;        /* 回放: 当前查看的第几张(0 起) */
static uint8_t    g_cam_ok    = 0;
static uint8_t    g_pca_ready = 0;

/* 大数组一律文件级 static, 不放栈上 */
static uint16_t g_rgb48[FACE_PIX];
static uint8_t  g_gray[FACE_PIX];

/* ---------------- LED: 绿=LED1(PB0) 通过; 红=LED0(PB1) 拒绝 ---------------- */
static void led_pass(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);   /* 绿亮 */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);     /* 红灭 */
}
static void led_reject(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);   /* 红亮 */
}
static void led_off(void)
{
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
}

/**
 * @brief 重画整页: 白底 + 状态栏 + 两侧面板 + 取景框 + 底部提示
 * @note  只在按键/状态变化时调用。摄像头只写框内区域, 所以重画不会与画面打架
 */
static void draw_page(void)
{
    /* 回放看的是 g_bpid, 其他模式是录入对象 g_pid: 状态栏/侧栏要显示对应的人 */
    uint8_t pid = (g_mode == MODE_BROWSE) ? g_bpid : g_pid;

    lcd_clear(WHITE);                                  /* 框外一律白底 */
    ui_status_bar(g_mode, pid, face_db_count(pid), g_alg);
    ui_side_panel(g_mode, pid, face_db_count(pid), g_alg);
    ui_roi_box(ui_box_color(g_mode));
    ui_bottom_hint(g_mode);
}

/**
 * @brief 丢掉若干帧, 直到"框内画面平均亮度连续两帧变化 <= 2"
 * @note  为什么不再用"固定丢 3 帧": 本工程传感器输出 1024x600, DVP 带宽大,
 *        实测帧率只有十几 fps, 3 帧往往要 200~300ms, 而旧实现的 200ms 超时
 *        会让它提前放弃 —— 等于没丢帧, 抓到的是 AEC 还没收敛的画面。
 *        改成"看亮度是否稳定"既可靠又不依赖具体帧率: 收敛了立刻抓,
 *        没收敛最多等 12 帧(~0.8s)再兜底。
 */
static void camera_settle(void)
{
    uint8_t  frame;
    uint8_t  stable = 0;
    uint16_t last   = camera_roi_mean();

    for (frame = 0; frame < 12U; frame++)
    {
        uint32_t t0 = HAL_GetTick();
        uint16_t m, d;

        while (!camera_new_frame())
        {
            if (HAL_GetTick() - t0 > 200U) return;    /* 等不到新帧就放弃, 不阻塞 */
        }

        m = camera_roi_mean();
        d = (m > last) ? (uint16_t)(m - last) : (uint16_t)(last - m);
        last = m;

        if (d <= 2U)
        {
            if (++stable >= 2U) return;               /* 连续两帧稳定 -> 可以拍了 */
        }
        else
        {
            stable = 0;                               /* 还在变, 重新计 */
        }
    }
}

/**
 * @brief 冻结一帧并取出 48x48 人脸图(尚未画任何 UI, 与画面一致)
 */
static void snapshot(void)
{
    camera_settle();                                   /* 等曝光收敛再抓 */
    camera_freeze();                                   /* 只停采集, 框内画面定格 */
    /* 取景框就在整屏帧缓冲里, 行跨度是 1024, 必须传 stride */
    img_roi_to_48(camera_roi_ptr(), camera_roi_stride(), g_rgb48);
    img_48_to_gray(g_rgb48, g_gray);
}

/* ---------------- 图库回放 ---------------- */
/* 回放对象: 从 NOR Flash 把 48x48 原图读回来, 放大 ROI_SCALE(=10) 倍显示在
 * 取景框里, 用来核对"录进去的到底是什么"。原图只存在 Flash 槽位中, RAM 里
 * 只有灰度 + 特征, 所以必须走 face_db_read_image() 而不是内存训练集。
 * g_bpid / g_bidx 在文件开头声明(要在 draw_page 之前可见)。 */
static uint16_t g_browse_line[ROI_W];              /* 放大行缓冲(960 B), 不放栈上 */

/* 回放用"整倍最近邻放大", 所以 ROI 必须恰好是 48 的整数倍(当前 480 = 48x10)。
 * 若以后改了 ROI_W/ROI_H 而除不尽, 会留下未画的边或越界, 这里直接编译期拦住。 */
#if (FACE_W * ROI_SCALE != ROI_W) || (FACE_H * ROI_SCALE != ROI_H)
#error "图库回放要求 ROI_W/ROI_H 能被 FACE_W/FACE_H 整除(见 face_cfg.h)"
#endif

/**
 * @brief 把 48x48 RGB565 最近邻放大 ROI_SCALE 倍, 画进取景框位置
 * @note  直接写主帧缓冲: 先按行把 48 个像素横向扩展成 480 像素(一行),
 *        再把这一行纵向重复 10 次。总写入量 480x480x2 = 450 KB, 与一帧
 *        摄像头画面相当, 肉眼无延迟。
 */
static void browse_paint(const uint16_t *img48)
{
    uint16_t *fb = lcd_get_framebuf();
    uint16_t  y, x, s, k;

    for (y = 0; y < FACE_H; y++)
    {
        const uint16_t *src = img48 + (uint32_t)y * FACE_W;

        for (x = 0; x < FACE_W; x++)
            for (s = 0; s < ROI_SCALE; s++)
                g_browse_line[x * ROI_SCALE + s] = src[x];

        for (k = 0; k < ROI_SCALE; k++)
        {
            uint16_t *dst = fb + ((uint32_t)ROI_Y0 + (uint32_t)y * ROI_SCALE + k) * LCD_WIDTH
                            + ROI_X0;
            memcpy(dst, g_browse_line, (uint32_t)ROI_W * 2U);
        }
    }
}

/**
 * @brief 刷新回放画面: 读图 + 放大显示 + 更新提示行
 */
static void browse_show(void)
{
    char s[48];
    int  n  = face_db_count(g_bpid);
    int  rc;

    if (n <= 0)                                     /* 该人还没录 -> 清空框内并提示 */
    {
        lcd_fill(ROI_X0, ROI_Y0, ROI_W, ROI_H, WHITE);
        sprintf(s, "%d号 无样本", g_bpid);
        ui_message(s, RED);
        return;
    }

    if (g_bidx >= (uint8_t)n) g_bidx = 0;           /* 换人后索引越界 -> 回第 1 张 */

    rc = face_db_read_image(g_bpid, g_bidx, g_rgb48);
    if (rc == -2)
    {
        lcd_fill(ROI_X0, ROI_Y0, ROI_W, ROI_H, WHITE);
        ui_message("仅内存模式 无原图", RED);
        return;
    }
    if (rc != 0)
    {
        lcd_fill(ROI_X0, ROI_Y0, ROI_W, ROI_H, WHITE);
        ui_message("读取失败", RED);
        return;
    }

    browse_paint(g_rgb48);
    sprintf(s, "%d号 第%d/%d张", g_bpid, g_bidx + 1, n);
    ui_message(s, BROWN);
}

/* ---------------- 图库变化后重建特征 ---------------- */
static void rebuild_features(void)
{
    algo_lbp_init_map();
    algo_feat_rebuild_all();                           /* LBP 特征(颜色特征由 face_db 维护) */
    g_pca_ready = (algo_pca_train() == 0);
}

/* ---------------- 算法分发 ---------------- */
static int run_algo(algo_t a, uint8_t *pid, float *score)
{
    switch (a)
    {
    case ALG_MSE:   return algo_mse_match(g_gray, pid, score);
    case ALG_LBP:   return algo_lbp_match(g_gray, pid, score);
    case ALG_COLOR: return algo_color_match(g_rgb48, pid, score);
    case ALG_PCA:
        if (!g_pca_ready) { *pid = 0; *score = 0.0f; return 0; }
        return algo_pca_match(g_gray, pid, score);
    default:        return 0;
    }
}

/* WK_UP 长按检测: 返回 1 = 已按住 ms 毫秒 */
static uint8_t wk_long_press(uint32_t ms)
{
    uint32_t t0 = HAL_GetTick();
    while (HAL_GPIO_ReadPin(WK_UP_GPIO_Port, WK_UP_Pin) == GPIO_PIN_SET)
    {
        if (HAL_GetTick() - t0 >= ms) return 1;
        HAL_Delay(10);
    }
    return 0;
}

void face_app_init(uint8_t cam_ok)
{
    g_cam_ok = cam_ok;
    led_off();

    face_db_init();                                    /* 读 Flash ID + 定界图库区 */
    face_db_load_all();                                /* Flash 不可用时返回 0 */

    algo_lbp_init_map();
    algo_feat_rebuild_all();
    g_pca_ready = (algo_pca_train() == 0);
}

uint8_t face_app_flash_ok(void) { return g_db_flash_ok; }

void face_app_enter(void)
{
    g_mode = MODE_PREVIEW;
    g_alg  = ALG_MSE;
    draw_page();                                       /* 先把白底页面铺好, 再开摄像头 */
    ui_message("K1 切换模式", BLACK);
}

void face_app_leave(void)
{
    /* 摄像头图层由 camera_stop() 关闭; 首页由 main 重画, 这里无需处理 */
}

/**
 * @brief 主循环轮询
 */
uint8_t face_app_poll(void)
{
    uint8_t k = key_scan();
    if (k == 0) return 0;

    /* ---------- WK_UP: 长按 -> 回首页 / 清空图库 ---------- */
    if (k == WK_UP)
    {
        if (wk_long_press(1500))
        {
            if (g_mode == MODE_PREVIEW)
            {
                return 1;                              /* 交给 main 回首页 */
            }
            else if (g_mode == MODE_BROWSE)
            {
                /* 回放模式下长按不清库(看图中误擦代价太大), 只回实时预览 */
                g_mode = MODE_PREVIEW;
                camera_start();                        /* 回预览要恢复采集 */
                draw_page();
                ui_message("已回预览", BLACK);
            }
            else
            {
                camera_freeze();
                draw_page();
                ui_message("正在清空图库...", RED);      /* 擦 50 个扇区要 2~5 秒, 先给提示 */
                face_db_erase_all();
                g_pca_ready = 0;
                led_off();
                draw_page();
                ui_message("图库已清空", RED);
                HAL_Delay(1200);
                camera_start();
            }
        }
        else
        {
            if (g_mode == MODE_PREVIEW)                       ui_message("长按WK回首页", RED);
            else if (g_mode == MODE_BROWSE)                   ui_message("长按WK回预览", RED);
            else                                              ui_message("长按WK清空图库", RED);
            HAL_Delay(800);
            draw_page();                               /* 0.8 秒后把提示擦掉 */
            if (g_mode == MODE_BROWSE) browse_show();  /* 擦提示时别把回放图清掉 */
        }
        return 0;
    }

    /* ---------- KEY1: 模式循环(不停摄像头, 画面继续实时) ---------- */
    if (k == KEY1)
    {
        g_mode = (app_mode_t)((g_mode + 1) % MODE_NUM);
        led_off();

        /* 回放要用取景框这块屏显图, 必须停采集(否则下一帧就把它盖了);
         * 从回放回到预览则要重新开采集。其余模式切换不用动摄像头。 */
        if (g_mode == MODE_BROWSE)      camera_freeze();
        else if (g_mode == MODE_PREVIEW) camera_start();

        draw_page();
        if (g_mode == MODE_BROWSE)
        {
            g_bidx = 0;
            browse_show();
        }
        else if (g_mode == MODE_ENROLL && !g_db_flash_ok)
            ui_message("图库仅内存(掉电丢失)", RED);
        else
            ui_message("已切换模式", BLACK);
        return 0;
    }

    /* ---------- KEY2: 换人 / 换算法 ---------- */
    if (k == KEY2)
    {
        if (g_mode == MODE_ENROLL)
        {
            char s[48];
            g_pid = (uint8_t)((g_pid % FACE_MAX_PEOPLE) + 1);
            draw_page();
            sprintf(s, "%d号 已录%d张", g_pid, face_db_count(g_pid));
            ui_message(s, BLACK);
        }
        else if (g_mode == MODE_RECOG || g_mode == MODE_CALIB)
        {
            g_alg = (algo_t)((g_alg + 1) % ALG_NUM);
            draw_page();
            ui_message(ui_algo_name(g_alg), BLACK);
        }
        else if (g_mode == MODE_BROWSE)
        {
            g_bpid = (uint8_t)((g_bpid % FACE_MAX_PEOPLE) + 1);
            g_bidx = 0;
            draw_page();
            browse_show();
        }
        return 0;
    }

    /* ---------- KEY0: 拍照(存图 / 识别) / 回放翻张 ---------- */
    if (k == KEY0)
    {
        /* 回放不依赖摄像头, 所以放在摄像头检查之前 */
        if (g_mode == MODE_BROWSE)
        {
            int n = face_db_count(g_bpid);
            if (n > 0) g_bidx = (uint8_t)((g_bidx + 1) % n);   /* 循环翻张 */
            browse_show();
            return 0;
        }

        if (!g_cam_ok) { ui_message("摄像头异常", RED); return 0; }

        if (g_mode == MODE_ENROLL)
        {
            snapshot();                                /* 冻结 + 取图 */
            if (face_db_save(g_pid, g_rgb48) == 0)
            {
                rebuild_features();                    /* 图库变了, 重建 LBP/PCA */
                draw_page();
                camera_start();                        /* ★ 取完图立刻恢复预览(见下方说明) */
                {
                    char s[48];
                    sprintf(s, "已保存 %d号 %d/%d", g_pid,
                            face_db_count(g_pid), FACE_IMG_PER_PERSON);
                    ui_message(s, GREEN);
                }
            }
            else
            {
                draw_page();
                camera_start();
                ui_message("保存失败", RED);
            }
            HAL_Delay(800);
        }
        else if (g_mode == MODE_RECOG || g_mode == MODE_CALIB)
        {
            if (g_train_cnt < 2)
            {
                draw_page();
                ui_message("样本不足, 请先录入", RED);
                HAL_Delay(1200);
                return 0;
            }

            snapshot();
            draw_page();
            /* ★ 关键: 取完 48x48 就立刻恢复采集, 不要等 1.5s 显示结束。
             *   原因: 停采期间传感器仍在自动曝光, 停得越久, 恢复后 AEC 需要
             *   重收敛的幅度越大; 而这 1.5s 的差异在"标定"和"识别"两种操作
             *   节奏下并不相同(标定要抄分数、看得久), 于是同一张脸会拿到
             *   明显不同的分数。把停采时长压到取图那几毫秒, 这个变量就消失了。 */
            camera_start();
            {
                uint8_t pid = 0; float score = 0.0f;
                int pass = run_algo(g_alg, &pid, &score);
                int dm   = (g_alg == ALG_MSE) ? g_mse_dmean : -1;
                int vr   = (g_alg == ALG_MSE) ? (int)g_mse_var : 0;

                if (g_mode == MODE_CALIB)
                {
                    /* 标定: 只显示分数供记录, 不做通过/拒绝判定 */
                    ui_calib_line(ui_algo_name(g_alg), pid, (int)(score + 0.5f), dm, vr);
                    led_off();
                }
                else
                {
                    if (pass) led_pass(); else led_reject();
                    ui_result_line(ui_algo_name(g_alg), pid, (int)(score + 0.5f), pass, dm, vr);
                }
            }
            HAL_Delay(1500);
            led_off();                                 /* 别让 LED 一直亮着干扰下一次测光 */
        }
        else /* MODE_PREVIEW */
        {
            ui_message("按K1切到录入/识别模式", BLACK);
            HAL_Delay(800);
            draw_page();
        }
    }
    return 0;
}
