/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "effect/effect.h"
#include "hingesensor.h"
#include "hingestate.h"

#include <map>
#include <memory>

namespace KWin
{

class GLFramebuffer;
class GLShader;
class GLTexture;
class LogicalOutput;

}

namespace HingeGlass
{
class LidSensor;
}

namespace KWin
{

/**
 * 铰链玻璃：转轴开合时把桌面折成磨砂玻璃。
 *
 * 参考实现是 macOS 上的 MacBook Duo / HingeGlass（Swift + Metal），
 * 本类是它的 KWin 移植。灵感来自 Apple iPhone Duo 开合时的玻璃动画。
 *
 * 渲染方式：把整个场景先渲染进自建离屏 FBO，再用玻璃着色器画回屏幕。
 * 这是 KWin 内置特效 screentransform 与 zoom 用的同一套路。
 * 相比"截图 + 叠加"，它内容实时、
 * 无额外场景渲染、无撕裂，也不需要任何 portal 授权。
 */
class HingeGlassEffect : public Effect
{
    Q_OBJECT

public:
    HingeGlassEffect();
    ~HingeGlassEffect() override;

    static bool supported();

    void reconfigure(ReconfigureFlags flags) override;
    void prePaintScreen(ScreenPrePaintData &data) override;
    void paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport,
                     int mask, const Region &deviceRegion, LogicalOutput *screen) override;
    bool isActive() const override;
    int requestedEffectChainPosition() const override;

private:
    struct OutputState
    {
        std::unique_ptr<GLTexture> texture;
        std::unique_ptr<GLFramebuffer> framebuffer;
    };

    /// 确保该输出有尺寸/格式正确的离屏纹理。失败返回 nullptr。
    OutputState *ensureOutput(LogicalOutput *screen, const RenderTarget &renderTarget);
    void drawGlass(const RenderViewport &viewport, OutputState *state);

    /// 当前应当使用的折叠角（调试时由 ForceAngleDeg 覆盖）
    double effectiveTheta() const;
    void destroyResources();

    void handleAngle(int deg);
    void handleNoData();
    void applyPollRate();

    /// 只请求重绘内置屏 —— 特效不影响外接屏
    void repaintInternalOutputs();

    HingeGlass::HingeSensor m_sensor;
    std::unique_ptr<HingeGlass::LidSensor> m_lidSensor;
    std::unique_ptr<HingeGlass::HingeState> m_state;
    std::unique_ptr<GLShader> m_shader;

    int m_mvpLocation = -1;
    int m_sizeLocation = -1;
    int m_angleLocation = -1;
    int m_frostLocation = -1;
    int m_eyeLocation = -1;
    int m_tapsLocation = -1;
    int m_hingePosLocation = -1;
    int m_samplerLocation = -1;

    // 用 std::map 而非 QHash：值里含 unique_ptr，是 move-only 类型
    // （KWin 内置的 zoom 特效出于同样原因也用 std::map，见 zoom.h:120）
    std::map<LogicalOutput *, OutputState> m_outputs;

    bool m_enabled = true;
    float m_frost = 0.09f;
    float m_eye = 2.4f;
    int m_taps = 1;
    float m_hingePos = 0.0f;
    double m_forceAngleDeg = -999.0;
    int m_idlePollHz = 10;
    int m_activePollHz = 30;
    int m_currentPollHz = 0;
};

} // namespace KWin
