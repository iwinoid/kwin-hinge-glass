#!/usr/bin/env python3
"""把离线预览器渲染的效果帧合成为笔记本演示动图。

几何是真的投影：底座躺在 z=0 平面上，屏幕绕 x 轴的铰链转动，
用透视相机投影后把效果画面按单应变换贴到屏幕四边形上。
也就是说屏幕自身的倾斜和画面里的折叠透视是两回事，叠加在一起更像实物。

用法：
    python3 tools/demo/make_demo.py <帧目录> <输出.webp>
"""
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

# ---- 画布与笔记本尺寸（任意单位，只有比例有意义）----
W, H = 960, 600
LAP_W = 1.00          # 机身宽度
LID_H = 0.625         # 屏幕高度（16:10）
BASE_D = LID_H        # 键盘面进深。真实笔记本这块与屏幕高度大致相等
BEZEL = 0.022         # 屏幕边框

BG_TOP = (18, 20, 28)
BG_BOT = (28, 31, 44)
SCREEN_BG = (12, 13, 18)
BASE_COL = (58, 61, 74)
BASE_EDGE = (86, 90, 108)


def look_at(eye, target, up=(0.0, 1.0, 0.0)):
    eye = np.asarray(eye, dtype=float)
    target = np.asarray(target, dtype=float)
    f = target - eye
    f /= np.linalg.norm(f)
    s = np.cross(f, np.asarray(up, dtype=float))
    s /= np.linalg.norm(s)
    u = np.cross(s, f)
    m = np.eye(4)
    m[0, :3], m[1, :3], m[2, :3] = s, u, -f
    m[:3, 3] = -m[:3, :3] @ eye
    return m


def project(pts, view, fov=26.0):
    """pts: (N,3) 世界坐标 -> (N,2) 画布像素坐标"""
    pts = np.atleast_2d(np.asarray(pts, dtype=float))
    cam = (view @ np.hstack([pts, np.ones((len(pts), 1))]).T).T[:, :3]
    depth = np.maximum(-cam[:, 2], 1e-4)
    f = 1.0 / np.tan(np.radians(fov) / 2.0)
    aspect = W / H
    x_ndc = (f / aspect) * cam[:, 0] / depth
    y_ndc = f * cam[:, 1] / depth
    return np.stack([(x_ndc * 0.5 + 0.5) * W, (0.5 - y_ndc * 0.5) * H], axis=1)


def find_coeffs(dst, src):
    """PIL PERSPECTIVE 系数：输出四边形 dst 的每个像素去 src 四边形取色。"""
    m = []
    for (dx, dy), (sx, sy) in zip(dst, src):
        m.append([dx, dy, 1, 0, 0, 0, -sx * dx, -sx * dy])
        m.append([0, 0, 0, dx, dy, 1, -sy * dx, -sy * dy])
    a = np.asarray(m, dtype=float)
    b = np.asarray(src, dtype=float).reshape(8)
    return np.linalg.solve(a, b).tolist()


def lid_frame(angle_deg):
    """返回屏幕（含边框）、底座、键盘区的世界坐标角点。

    相机在 -y 一侧，所以「朝向使用者」是 -y：
      * 底座由铰链往 -y 伸出
      * 角度自底座平面量起：0 = 合上（屏幕倒扣在底座上），
        90 = 竖直，100 = 正常使用的后仰
    """
    a = np.radians(angle_deg)
    d = np.array([0.0, -np.cos(a), np.sin(a)])       # 屏幕由铰链伸出的方向
    half = LAP_W / 2.0
    top = d * LID_H                                  # 盖板外沿的上边

    # 盖板外沿尺寸 = 机身尺寸（宽 LAP_W，高 LID_H）。
    # 曾经写成 half + BEZEL，那是把边框往**外**扩，盖板因此比机身宽了 4.4%。
    # 边框要往里缩，显示区再从盖板外沿内缩一圈。
    bezel = [np.array([-half, 0, 0]), np.array([half, 0, 0]),
             np.array([half, 0, 0]) + top, np.array([-half, 0, 0]) + top]

    # 显示区。四角顺序必须与图像的「左上、右上、右下、左下」一致，
    # 否则单应变换会把画面上下颠倒。
    sw = half - BEZEL
    screen = [np.array([-sw, 0, 0]) + d * (LID_H - BEZEL),   # 左上
              np.array([sw, 0, 0]) + d * (LID_H - BEZEL),    # 右上
              np.array([sw, 0, 0]) + d * BEZEL,              # 右下（靠铰链）
              np.array([-sw, 0, 0]) + d * BEZEL]             # 左下（靠铰链）

    # 底座：由铰链往使用者方向伸出
    base = [np.array([-half, 0.0, 0.0]), np.array([half, 0.0, 0.0]),
            np.array([half, -BASE_D, 0.0]), np.array([-half, -BASE_D, 0.0])]
    # 键盘区（底座内缩）
    kx = half * 0.82
    key = [np.array([-kx, -0.10, 0.0]), np.array([kx, -0.10, 0.0]),
           np.array([kx, -BASE_D * 0.86, 0.0]), np.array([-kx, -BASE_D * 0.86, 0.0])]
    return bezel, screen, base, key


VIEW = look_at(eye=(0.0, -2.70, 1.62), target=(0.0, -0.31, 0.26))


def background():
    img = Image.new("RGB", (W, H))
    d = ImageDraw.Draw(img)
    for y in range(H):
        t = y / (H - 1)
        d.line([(0, y), (W, y)],
               fill=tuple(int(BG_TOP[i] + (BG_BOT[i] - BG_TOP[i]) * t) for i in range(3)))
    return img


LID_BACK = (44, 47, 57)      # A 面（上盖外表面）


def screen_facing(angle_deg):
    """屏幕法线与视线的夹角余弦。>0 表示屏幕朝向相机，<=0 表示看到的是 A 面。

    笔记本合上时屏幕是朝下的，所以低角度下必须显示 A 面，
    否则会出现「A 面透出桌面内容」这种物理上不存在的画面。
    """
    a = np.radians(angle_deg)
    normal = np.array([0.0, -np.sin(a), -np.cos(a)])      # 屏幕正面朝向
    _, screen, _, _ = lid_frame(angle_deg)
    center = np.mean(screen, axis=0)
    eye = np.array([0.0, -2.70, 1.62])
    v = eye - center
    v /= np.linalg.norm(v)
    return float(np.dot(v, normal))


def compose(screen_img, angle_deg):
    bezel, screen, base, key = lid_frame(angle_deg)
    pb = project(bezel, VIEW)
    ps = project(screen, VIEW)
    pbase = project(base, VIEW)
    pkey = project(key, VIEW)
    facing = screen_facing(angle_deg)

    img = background()
    d = ImageDraw.Draw(img)

    # 底座投影
    shadow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(shadow).polygon([tuple(p) for p in pbase], fill=110)
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    img.paste(Image.new("RGB", (W, H), (0, 0, 0)), (0, 0), shadow)
    d = ImageDraw.Draw(img)

    # 底座
    d.polygon([tuple(p) for p in pbase], fill=BASE_COL, outline=BASE_EDGE)
    d.polygon([tuple(p) for p in pkey], fill=(46, 48, 59))

    # 键盘：五行按键的横向分隔线
    kx = LAP_W / 2.0 * 0.78
    rows = 5
    for i in range(1, rows):
        t = i / rows
        y0, y1 = -0.105, -0.335
        yy = y0 + (y1 - y0) * t
        a = project([[-kx, yy, 0.0]], VIEW)[0]
        b = project([[kx, yy, 0.0]], VIEW)[0]
        d.line([tuple(a), tuple(b)], fill=(58, 61, 74), width=2)
    # 键位列
    for i in range(1, 14):
        t = i / 14
        xx = -kx + 2 * kx * t
        a = project([[xx, -0.105, 0.0]], VIEW)[0]
        b = project([[xx, -0.335, 0.0]], VIEW)[0]
        d.line([tuple(a), tuple(b)], fill=(58, 61, 74), width=1)
    # 触控板
    tp = project([[-0.16, -0.40, 0.0], [0.16, -0.40, 0.0],
                  [0.16, -0.50, 0.0], [-0.16, -0.50, 0.0]], VIEW)
    d.polygon([tuple(p) for p in tp], fill=(52, 55, 67), outline=(70, 74, 90))

    # 盖板。朝向相机时是屏幕边框，背朝相机时是 A 面。
    if facing > 0:
        d.polygon([tuple(p) for p in pb], fill=SCREEN_BG, outline=(70, 74, 90))
    else:
        d.polygon([tuple(p) for p in pb], fill=LID_BACK, outline=(96, 100, 118))
        # A 面上的印记。必须画在 A 面所在的平面上再投影 ——
        # 直接画 2D 圆的话它会永远正对相机，看起来像浮在空中。
        a = np.radians(angle_deg)
        up = np.array([0.0, -np.cos(a), np.sin(a)])       # 盖板平面内的「上」方向
        right = np.array([1.0, 0.0, 0.0])                 # 盖板平面内的「右」方向
        center = up * (LID_H * 0.5)
        ring = [center + 0.052 * (np.cos(t) * right + np.sin(t) * up)
                for t in np.linspace(0, 2 * np.pi, 32)]
        ring_px = [tuple(p) for p in project(ring, VIEW)]
        d.polygon(ring_px, outline=(78, 82, 98))
        core = [center + 0.018 * (np.cos(t) * right + np.sin(t) * up)
                for t in np.linspace(0, 2 * np.pi, 24)]
        d.polygon([tuple(p) for p in project(core, VIEW)], fill=(78, 82, 98))

    # 画面按单应变换贴到屏幕上。掠射角附近让画面淡出，
    # 这样关闭过程的最后一小段自然过渡到 A 面，不会突然跳变。
    if facing > 0:
        alpha = min(1.0, facing / 0.18)
        src = [(0, 0), (screen_img.width, 0),
               (screen_img.width, screen_img.height), (0, screen_img.height)]
        coeffs = find_coeffs([tuple(p) for p in ps], src)
        warped = screen_img.transform((W, H), Image.PERSPECTIVE, coeffs, Image.BICUBIC)

        mask = Image.new("L", (W, H), 0)
        ImageDraw.Draw(mask).polygon([tuple(p) for p in ps], fill=int(255 * alpha))
        img.paste(warped, (0, 0), mask)
    return img


def main():
    frames_dir = sys.argv[1] if len(sys.argv) > 1 else "/tmp/demo"
    out_path = sys.argv[2] if len(sys.argv) > 2 else "image/README/demo.webp"
    lids = [float(x) for x in open(os.path.join(frames_dir, "lids.txt")) if x.strip()]

    # θ = 45 × (100 − 开合角) / 100，与状态机的归一化一致。
    # 预览器只渲染实际出现过的 θ，所以按最接近的取帧。
    avail = sorted(int(f[6:9]) for f in os.listdir(frames_dir)
                   if f.startswith("theta_") and f.endswith(".png"))

    def theta_file(angle):
        t = 45.0 * (100.0 - min(angle, 100.0)) / 100.0
        nearest = min(avail, key=lambda v: abs(v - t))
        return os.path.join(frames_dir, f"theta_{nearest:03d}.png")

    cache = {}
    out = []
    for i, lid in enumerate(lids):
        path = theta_file(lid)
        if path not in cache:
            cache[path] = Image.open(path).convert("RGB")
        out.append(compose(cache[path], lid))
        if i % 20 == 0:
            print(f"  合成 {i}/{len(lids)}  开合角 {lid:5.1f}°  朝向 {screen_facing(lid):+.2f}")

    # 以 .webp 结尾就直接编码动图，否则当成目录写出逐帧 PNG
    # （逐帧 PNG 交给 ffmpeg 编码，这样重复帧的停顿时长不会被合并掉）
    if out_path.endswith(".webp"):
        out[0].save(out_path, save_all=True, append_images=out[1:],
                    duration=50, loop=0, quality=88, method=6)
        print(f"  {out_path}  {len(out)} 帧  {os.path.getsize(out_path) // 1024} KB")
    else:
        os.makedirs(out_path, exist_ok=True)
        for i, im in enumerate(out):
            im.save(os.path.join(out_path, f"f{i:03d}.png"))
        print(f"  {out_path}/  写出 {len(out)} 帧 PNG")


if __name__ == "__main__":
    main()
