#!/usr/bin/env python3
"""合成一张用于演示的通用「桌面」图像。

不用任何真实截图：全部是几何图形，避免泄露使用者的桌面内容，
也让演示图在任何机器上都能复现。
"""
import math
from PIL import Image, ImageDraw, ImageFilter

W, H = 1600, 1000
S = 2  # 先按 2 倍画再缩小，得到抗锯齿边缘


def rounded(d, box, r, fill, outline=None, width=1):
    d.rounded_rectangle(box, radius=r, fill=fill, outline=outline, width=width)


def main(out="/tmp/desktop.png"):
    img = Image.new("RGB", (W * S, H * S))
    d = ImageDraw.Draw(img)

    # 壁纸：对角渐变，深蓝 -> 紫
    for y in range(H * S):
        t = y / (H * S)
        for_x = t
        r = int(28 + 40 * for_x)
        g = int(34 + 18 * for_x)
        b = int(74 + 60 * for_x)
        d.line([(0, y), (W * S, y)], fill=(r, g, b))

    # 背景装饰：几个柔和的圆
    glow = Image.new("RGB", (W * S, H * S), (0, 0, 0))
    gd = ImageDraw.Draw(glow)
    for cx, cy, rad, col in [(0.18, 0.22, 260, (70, 110, 220)),
                             (0.82, 0.30, 200, (150, 80, 200)),
                             (0.62, 0.86, 300, (40, 130, 190))]:
        gd.ellipse([cx * W * S - rad * S, cy * H * S - rad * S,
                    cx * W * S + rad * S, cy * H * S + rad * S], fill=col)
    glow = glow.filter(ImageFilter.GaussianBlur(120 * S))
    img = Image.blend(img, Image.blend(img, glow, 0.55), 0.55)
    d = ImageDraw.Draw(img)

    # 主窗口
    wx, wy, ww, wh = int(0.12 * W * S), int(0.13 * H * S), int(0.56 * W * S), int(0.66 * H * S)
    rounded(d, [wx, wy, wx + ww, wy + wh], 16 * S, (30, 32, 40), (58, 62, 78), 2 * S)
    rounded(d, [wx, wy, wx + ww, wy + 46 * S], 16 * S, (42, 45, 58))
    d.rectangle([wx, wy + 30 * S, wx + ww, wy + 46 * S], fill=(42, 45, 58))
    for i, c in enumerate([(240, 96, 92), (245, 190, 80), (110, 200, 120)]):
        cx = wx + (26 + i * 26) * S
        d.ellipse([cx - 7 * S, wy + 16 * S, cx + 7 * S, wy + 30 * S], fill=c)
    # 标题栏文字占位
    rounded(d, [wx + 120 * S, wy + 17 * S, wx + 300 * S, wy + 29 * S], 6 * S, (70, 74, 92))

    # 侧栏
    rounded(d, [wx + 16 * S, wy + 62 * S, wx + 200 * S, wy + wh - 16 * S], 10 * S, (36, 38, 48))
    for i in range(7):
        yy = wy + (82 + i * 34) * S
        rounded(d, [wx + 32 * S, yy, wx + 184 * S - (0 if i % 3 else 60) * S, yy + 14 * S],
                7 * S, (58, 62, 78) if i else (86, 120, 220))

    # 正文：标题 + 段落条
    x0 = wx + 224 * S
    rounded(d, [x0, wy + 70 * S, x0 + 300 * S, wy + 92 * S], 8 * S, (96, 102, 124))
    for row in range(11):
        yy = wy + (112 + row * 30) * S
        w = ww - (224 + 24) * S
        if row % 5 == 4:
            w = int(w * 0.45)
        elif row % 3 == 2:
            w = int(w * 0.78)
        rounded(d, [x0, yy, x0 + w, yy + 13 * S], 6 * S, (62, 66, 84))

    # 第二窗口
    vx, vy, vw, vh = int(0.58 * W * S), int(0.46 * H * S), int(0.34 * W * S), int(0.40 * H * S)
    rounded(d, [vx, vy, vx + vw, vy + vh], 16 * S, (26, 28, 36), (58, 62, 78), 2 * S)
    rounded(d, [vx, vy, vx + vw, vy + 42 * S], 16 * S, (40, 43, 56))
    d.rectangle([vx, vy + 28 * S, vx + vw, vy + 42 * S], fill=(40, 43, 56))
    for i, c in enumerate([(240, 96, 92), (245, 190, 80), (110, 200, 120)]):
        cx = vx + (24 + i * 24) * S
        d.ellipse([cx - 6 * S, vy + 15 * S, cx + 6 * S, vy + 27 * S], fill=c)
    for row in range(7):
        yy = vy + (60 + row * 30) * S
        w = vw - 48 * S if row % 4 else int(vw * 0.42)
        rounded(d, [vx + 24 * S, yy, vx + 24 * S + w, yy + 13 * S], 6 * S, (58, 62, 78))

    # 底部面板
    py = int(0.915 * H * S)
    rounded(d, [int(0.06 * W * S), py, int(0.94 * W * S), py + 52 * S], 14 * S, (22, 24, 32), (54, 58, 72), 2 * S)
    for i in range(9):
        cx = int(0.10 * W * S) + i * 44 * S
        col = (86, 120, 220) if i == 2 else (62, 66, 84)
        rounded(d, [cx, py + 12 * S, cx + 30 * S, py + 40 * S], 8 * S, col)
    # 右侧状态区
    for i in range(3):
        cx = int(0.88 * W * S) + i * 30 * S
        rounded(d, [cx, py + 18 * S, cx + 18 * S, py + 34 * S], 5 * S, (62, 66, 84))

    img = img.resize((W, H), Image.LANCZOS)
    img.save(out)
    print(f"  {out}  {img.size[0]}x{img.size[1]}")


if __name__ == "__main__":
    import sys
    main(sys.argv[1] if len(sys.argv) > 1 else "/tmp/desktop.png")
