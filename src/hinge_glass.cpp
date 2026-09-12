/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "hinge_glass.h"

#include "glassshader.h"
#include "lidsensor.h"

#include "core/rendertarget.h"
#include "core/renderviewport.h"
#include "effect/effecthandler.h"
#include "opengl/glframebuffer.h"
#include "opengl/glshader.h"
#include "opengl/glshadermanager.h"
#include "opengl/gltexture.h"
#include "opengl/glutils.h"
#include "opengl/glvertexbuffer.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDateTime>
#include <QDebug>

#include <cmath>

namespace KWin
{

// 自己的日志分类（screentransform.cpp:23 同款做法）。
// 插件的元数据在「桌面特效」页里被无条件显示为"支持"，
// 所以加载失败只能靠日志定位 —— 用
//   journalctl --user -b | grep kwin_effect_hinge_glass
Q_LOGGING_CATEGORY(KWIN_HINGEGLASS, "kwin_effect_hinge_glass", QtWarningMsg)

namespace
{
std::int64_t nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}
} // namespace

HingeGlassEffect::HingeGlassEffect()
    : Effect()
{
    m_shader = ShaderManager::instance()->generateCustomShader(
        ShaderTrait::MapTexture,
        QByteArray(), // 留空 -> 复用 KWin 的 base.vert，白拿 texcoord0 与 MVP
        QByteArray(HingeGlass::kGlassFragmentSource));
    if (!m_shader) {
        qCCritical(KWIN_HINGEGLASS) << "HingeGlass: 玻璃着色器编译失败，特效不可用";
        return;
    }

    m_mvpLocation = m_shader->uniformLocation("modelViewProjectionMatrix");
    m_sizeLocation = m_shader->uniformLocation("uSize");
    m_angleLocation = m_shader->uniformLocation("uAngle");
    m_frostLocation = m_shader->uniformLocation("uFrost");
    m_eyeLocation = m_shader->uniformLocation("uEye");
    m_tapsLocation = m_shader->uniformLocation("uTaps");
    m_hingePosLocation = m_shader->uniformLocation("uHingePos");
    m_samplerLocation = m_shader->uniformLocation("sampler");

    reconfigure(ReconfigureAll);

    m_lidSensor = std::make_unique<HingeGlass::LidSensor>();
    connect(m_lidSensor.get(), &HingeGlass::LidSensor::lidClosed, this, [this]() {
        if (m_state && m_state->onLidClosed()) {
            // 合盖：立刻丢弃 GL 资源。此时输出正在关闭，KWin 基本不再重绘，
            // 主动请求一次重绘把最后那一帧恢复正常画面；
            // 销毁资源则避免在挂起期间白占显存。
            destroyResources();
            effects->addRepaintFull();
        }
        applyPollRate();
    });
    connect(m_lidSensor.get(), &HingeGlass::LidSensor::lidOpened, this, [this]() {
        if (m_state) {
            m_state->onLidOpened(nowMs());
        }
        applyPollRate();
    });

    connect(&m_sensor, &HingeGlass::HingeSensor::angleChanged, this, &HingeGlassEffect::handleAngle);
    connect(&m_sensor, &HingeGlass::HingeSensor::unavailable, this, &HingeGlassEffect::handleNoData);

    if (m_sensor.start(m_idlePollHz)) {
        m_currentPollHz = m_idlePollHz;
    }
}

HingeGlassEffect::~HingeGlassEffect()
{
    m_sensor.stop();
    destroyResources();
}

bool HingeGlassEffect::supported()
{
    if (!effects->isOpenGLCompositing()) {
        qWarning() << "HingeGlass: 只支持 OpenGL 合成，当前后端不是，特效不会被加载";
        return false;
    }
    if (HingeGlass::HingeSensor::findDevicePath().isEmpty()) {
        // 「桌面特效」页对插件特效无条件显示为"支持"（effectsmodel.cpp:340 硬编码），
        // 所以勾了没反应时只能查日志。这条日志就是给那种情况用的。
        qWarning() << "HingeGlass: 本机没有 cros-ec-lid-angle 传感器，特效不会被加载";
        return false;
    }
    return true;
}

void HingeGlassEffect::reconfigure(ReconfigureFlags flags)
{
    Q_UNUSED(flags)

    const KConfigGroup cg = effects->config()->group(QStringLiteral("Effect-hinge_glass"));

    m_enabled = cg.readEntry(QStringLiteral("Enabled"), true);
    m_frost = float(cg.readEntry(QStringLiteral("Frost"), 0.09));
    m_eye = float(cg.readEntry(QStringLiteral("Eye"), 2.4));
    m_taps = qBound(1, cg.readEntry(QStringLiteral("Taps"), HingeGlass::kMaxTaps), HingeGlass::kMaxTaps);
    m_hingePos = float(cg.readEntry(QStringLiteral("HingePosition"), 0.0));
    m_forceAngleDeg = cg.readEntry(QStringLiteral("ForceAngleDeg"), -999.0);
    m_idlePollHz = qMax(1, cg.readEntry(QStringLiteral("IdlePollHz"), 20));
    m_activePollHz = qMax(1, cg.readEntry(QStringLiteral("ActivePollHz"), 30));

    HingeGlass::Config cfg;
    cfg.originalAngle = cg.readEntry(QStringLiteral("OriginalAngle"), 100.0);
    cfg.maxAngle = cg.readEntry(QStringLiteral("MaxAngle"), 45.0);
    cfg.stillToleranceDeg = cg.readEntry(QStringLiteral("StillToleranceDeg"), 10.0);
    cfg.persistWhileFolded = cg.readEntry(QStringLiteral("PersistWhileFolded"), false);
    cfg.dwellMs = cg.readEntry(QStringLiteral("DwellMs"), 300);
    cfg.segments = HingeGlass::parseDwellSegments(
        cg.readEntry(QStringLiteral("DwellSegments"), QStringLiteral("80:2000,100:300")).toStdString());
    cfg.fadeMs = cg.readEntry(QStringLiteral("FadeMs"), 180);
    cfg.minEffectMs = cg.readEntry(QStringLiteral("MinEffectMs"), 350);
    cfg.springFreq = cg.readEntry(QStringLiteral("SpringFreq"), 30.0);

    if (m_state) {
        *m_state = HingeGlass::HingeState(cfg);
    } else {
        m_state = std::make_unique<HingeGlass::HingeState>(cfg);
    }

    applyPollRate();

    // reconfigure 之后主动请求一次重绘：KWin 的 reconfigureEffect 只重读配置，
    // 不会自己安排重绘，否则改完参数要等下一次自然重绘（如时钟跳动）才可见。
    effects->addRepaintFull();
}

void HingeGlassEffect::applyPollRate()
{
    if (!m_sensor.isAvailable() || !m_state) {
        return;
    }
    // 空闲时降频省电，活动时提速。采纳自 Mac-Duo（其空闲 8Hz / 活动 30Hz）。
    const int wanted = m_state->active() ? m_activePollHz : m_idlePollHz;
    if (wanted == m_currentPollHz) {
        return;
    }
    m_currentPollHz = wanted;
    m_sensor.setPollHz(wanted);
}

void HingeGlassEffect::handleAngle(int deg)
{
    if (!m_state || !m_enabled || effects->isScreenLocked()) {
        return;
    }
    const bool wasActive = m_state->active();
    if (m_state->onAngle(deg, nowMs())) {
        effects->addRepaintFull();
    }
    if (m_state->active() != wasActive) {
        applyPollRate();
    }
}

void HingeGlassEffect::handleNoData()
{
    if (!m_state || !m_enabled || effects->isScreenLocked()) {
        return;
    }
    // 不是"没有变化"，而是"这一拍没有可信读数"。
    // 状态机把它当无变化处理，于是「折到 360° 时 EC 失效」（读数变 500）
    // 这条路径自然走停留计时，无需任何特判。
    const bool wasActive = m_state->active();
    if (m_state->onNoData(nowMs())) {
        effects->addRepaintFull();
    }
    if (m_state->active() != wasActive) {
        applyPollRate();
    }
}

double HingeGlassEffect::effectiveTheta() const
{
    // 调试用：绕过状态机直接指定折叠角，便于在不折屏幕的情况下验证渲染与观感。
    // HingeGlass 与 Mac Duo 都有等价的预览模式。
    if (m_forceAngleDeg > -900.0) {
        return m_forceAngleDeg;
    }
    return m_state ? m_state->theta() : 0.0;
}

bool HingeGlassEffect::isActive() const
{
    if (!m_enabled || !m_shader || effects->isScreenLocked()) {
        return false;
    }
    if (m_forceAngleDeg > -900.0) {
        return true;
    }
    return m_state && m_state->active();
}

int HingeGlassEffect::requestedEffectChainPosition() const
{
    // 必须靠前：paintScreen 里重入 effects->paintScreen() 只会从**本特效之后**
    // 继续（effecthandler.cpp:352-360）。排在前面等于把整屏替换掉、更靠前的特效白画。
    // 取 0 让整条链都进到我们的离屏纹理里。
    return 0;
}

void HingeGlassEffect::prePaintScreen(ScreenPrePaintData &data)
{
    if (isActive()) {
        // 整屏内容都会被替换，必须强制全屏重绘
        data.mask |= PAINT_SCREEN_TRANSFORMED;
    }
    effects->prePaintScreen(data);
}

HingeGlassEffect::OutputState *HingeGlassEffect::ensureOutput(LogicalOutput *screen,
                                                             const RenderTarget &renderTarget)
{
    const QSize wanted = renderTarget.transformedSize();
    const GLenum format = renderTarget.texture() ? renderTarget.texture()->internalFormat() : GL_RGBA8;

    OutputState &state = m_outputs[screen];
    if (state.texture && state.texture->size() == wanted
        && state.texture->internalFormat() == format) {
        return &state;
    }

    // GLFramebuffer 只持有 GLTexture* 且不拥有它 —— 必须先销毁 FBO 再销毁纹理
    // （blur.cpp:609-610 同款顺序）
    state.framebuffer.reset();
    state.texture.reset();

    // levels > 1 才有 mip 链。GLTexture::allocate 的 levels 默认是 1，
    // 且 m_canUseMipmaps = (levels > 1)：不显式传的话 generateMipmaps() 是静默空操作，
    // bind() 还会把 mipmap filter 悄悄降级 —— 霜化的 LOD 采样会退化成硬边。
    const int levels = 1 + int(std::floor(std::log2(double(qMax(wanted.width(), wanted.height())))));
    state.texture = GLTexture::allocate(format, wanted, levels);
    if (!state.texture) {
        qCWarning(KWIN_HINGEGLASS) << "HingeGlass: 离屏纹理分配失败" << wanted;
        m_outputs.erase(screen);
        return nullptr;
    }
    state.texture->setFilter(GL_LINEAR_MIPMAP_LINEAR);
    // 默认是 GL_REPEAT，不设的话边缘双线性采样会环绕取样
    state.texture->setWrapMode(GL_CLAMP_TO_EDGE);

    state.framebuffer = std::make_unique<GLFramebuffer>(state.texture.get());
    if (!state.framebuffer->valid()) {
        // 构造函数失败不抛异常，只打日志并置 m_valid = false，必须自己查
        qCWarning(KWIN_HINGEGLASS) << "HingeGlass: 离屏 FBO 创建失败";
        state.framebuffer.reset();
        state.texture.reset();
        m_outputs.erase(screen);
        return nullptr;
    }
    return &state;
}

void HingeGlassEffect::paintScreen(const RenderTarget &renderTarget, const RenderViewport &viewport,
                                   int mask, const Region &deviceRegion, LogicalOutput *screen)
{
    if (!isActive()) {
        effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        return;
    }

    OutputState *state = ensureOutput(screen, renderTarget);
    if (!state) {
        effects->paintScreen(renderTarget, viewport, mask, deviceRegion, screen);
        return;
    }

    // 1) 把场景渲染进离屏纹理。
    //    RenderTarget 必须继承当前输出的 colorDescription，否则颜色会被二次转换
    //    （不匹配没有任何断言或警告，只会静默发灰/过曝）。
    //    RenderViewport 的 renderOffset 必须传 QPoint()，否则投影矩阵的 offset 缩放会切小画面。
    const RenderTarget fboTarget(state->framebuffer.get(), renderTarget.colorDescription());
    const RenderViewport fboViewport(viewport.renderRect(), viewport.scale(), fboTarget, QPoint());

    GLFramebuffer::pushFramebuffer(state->framebuffer.get());
    effects->paintScreen(fboTarget, fboViewport, mask, deviceRegion, screen);
    GLFramebuffer::popFramebuffer();

    // 2) 生成 mipmap。必须在渲染结束且 pop 之后；
    //    generateMipmaps() 内部不 bind，要自己先 bind（对比 render() 是有 bind 的）。
    state->texture->bind();
    state->texture->generateMipmaps();
    state->texture->unbind();

    // 3) 用玻璃着色器画回屏幕
    drawGlass(viewport, state);
}

void HingeGlassEffect::drawGlass(const RenderViewport &viewport, OutputState *state)
{
    const QSize size = state->texture->size();
    const float w = float(size.width());
    const float h = float(size.height());

    GLVertexBuffer *vbo = GLVertexBuffer::streamingBuffer();
    vbo->reset();
    vbo->setAttribLayout(std::span(GLVertexBuffer::GLVertex2DLayout), sizeof(GLVertex2D));

    const auto opt = vbo->map<GLVertex2D>(6);
    if (!opt) {
        return;
    }
    const auto map = *opt;

    // 与 KWin 约定一致：屏幕顶部对应 texcoord v = 1
    // （screentransform.cpp:157-182；gltexture.cpp:302 注释
    //   "our Y axis is flipped vs OpenGL"）。着色器里正是靠 1.0 - y 转成
    // Metal 原版的 uv 方向 —— 这一步已在离线预览器里眼见为实。
    map[0] = GLVertex2D{.position = QVector2D(0.0f, 0.0f), .texcoord = QVector2D(0.0f, 1.0f)};
    map[1] = GLVertex2D{.position = QVector2D(w, h), .texcoord = QVector2D(1.0f, 0.0f)};
    map[2] = GLVertex2D{.position = QVector2D(0.0f, h), .texcoord = QVector2D(0.0f, 0.0f)};
    map[3] = GLVertex2D{.position = QVector2D(0.0f, 0.0f), .texcoord = QVector2D(0.0f, 1.0f)};
    map[4] = GLVertex2D{.position = QVector2D(w, 0.0f), .texcoord = QVector2D(1.0f, 1.0f)};
    map[5] = GLVertex2D{.position = QVector2D(w, h), .texcoord = QVector2D(1.0f, 0.0f)};
    vbo->unmap();

    glActiveTexture(GL_TEXTURE0);
    state->texture->bind();

    ShaderManager::instance()->pushShader(m_shader.get());
    m_shader->setUniform(m_mvpLocation, viewport.projectionMatrix());
    m_shader->setUniform(m_sizeLocation, QVector2D(w, h));
    m_shader->setUniform(m_angleLocation, float(effectiveTheta()));
    m_shader->setUniform(m_frostLocation, m_frost);
    m_shader->setUniform(m_eyeLocation, m_eye);
    m_shader->setUniform(m_tapsLocation, m_taps);
    m_shader->setUniform(m_hingePosLocation, m_hingePos);
    m_shader->setUniform(m_samplerLocation, 0);

    vbo->bindArrays();
    vbo->draw(GL_TRIANGLES, 0, 6);
    vbo->unbindArrays();

    ShaderManager::instance()->popShader();
    state->texture->unbind();
}

void HingeGlassEffect::destroyResources()
{
    if (m_outputs.empty()) {
        return;
    }
    // 纹理与 FBO 都绑定创建时的 GL context，析构时会断言 context 兼容。
    // 合盖事件可能来自非渲染路径，所以显式确保 context 当前。
    effects->makeOpenGLContextCurrent();
    m_outputs.clear();
}

} // namespace KWin
