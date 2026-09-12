/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    玻璃折叠着色器 —— 移植自 macOS 参考实现 MacBook Duo / HingeGlass 的 Metal glassMain
    （源文件 Sources/GlassRenderer.swift 的 glassMain，第 127-172 行）。

    移植约定（全部对照 KWin 6.7.5 源码核实）：
      * 不写 #version —— KWin 自己注入 #version 140，写了会被丢弃（glshader.cpp:115-116）
      * 顶点源码留空 -> 复用 KWin 的 base.vert，免费获得 texcoord0 与
        modelViewProjectionMatrix
      * 片元源码非空时 KWin 什么都不自动声明，所以下面三行必须自己写
      * 必须用 in/out 语法（6.7 起不再自动改写 attribute/varying）

    与原版的差异（四处，均为有意为之）：
      1. 去掉了 imageAspect / aspect-fill —— 我们的纹理恒等于屏幕尺寸，fit 恒为 (1,1)
      2. 加了 uTaps（性能旋钮）。原版固定 24 抽
      3. 掠射反光用 s*s 代替原版的 pow(s, 2.0) —— GLSL 里 pow() 的底数为负时是
         未定义行为，而 s 确实会取负值。数学上等价，但消除了 UB
      4. 加了 uHingePos：折线位置可自定义。原版硬编码在屏幕底边
*/

#pragma once

namespace HingeGlass
{

inline constexpr int kMaxTaps = 24;

inline constexpr const char *kGlassFragmentSource = R"GLSL(
in vec2 texcoord0;
uniform sampler2D sampler;
uniform vec2  uSize;
uniform float uAngle;
uniform float uFrost;
uniform float uEye;
uniform float uHingePos;
uniform int   uTaps;
out vec4 fragColor;

const float kGoldenAngle = 2.39996323;
const int   kMaxTaps = 24;
const float kPi = 3.14159265358979323846;

vec3 sampleScene(vec2 point, float lod)
{
    vec2 uv = point / uSize;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0)))) {
        return vec3(0.015, 0.018, 0.025);
    }
    // point 处在 Metal 的坐标系里（y = 0 在屏幕顶部），而 KWin 渲染进 FBO 得到的
    // 纹理是 OpenGL 约定：v = 1 在顶部、v = 0 在底部
    // （gltexture.cpp:302 "our Y axis is flipped vs OpenGL"；
    //   blur.cpp:663 把屏幕 y0=0 映射到 v0=1）。所以取纹理时必须翻 V。
    //
    // 漏掉这一行的后果是整个画面上下颠倒 —— 而且离线预览器**发现不了**：
    // 它把 QImage 原样上传，v = 0 恰好等于图片顶部，正好等价于 Metal 的约定。
    return textureLod(sampler, vec2(uv.x, 1.0 - uv.y), lod).rgb;
}

void main()
{
    // KWin 的 texcoord0.y = 1 在屏幕顶部；Metal 原版的 uv.y = 0 在顶部。
    // 统一成 muv 之后，下面的数学与原版逐行一致。
    vec2 muv = vec2(texcoord0.x, 1.0 - texcoord0.y);

    float rad = uAngle * kPi / 180.0;

    // 折线的 y 坐标（0 = 屏幕顶部）。uHingePos 是折线到底边的距离 ÷ 屏高：
    //   0   -> 底边（原版行为）
    //   0.5 -> 屏幕正中
    //   <0  -> 屏幕下方（物理转轴在可见屏幕之外）
    float hingeY = uSize.y * (1.0 - uHingePos);
    float pixelY = muv.y * uSize.y;

    // 只有折线**上方**的部分参与折叠；下方保持恒等 ——
    // 这正是 iPhone Duo 的观感：一半显示正常内容，另一半才虚拟出水平面。
    float d = hingeY - pixelY;
    float dPos = max(d, 0.0);

    float gap = dPos * sin(rad);
    vec2 glass = vec2(muv.x * uSize.x, d > 0.0 ? (hingeY - dPos * cos(rad)) : pixelY);

    float eye = uSize.y * uEye;
    vec2  center = uSize * 0.5;
    vec2  hit = center + (glass - center) * eye / max(eye - gap, eye * 0.15);

    float radius = min(abs(gap) * uFrost, uSize.y * 0.055);
    // 0.22 是原版常数：24 抽的圆盘已经贡献了 ~radius 的模糊，mip 只需抹平抽样锯齿。
    // 曾经为了"让 taps=1 也能达到同样模糊量"把它改成随抽样数变化，那是错的：
    // 那等于用一个 mip 第 6 级的单次采样（≈64x64 盒式模糊）代替平滑圆盘，
    // 模糊量对了但整屏会糊成"低分辨率"的观感。
    float lod = max(0.0, log2(max(1.0, radius * 0.22)));

    int taps = clamp(uTaps, 1, kMaxTaps);
    vec3 color = vec3(0.0);
    for (int i = 0; i < kMaxTaps; ++i) {
        if (i >= taps) {
            break;
        }
        // taps == 1 时取圆心，避免落在 0.707*radius 处造成整体偏移
        float r = taps > 1 ? sqrt((float(i) + 0.5) / float(taps)) * radius : 0.0;
        float a = float(i) * kGoldenAngle;
        color += sampleScene(hit + vec2(cos(a), sin(a)) * r, lod);
    }
    color /= float(taps);

    float depth = abs(gap) / uSize.y;
    color *= 1.0 - min(depth * 0.45, 0.35);

    // 掠射反光按折叠区高度等比缩放；uHingePos = 0 时退化为原版的常数
    float foldTop = clamp(1.0 - uHingePos, 0.0, 1.0);
    float sheenCenter = foldTop * (0.25 + sin(rad) * 0.7);
    float s = (muv.y - sheenCenter) / max(0.19 * foldTop, 1e-3);
    float sheen = exp(-s * s);
    color += vec3(0.78, 0.87, 1.0) * sheen * abs(sin(rad)) * 0.055;

    fragColor = vec4(color, 1.0);
}
)GLSL";

} // namespace HingeGlass
