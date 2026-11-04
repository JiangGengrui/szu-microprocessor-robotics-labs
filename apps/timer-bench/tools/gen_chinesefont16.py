# -*- coding: utf-8 -*-
"""为OLED生成16x16中文字库 chinesefont16.h
取模方式与现有ASCII字库一致: 逐列式/高位在前(C51格式), 每字16列x2字节=32字节
字符集: 定时器与显示计数 (8个不重复汉字)
输出: Core/Inc/chinesefont16.h
"""
from PIL import Image, ImageDraw, ImageFont
import os

CHARS = "定时器与显示计数"

FONT_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\simsun.ttc",
]

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "firmware", "Core", "Inc", "chinesefont16.h")   # 生成到本应用的 Core/Inc


def pick_font():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    raise SystemExit("no chinese font found")


def render_cn(ch, size, font_path):
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


def main():
    font_path = pick_font()
    print("font: %s" % font_path)
    chars = list(dict.fromkeys(CHARS))
    print("chars(%d): %s" % (len(chars), "".join(chars)))

    with open(OUT, "w", encoding="utf-8") as f:
        f.write("#ifndef __CHINESE_FONT16_H\n#define __CHINESE_FONT16_H\n\n")
        f.write("#include <stdint.h>\n\n")
        f.write("/* 16x16中文字库(OLED用): 逐列式/高位在前, 每字32字节\n")
        f.write(" * 字符集: %s */\n\n" % "".join(chars))
        f.write("typedef struct { char name[4]; uint8_t data[32]; } cn16_t;\n\n")
        f.write("static const cn16_t cn16_font[] = {\n")
        for ch in chars:
            bs = ", ".join("0x%02X" % b for b in to_bytes(render_cn(ch, 16, font_path), 16))
            f.write('    {"%s", {%s}},\n' % (ch, bs))
        f.write("};\n\n")
        f.write("#define CN16_FONT_COUNT  (sizeof(cn16_font)/sizeof(cn16_font[0]))\n\n")
        f.write("#endif\n")
    print("written: %s" % OUT)


if __name__ == "__main__":
    main()
