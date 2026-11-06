# -*- coding: utf-8 -*-
"""
face-id 人脸识别系统 —— PC 端离线复现
=====================================
目的: 板上图库写在 NOR Flash 里, PC 端拿不到, 因此用公开人脸库 (ORL/AT&T,
      40 人 x 10 张, 92x112 灰度) 复现"取景框 -> 48x48 灰度 -> 四种距离"的
      完整链路, 得到与板上同一量纲的分数分布, 用于分析分数分布、选取阈值。

与板端代码的对应关系 (逐行对齐, 不是"类似"):
  img_roi_to_48()    -> rgb565_block10x10()   10x10 块平均, 保留 5/6/5 位域量化
  rgb565_to_gray()   -> y=(77R+150G+29B)>>8    扩位方式 (r<<3|r>>2) 完全一致
  algo_lbp_extract() -> lbp_feat()            4x4x59, 16 个 12x12 分区, 归一化 1000,
                                              边界 1..46 跳过, 非均匀归第 59 类
  algo_color_extract()-> col_feat()           3x16 bin, 归一化 1000
  chi2_dist()        -> chi2()                (a-b)^2/(a+b+1)
  algo_mse_match()   -> mse_score()           sum(d^2)/2304, MSE_USE_ZM=0
  algo_pca_train/match() -> pca_*()           Gram + 特征分解 + k=12 欧氏距离
"""

import json
import os
import sys

import numpy as np
from PIL import Image

FACE_W = 48
FACE_H = 48
FACE_PIX = FACE_W * FACE_H          # 2304
ROI_W = 480
ROI_H = 480
LBP_BLK = 4
LBP_CLASS = 59
LBP_DIM = LBP_BLK * LBP_BLK * LBP_CLASS      # 944
COLOR_BIN = 16
COLOR_DIM = 3 * COLOR_BIN                    # 48
PCA_K = 12

HERE = os.path.dirname(os.path.abspath(__file__))
ORL = os.path.join(HERE, "orl")

# ---- 数据划分: 1~5 号已录入, 26~29 号未录入 -------------------------------
# 图库: 5 人 x 5 张 = 25 槽(与板上一致)
# 标定集: 用于确定阈值, 已录入者 6/7 号图 10 张 + 未录入者 s26/s27 各 3 张
# 测试集: 只用一次, 已录入者 8/9 号图 10 张 + 未录入者 s28/s29 各 5 张
ENROLL = ["s1", "s2", "s3", "s4", "s5"]
ENROLL_IMG = [1, 2, 3, 4, 5]
CAL_GEN_IMG = [6, 7]
TEST_GEN_IMG = [8, 9]
CAL_STR = {"s26": [1, 2, 3], "s27": [1, 2, 3]}
TEST_STR = {"s28": [1, 2, 3, 4, 5], "s29": [1, 2, 3, 4, 5]}


# ============================ 预处理 ======================================
def load_gray(path):
    return np.asarray(Image.open(path).convert("L"), dtype=np.uint8)


def to_roi480(gray):
    """把人脸图铺满 480x480 取景框(板端 1:1 映射, 框内就是这一块传感器区域)"""
    im = Image.fromarray(gray).resize((ROI_W, ROI_H), Image.BILINEAR)
    return np.asarray(im, dtype=np.uint8)


def rgb565_block10x10(roi):
    """img_roi_to_48(): 10x10 块平均, 在 RGB565 的 5/6/5 位域上做整除"""
    v = roi.astype(np.uint32)
    r5 = v >> 3                    # 5 位
    g6 = v >> 2                    # 6 位
    b5 = v >> 3
    blk = ROI_W // FACE_W          # 10
    def avg(ch):
        x = ch.reshape(FACE_H, blk, FACE_W, blk).sum(axis=(1, 3))
        return (x // (blk * blk)).astype(np.uint16)
    r, g, b = avg(r5), avg(g6), avg(b5)
    return ((r << 11) | (g << 5) | b).astype(np.uint16)


def rgb565_to_gray(px):
    """BT.601 整数近似, 与 Core/Src/imgproc.c 完全一致"""
    px = px.astype(np.uint32)
    r = (px >> 11) & 0x1F
    g = (px >> 5) & 0x3F
    b = px & 0x1F
    r8 = (r << 3) | (r >> 2)
    g8 = (g << 2) | (g >> 4)
    b8 = (b << 3) | (b >> 2)
    return ((r8 * 77 + g8 * 150 + b8 * 29) >> 8).astype(np.uint8)


def pipeline(gray):
    """灰度图 -> 48x48 RGB565 / 48x48 灰度"""
    rgb48 = rgb565_block10x10(to_roi480(gray))
    return rgb48, rgb565_to_gray(rgb48).reshape(FACE_PIX)


# ============================ 四个算法 ====================================
def _lbp_map():
    m = np.zeros(256, dtype=np.uint8)
    nxt = 0
    for i in range(256):
        t = 0
        for k in range(8):
            a = (i >> k) & 1
            b = (i >> ((k + 1) & 7)) & 1
            if a != b:
                t += 1
        if t <= 2:
            m[i] = nxt
            nxt += 1
        else:
            m[i] = 255
    m[m == 255] = 58
    return m


LBP_MAP = _lbp_map()


def lbp_feat(img2d):
    """algo_lbp_extract(): 16 个 12x12 分区, 每区 59 bin, x1000/total"""
    i = img2d.astype(np.int32)
    code = np.zeros_like(i)
    dx = [-1, 0, 1, 1, 1, 0, -1, -1]
    dy = [-1, -1, -1, 0, 1, 1, 1, 0]
    for k in range(8):
        shifted = np.roll(np.roll(i, dy[k], axis=0), dx[k], axis=1)
        code |= ((shifted >= i).astype(np.int32) << k)
    cls = LBP_MAP[code]

    feat = np.zeros(LBP_DIM, dtype=np.uint16)
    blk = FACE_W // LBP_BLK        # 12
    for by in range(LBP_BLK):
        for bx in range(LBP_BLK):
            cnt = np.zeros(LBP_CLASS, dtype=np.uint32)
            total = 0
            for y in range(by * blk, (by + 1) * blk):
                if y < 1 or y > FACE_H - 2:
                    continue
                for x in range(bx * blk, (bx + 1) * blk):
                    if x < 1 or x > FACE_W - 2:
                        continue
                    cnt[cls[y, x]] += 1
                    total += 1
            off = (by * LBP_BLK + bx) * LBP_CLASS
            if total:
                feat[off:off + LBP_CLASS] = (cnt * 1000 // total).astype(np.uint16)
    return feat


def col_feat(rgb48):
    """algo_color_extract(): 3x16 bin, 归一化 1000"""
    p = rgb48.astype(np.uint32)
    r = (p >> 11) & 0x1F
    g = (p >> 5) & 0x3F
    b = p & 0x1F
    h = np.zeros((3, COLOR_BIN), dtype=np.uint32)
    np.add.at(h[0], (r * COLOR_BIN) >> 5, 1)
    np.add.at(h[1], (g * COLOR_BIN) >> 6, 1)
    np.add.at(h[2], (b * COLOR_BIN) >> 5, 1)
    feat = (h * 1000 // FACE_PIX).astype(np.uint16)
    return feat.reshape(COLOR_DIM)


def chi2(a, b):
    a = a.astype(np.float32)
    b = b.astype(np.float32)
    d = a - b
    return float((d * d / (a + b + 1.0)).sum())


def mse_score(q, t):
    d = q.astype(np.int32) - t.astype(np.int32)
    return float((d * d).sum()) / FACE_PIX


class PCA:
    """algo_pca_train/match(): 样本 Gram 矩阵 + 特征分解 + 低维欧氏距离"""

    def __init__(self, gallery):
        X = gallery.astype(np.float32)            # N x 2304
        self.N = X.shape[0]
        self.mean = X.mean(axis=0)
        A = X - self.mean
        G = A @ A.T                               # N x N, 与板端一致
        lam, V = np.linalg.eigh(G.astype(np.float64))
        lam = lam.astype(np.float32)
        V = V.astype(np.float32)
        order = np.argsort(-lam)
        lam, V = lam[order], V[:, order]
        k = min(PCA_K, self.N - 1)
        self.k = 0
        for j in range(k):
            if lam[j] < 1e-3:
                break
            self.k += 1
        lam, V = lam[:self.k], V[:, :self.k]
        self.eigval = lam
        self.eigface = (V.T @ A) / np.sqrt(lam)[:, None]        # k x 2304
        self.train_proj = np.sqrt(lam)[None, :] * V              # N x k
        self.train_pid = np.array([i // 5 for i in range(self.N)])

    def score(self, gray):
        wq = self.eigface @ (gray.astype(np.float32) - self.mean)
        d = np.sqrt(((wq[None, :] - self.train_proj) ** 2).sum(axis=1))
        return float(d.min()), int(self.train_pid[d.argmin()])


# ============================ 评估流程 ====================================
def build_gallery():
    names, rgbs, grays, pids = [], [], [], []
    for pi, sub in enumerate(ENROLL):
        for k, img in enumerate(ENROLL_IMG):
            g = load_gray(os.path.join(ORL, sub, f"{img}.pgm"))
            rgb48, gr = pipeline(g)
            names.append(f"{sub}_{img}")
            rgbs.append(rgb48)
            grays.append(gr)
            pids.append(pi)
    return names, np.array(rgbs), np.array(grays), np.array(pids)


def probe_set():
    """返回 [(label, pid_or_-1, rgb48, gray, split)]"""
    out = []
    for split, imgs in (("cal", CAL_GEN_IMG), ("test", TEST_GEN_IMG)):
        for pi, sub in enumerate(ENROLL):
            for img in imgs:
                g = load_gray(os.path.join(ORL, sub, f"{img}.pgm"))
                rgb48, gr = pipeline(g)
                out.append((f"{sub}_{img}", pi, rgb48, gr, split))
    for split, table in (("cal", CAL_STR), ("test", TEST_STR)):
        for sub, imgs in table.items():
            for img in imgs:
                g = load_gray(os.path.join(ORL, sub, f"{img}.pgm"))
                rgb48, gr = pipeline(g)
                out.append((f"{sub}_{img}", -1, rgb48, gr, split))
    return out


def pick_threshold(genuine, impostor):
    """阈值取法: 可分时 min/max<3 取算术中点, >=3 取几何平均;
       两类重叠时按标定集上的等错误率(EER)取点, 不碰测试集"""
    g_max, i_min = max(genuine), min(impostor)
    if g_max < i_min:
        ratio = i_min / g_max
        if ratio < 3.0:
            return (g_max + i_min) / 2.0, g_max, i_min, "算术中点", True
        return float(np.sqrt(g_max * i_min)), g_max, i_min, "几何平均", True
    cands = np.unique(np.concatenate([genuine, impostor]))
    best = None
    for th in cands:
        frr = sum(1 for v in genuine if v > th) / len(genuine)
        fpr = sum(1 for v in impostor if v <= th) / len(impostor)
        key = (abs(frr - fpr), frr + fpr)
        if best is None or key < best[0]:
            best = (key, th, frr, fpr)
    _, th, frr, fpr = best
    return float(th), g_max, i_min, f"EER(FRR={frr:.0%},FPR={fpr:.0%})", False


def main():
    if not os.path.isdir(ORL):
        raise SystemExit(
            f"未找到 ORL 数据集: {ORL}\n"
            "请将 ORL/AT&T 人脸库解压为 tools/orl/s1/1.pgm ... s40/10.pgm，\n"
            "或修改脚本顶部的 ORL 常量指向实际路径。"
        )
    names, gal_rgb, gal_gray, gal_pid = build_gallery()
    print(f"图库: {len(names)} 张, {len(set(gal_pid.tolist()))} 人")

    # 预计算图库特征
    lbp_g = np.array([lbp_feat(g.reshape(FACE_H, FACE_W)) for g in gal_gray])
    col_g = np.array([col_feat(r) for r in gal_rgb])
    pca = PCA(gal_gray)
    print(f"PCA: N={pca.N}, k={pca.k}")

    probes = probe_set()
    rows = []
    for label, pid, rgb48, gr, split in probes:
        d_mse = min(mse_score(gr, t) for t in gal_gray)
        d_lbp = min(chi2(lbp_feat(gr.reshape(FACE_H, FACE_W)), t) for t in lbp_g)
        d_col = min(chi2(col_feat(rgb48), t) for t in col_g)
        d_pca, _ = pca.score(gr)
        rows.append(dict(label=label, gen=(pid >= 0), split=split, mse=d_mse,
                         lbp=d_lbp, col=d_col, pca=d_pca))

    result = {}
    for alg in ["mse", "lbp", "col", "pca"]:
        # 阈值只由标定集决定
        cgen = [r[alg] for r in rows if r["gen"] and r["split"] == "cal"]
        cimp = [r[alg] for r in rows if not r["gen"] and r["split"] == "cal"]
        th, g_max, i_min, rule, sep = pick_threshold(cgen, cimp)

        # 测试集只用一次
        tgen = [r[alg] for r in rows if r["gen"] and r["split"] == "test"]
        timp = [r[alg] for r in rows if not r["gen"] and r["split"] == "test"]
        tp = sum(1 for v in tgen if v <= th)
        fp = sum(1 for v in timp if v <= th)

        result[alg] = dict(
            gen_max=g_max, gen_min=min(cgen), gen_mean=float(np.mean(cgen)),
            imp_min=i_min, imp_max=max(cimp), imp_mean=float(np.mean(cimp)),
            th=th, rule=rule, separable=sep,
            n_cgen=len(cgen), n_cimp=len(cimp),
            n_gen=len(tgen), n_imp=len(timp), tp=tp, fp=fp,
            tpr=tp / len(tgen), frr=1 - tp / len(tgen), fpr=fp / len(timp),
            test_gen_max=max(tgen), test_imp_min=min(timp),
        )

    print("\n[标定集] 1~5 号各 2 张(10 张) + s26/s27 各 3 张(6 张)")
    print("{:<5}{:>9}{:>9}{:>9}{:>9}{:>10}{:>7}{:>14}".format(
        "alg", "本人max", "本人min", "外人min", "外人max", "阈值", "可分", "取法"))
    for alg, d in result.items():
        print("{:<5}{:>9.1f}{:>9.1f}{:>9.1f}{:>9.1f}{:>10.1f}{:>7}{:>14}".format(
            alg.upper(), d["gen_max"], d["gen_min"], d["imp_min"], d["imp_max"],
            d["th"], "是" if d["separable"] else "否", d["rule"]))

    print("\n[测试集] 1~5 号各 2 张(10 张) + s28/s29 各 5 张(10 张)")
    print("{:<5}{:>10}{:>10}{:>10}{:>12}".format("alg", "通过", "正确通过率", "误拒率", "误通过率"))
    for alg, d in result.items():
        print("{:<5}{:>10}{:>11.0%}{:>10.0%}{:>13.0%}".format(
            alg.upper(), f"{d['tp']}/{d['n_gen']}", d["tpr"], d["frr"], d["fpr"]))

    with open(os.path.join(HERE, "result.json"), "w", encoding="utf-8") as f:
        json.dump(dict(result=result, rows=rows), f, ensure_ascii=False, indent=2)
    print("\n-> result.json")


if __name__ == "__main__":
    sys.exit(main())
