# -*- coding: utf-8 -*-
"""生成界面中文字库 chinesefont.h
取模方式与现有ASCII字库一致: 逐列式/高位在前(C51格式)
字符集: 定时器与显示界面用字 (6个不重复汉字)
输出: Core/Inc/chinesefont.h (24x24 和 32x32 两种字号)
"""
from PIL import Image, ImageDraw, ImageFont
import os

CHARS = "定时器与显示"

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",      # 微软雅黑
    r"C:\Windows\Fonts\simhei.ttf",    # 黑体
    r"C:\Windows\Fonts\simsun.ttc",    # 宋体
]

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "firmware", "Core", "Inc", "chinesefont.h")   # 生成到本应用的 Core/Inc


def pick_font():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    raise SystemExit("no chinese font found")


def render_cn(ch, size, font_path):
    """渲染一个汉字到 size x size 位图"""
    img = Image.new("L", (size + 8, size + 8), 0)
    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(font_path, size)
    bbox = draw.textbbox((0, 0), ch, font=font)
    gw, gh = bbox[2] - bbox[0], bbox[3] - bbox[1]
    px = max(0, (size - gw) // 2)
    py = max(0, (size - gh) // 2)
    draw.text((px - bbox[0], py - bbox[1]), ch, font=font, fill=255)
    px_data = img.load()
    bmp = []
    for y in range(size):
        row = []
        for x in range(size):
            row.append(1 if px_data[x, y] > 96 else 0)
        bmp.append(row)
    return bmp


def to_bytes(bmp, size):
    """逐列式/高位在前 编码"""
    ceil8 = (size + 7) // 8
    data = []
    for c in range(size):
        for b in range(ceil8):
            byte = 0
            for r in range(8):
                yy = b * 8 + r
                if yy < size and bmp[yy][c]:
                    byte |= 0x80 >> r
            data.append(byte)
    return data


def preview(bmp, ch):
    print("--- %s ---" % ch)
    for row in bmp:
        print("".join("#" if p else "." for p in row))


def main():
    font_path = pick_font()
    print("font: %s" % font_path)
    chars = list(dict.fromkeys(CHARS))  # 去重保序
    print("chars(%d): %s" % (len(chars), "".join(chars)))

    data24 = {ch: to_bytes(render_cn(ch, 24, font_path), 24) for ch in chars}
    data32 = {ch: to_bytes(render_cn(ch, 32, font_path), 32) for ch in chars}

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("#ifndef __CHINESE_FONT_H\n#define __CHINESE_FONT_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* 中文字库: 逐列式/高位在前, 由 gen_chinesefont.py 生成\n")
        f.write(" * 字符集: %s\n" % "".join(chars))
        f.write(" * 24号: 每字24列x3字节=72字节; 32号: 每字32列x4字节=128字节 */\n\n")

        f.write("typedef struct { char name[4]; uint8_t data[72]; } cn24_t;\n")
        f.write("typedef struct { char name[4]; uint8_t data[128]; } cn32_t;\n\n")

        f.write("static const cn24_t cn24_font[] = {\n")
        for ch in chars:
            bs = ", ".join("0x%02X" % b for b in data24[ch])
            f.write('    {"%s", {%s}},\n' % (ch, bs))
        f.write("};\n\n")

        f.write("static const cn32_t cn32_font[] = {\n")
        for ch in chars:
            bs = ", ".join("0x%02X" % b for b in data32[ch])
            f.write('    {"%s", {%s}},\n' % (ch, bs))
        f.write("};\n\n")

        f.write("#define CN24_FONT_COUNT  (sizeof(cn24_font)/sizeof(cn24_font[0]))\n")
        f.write("#define CN32_FONT_COUNT  (sizeof(cn32_font)/sizeof(cn32_font[0]))\n\n")
        f.write("#endif\n")

    print("written: %s" % OUT)
    preview(render_cn("定", 24, font_path), "定(24)")
    preview(render_cn("锐", 24, font_path), "锐(24)")


if __name__ == "__main__":
    main()
