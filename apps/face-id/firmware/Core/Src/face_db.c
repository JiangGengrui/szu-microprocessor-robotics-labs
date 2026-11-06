#include "face_db.h"
#include "norflash.h"
#include "imgproc.h"
#include "algo_lbp.h"          /* 需要 g_color_feat / algo_color_extract */
#include <string.h>

/* =========================================================================
 * 人脸库实现
 *   记录格式(每条 16 字节头 + 4608 字节图 = 4624 字节, 独占 8KB 槽位)
 *     0  magic(u32)  = 0x46414345 "FACE"
 *     4  version(u16)= 1
 *     6  person_id(u8) = 1~5
 *     7  reserved(u8) = 0
 *     8  width(u16) = 48
 *    10  height(u16)= 48
 *    12  img_len(u16)= 4608
 *    14  crc(u16)   = 图像数据 CRC16-CCITT
 *    16  img[4608]  = 48x48 RGB565 原始字节
 * ========================================================================= */

#define FACE_MAGIC      0x46414345UL
#define FACE_VERSION    1

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  person_id;
    uint8_t  reserved;
    uint16_t width;
    uint16_t height;
    uint16_t img_len;
    uint16_t crc;
} face_rec_t;                                   /* sizeof = 16, 无填充 */

/* ---- 内存训练集(SDRAM) ---- */
uint8_t (*g_train_gray)[FACE_PIX] = (uint8_t (*)[FACE_PIX])TRAIN_GRAY_BASE;
uint8_t  g_train_pid[FACE_MAX_SLOTS];
int      g_train_cnt = 0;
uint8_t  g_db_flash_ok = 0;

/* ---- 内部状态 ---- */
static uint32_t g_face_base   = FACE_FLASH_BASE;   /* 实际使用的图库起始字节地址 */
static int16_t  g_slot_idx[FACE_MAX_SLOTS];        /* 槽位 -> 内存训练集下标(-1 表示空槽) */
static uint8_t  g_img_buf[FACE_RGB_BYTES];         /* Flash 读写中转(勿放栈上) */
static uint16_t g_rgb48_tmp[FACE_PIX];             /* RGB565 中转缓冲(勿放栈上) */

/* Flash 芯片容量(字节) -> 图库起始地址; 容量不足时按比例下移, 保证落在芯片范围内 */
static uint32_t face_base_for_chip(uint16_t type)
{
    uint8_t cap = (uint8_t)(type & 0xFF);          /* W25Qxx: 0x13=1MB 0x14=2MB 0x15=4MB
                                                    *         0x16=8MB 0x17=16MB 0x18=32MB */
    if (cap >= 0x18) return 0x00C00000UL;          /* 32MB: 用 12MB 处 */
    if (cap == 0x17) return 0x00700000UL;          /* 16MB: 用 7MB 处  */
    if (cap == 0x16) return 0x00300000UL;          /*  8MB: 用 3MB 处  */
    if (cap == 0x15) return 0x00080000UL;          /*  4MB: 用 512KB 处 */
    return 0x00040000UL;                           /*  2MB 及以下 */
}

static uint16_t crc16_ccitt(const uint8_t *d, uint32_t n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= (uint16_t)(*d++) << 8;
        for (int i = 0; i < 8; i++)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static uint32_t slot_addr(int slot) { return g_face_base + (uint32_t)slot * FACE_SLOT_SIZE; }
static int      slot_of(uint8_t pid, int idx) { return (pid - 1) * FACE_IMG_PER_PERSON + idx; }

/**
 * @brief 初始化图库
 */
uint8_t face_db_init(void)
{
    norflash_init();                                    /* 官方 norflash_init() 返回 void */

    if (g_norflash_type == 0x0000 || g_norflash_type == 0xFFFF)
    {
        g_db_flash_ok = 0;                              /* 读不到 ID -> 降级为仅内存图库 */
    }
    else
    {
        g_db_flash_ok = 1;
        g_face_base   = face_base_for_chip(g_norflash_type);
    }

    for (int i = 0; i < FACE_MAX_SLOTS; i++) g_slot_idx[i] = -1;
    g_train_cnt = 0;
    return 0;                                           /* 两种模式都可用, 由 g_db_flash_ok 区分 */
}

/* 读取一条记录并做完整性校验; 成功则写入内存训练集 */
static int load_one(int slot)
{
    face_rec_t rec;
    uint32_t addr = slot_addr(slot);

    norflash_read((uint8_t *)&rec, addr, sizeof(rec));
    if (rec.magic != FACE_MAGIC || rec.version != FACE_VERSION) return -1;
    if (rec.img_len != FACE_RGB_BYTES || rec.width != FACE_W || rec.height != FACE_H) return -1;
    if (rec.person_id < 1 || rec.person_id > FACE_MAX_PEOPLE) return -1;

    norflash_read(g_img_buf, addr + sizeof(rec), FACE_RGB_BYTES);
    if (crc16_ccitt(g_img_buf, FACE_RGB_BYTES) != rec.crc) return -1;

    if (g_train_cnt < FACE_MAX_SLOTS)
    {
        int idx = g_train_cnt++;
        memcpy(g_rgb48_tmp, g_img_buf, FACE_RGB_BYTES);
        img_48_to_gray(g_rgb48_tmp, g_train_gray[idx]);
        algo_color_extract(g_rgb48_tmp, g_color_feat[idx]);
        g_train_pid[idx] = rec.person_id;
        g_slot_idx[slot] = (int16_t)idx;
    }
    return 0;
}

/**
 * @brief 从 Flash 逐槽加载全部记录
 */
int face_db_load_all(void)
{
    for (int i = 0; i < FACE_MAX_SLOTS; i++) g_slot_idx[i] = -1;
    g_train_cnt = 0;

    if (!g_db_flash_ok) return 0;
    for (int s = 0; s < FACE_MAX_SLOTS; s++) load_one(s);
    return g_train_cnt;
}

/**
 * @brief 某人已录入张数
 */
int face_db_count(uint8_t pid)
{
    int c = 0;
    for (int i = 0; i < g_train_cnt; i++) if (g_train_pid[i] == pid) c++;
    return c;
}

/**
 * @brief 保存一张人脸(写 Flash + 刷新内存训练集)
 * @note  选槽策略: 该人第 1 个空槽; 5 个都满则覆盖最后一张
 */
int face_db_save(uint8_t pid, const uint16_t *img48)
{
    if (pid < 1 || pid > FACE_MAX_PEOPLE) return 1;

    int slot = -1;
    for (int i = 0; i < FACE_IMG_PER_PERSON; i++)
    {
        int s = slot_of(pid, i);
        if (g_slot_idx[s] < 0) { slot = s; break; }
    }
    if (slot < 0) slot = slot_of(pid, FACE_IMG_PER_PERSON - 1);   /* 已满: 覆盖最后一张 */

    memcpy(g_img_buf, img48, FACE_RGB_BYTES);

    if (g_db_flash_ok)
    {
        face_rec_t rec;
        rec.magic     = FACE_MAGIC;
        rec.version   = FACE_VERSION;
        rec.person_id = pid;
        rec.reserved  = 0;
        rec.width     = FACE_W;
        rec.height    = FACE_H;
        rec.img_len   = FACE_RGB_BYTES;
        rec.crc       = crc16_ccitt(g_img_buf, FACE_RGB_BYTES);

        /* 整槽重写: 先擦该槽的 2 个扇区, 再写"头 + 图" */
        uint32_t addr = slot_addr(slot);
        norflash_erase_sector(addr / 4096U);
        norflash_erase_sector(addr / 4096U + 1U);
        norflash_write((uint8_t *)&rec, addr, sizeof(rec));
        norflash_write(g_img_buf, addr + sizeof(rec), FACE_RGB_BYTES);

        /* 读回校验(保证"写入成功"是真的成功) */
        face_rec_t chk;
        norflash_read((uint8_t *)&chk, addr, sizeof(chk));
        norflash_read(g_img_buf, addr + sizeof(rec), FACE_RGB_BYTES);
        if (chk.magic != FACE_MAGIC ||
            crc16_ccitt(g_img_buf, FACE_RGB_BYTES) != chk.crc)
            return 1;
    }

    /* 刷新内存训练集(灰度和颜色特征同时对齐) */
    memcpy(g_rgb48_tmp, g_img_buf, FACE_RGB_BYTES);

    int idx = g_slot_idx[slot];
    if (idx < 0)
    {
        if (g_train_cnt >= FACE_MAX_SLOTS) return 1;
        idx = g_train_cnt++;
        g_slot_idx[slot] = (int16_t)idx;
    }
    g_train_pid[idx] = pid;
    img_48_to_gray(g_rgb48_tmp, g_train_gray[idx]);
    algo_color_extract(g_rgb48_tmp, g_color_feat[idx]);
    return 0;
}

/**
 * @brief  读取某人第 idx 张(0 起)的 RGB565 原图(图库回放用)
 */
int face_db_read_image(uint8_t pid, uint8_t idx, uint16_t *dst)
{
    face_rec_t rec;
    uint32_t   addr;
    int        slot;

    if (pid < 1 || pid > FACE_MAX_PEOPLE || idx >= FACE_IMG_PER_PERSON) return -1;

    slot = slot_of(pid, idx);
    if (g_slot_idx[slot] < 0) return -1;          /* 空槽: 该人还没录到这一张 */
    if (!g_db_flash_ok)       return -2;          /* 仅内存模式: RAM 里没有原图 */

    addr = slot_addr(slot);
    norflash_read((uint8_t *)&rec, addr, sizeof(rec));
    if (rec.magic != FACE_MAGIC || rec.version != FACE_VERSION) return -1;
    if (rec.img_len != FACE_RGB_BYTES || rec.width != FACE_W || rec.height != FACE_H) return -1;

    norflash_read((uint8_t *)dst, addr + sizeof(rec), FACE_RGB_BYTES);
    if (crc16_ccitt((uint8_t *)dst, FACE_RGB_BYTES) != rec.crc) return -1;

    return 0;
}

/**
 * @brief 清空图库
 */
void face_db_erase_all(void)
{
    if (g_db_flash_ok)
    {
        uint32_t s = g_face_base / 4096U;
        uint32_t n = (FACE_SLOT_SIZE * FACE_MAX_SLOTS + 4095U) / 4096U;   /* 50 个扇区 */
        for (uint32_t i = 0; i < n; i++) norflash_erase_sector(s + i);
    }
    for (int i = 0; i < FACE_MAX_SLOTS; i++) g_slot_idx[i] = -1;
    g_train_cnt = 0;
}
