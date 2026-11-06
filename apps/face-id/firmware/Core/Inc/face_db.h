#ifndef __FACE_DB_H
#define __FACE_DB_H
#include "main.h"
#include "face_cfg.h"

/* =========================================================================
 * 人脸库(face-id 阶段五/六)
 *   - 持久化: 板载 SPI NOR Flash, 5 人 x 5 张 = 25 条记录, 每条独占 8KB(2 个扇区)
 *   - 内存训练集: g_train_gray[25][2304] + g_train_pid[25], 放在 SDRAM
 *   - 降级方案: 若 Flash 读不到 ID, 自动切换为"仅内存图库"(掉电丢失), 功能仍可用
 * ========================================================================= */

/* 内存训练集(在 SDRAM, 见 face_cfg.h 的 TRAIN_GRAY_BASE) */
extern uint8_t (*g_train_gray)[FACE_PIX];      /* 25 x 2304 灰度图 */
extern uint8_t  g_train_pid[FACE_MAX_SLOTS];   /* 每条对应的 1~5 号人员 */
extern int      g_train_cnt;                   /* 当前有效样本数(0~25) */
extern uint8_t  g_db_flash_ok;                 /* 1=NOR Flash 可用; 0=仅内存降级模式 */

/**
 * @brief  初始化图库: 读 Flash ID, 计算图库基址并清空内存训练集
 * @retval 0 = 成功(Flash 或降级内存模式均可读写); 1 = 失败
 */
uint8_t face_db_init(void);

/**
 * @brief  从 Flash 逐个槽位加载全部记录到内存训练集
 * @retval 成功加载的样本数
 */
int face_db_load_all(void);

/**
 * @brief  保存一张 48x48 RGB565 人脸图到图库(写 Flash + 刷新内存训练集)
 * @retval 0 = 成功; 1 = 失败
 */
int face_db_save(uint8_t pid, const uint16_t *img48);

/**
 * @brief  某人已录入张数
 */
int face_db_count(uint8_t pid);

/**
 * @brief  读取某人第 idx 张(0 起)的 RGB565 原图, 用于"图库回放"
 * @param  pid 1~FACE_MAX_PEOPLE;  idx 0~FACE_IMG_PER_PERSON-1
 * @param  dst 48x48 RGB565 输出缓冲(4608 字节)
 * @retval 0 = 成功
 * @retval -1 = 槽位为空 / 记录损坏(魔数或 CRC 不对)
 * @retval -2 = 仅内存降级模式(Flash 不可用时, RAM 里只留了灰度图与特征, 没有原图)
 * @note   原图只存在 NOR Flash 的槽位里, 所以这里必然是一次 Flash 读(约 4.6KB)。
 */
int face_db_read_image(uint8_t pid, uint8_t idx, uint16_t *dst);

/**
 * @brief  清空整个图库(擦除所有槽位 + 清内存训练集)
 */
void face_db_erase_all(void);

#endif
