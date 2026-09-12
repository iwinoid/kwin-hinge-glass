/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    离线预览器：用与 KWin 完全相同的着色器约定（#version 140 + base.vert 的
    texcoord0/modelViewProjectionMatrix）在离屏 FBO 里渲染玻璃折叠效果，输出 PNG。

    它的用途：
      1. 验证着色器能编译
      2. 验证 Y 翻转方向 —— 渲染 θ=0 时必须是恒等变换
      3. 验证 mipmap + textureLod 这条在 KWin 内零先例的路径
      4. 标定 Frost / Eye，并对比不同 Taps 的观感差异
*/

#include "glassshader.h"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLBuffer>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QPainter>
#include <QDebug>

#include <cmath>
#include <cstdio>

// 与 KWin 的 base.vert 逐行一致
static const char *kVertexSource = R"GLSL(#version 140
in vec4 position;
in vec4 texcoord;
out vec2 texcoord0;
uniform mat4 modelViewProjectionMatrix;
void main()
{
    texcoord0 = texcoord.st;
    gl_Position = modelViewProjectionMatrix * position;
}
)GLSL";

/// 造一张方向性极强的测试图，任何翻转/偏移都一眼可见
static QImage makeTestImage(const QSize &size)
{
    QImage img(size, QImage::Format_RGBA8888);
    img.fill(QColor(40, 40, 48));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing, false);

    // 上下渐变：顶部红、底部蓝 —— 用来判断 Y 方向
    QLinearGradient grad(0, 0, 0, size.height());
    grad.setColorAt(0.0, QColor(200, 30, 30));
    grad.setColorAt(1.0, QColor(30, 30, 200));
    p.fillRect(img.rect(), grad);

    // 四角标记：左上红 / 右上绿 / 左下蓝 / 右下黄（在渐变之上，色相区分明显）
    const int m = qMin(size.width(), size.height()) / 8;
    p.fillRect(0, 0, m, m, QColor(255, 0, 0));                              // 左上 = 红
    p.fillRect(size.width() - m, 0, m, m, QColor(0, 255, 0));               // 右上 = 绿
    p.fillRect(0, size.height() - m, m, m, QColor(0, 128, 255));            // 左下 = 蓝
    p.fillRect(size.width() - m, size.height() - m, m, m, QColor(255, 255, 0)); // 右下 = 黄

    // 白色网格，用来观察透视压缩与模糊
    p.setPen(QPen(QColor(255, 255, 255, 200), 3));
    for (int x = 0; x < size.width(); x += 100) {
        p.drawLine(x, 0, x, size.height());
    }
    for (int y = 0; y < size.height(); y += 100) {
        p.drawLine(0, y, size.width(), y);
    }

    // 顶部一行刻度，越靠左越密 —— 观察水平方向的各向异性
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 40; ++i) {
        const int w = qMax(2, 40 - i);
        p.fillRect(20 + i * 45, m + 20, w, 40, QColor(255, 255, 255, 220));
    }
    p.end();
    return img;
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("hinge-glass-preview"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("铰链玻璃着色器离线预览器"));
    parser.addHelpOption();

    QCommandLineOption anglesOpt(QStringLiteral("angles"),
                                 QStringLiteral("要渲染的角度，逗号分隔"),
                                 QStringLiteral("list"), QStringLiteral("0,5,10,20,30,45,80"));
    QCommandLineOption outOpt(QStringLiteral("out"),
                              QStringLiteral("输出路径前缀，会追加 _<角度>.png"),
                              QStringLiteral("prefix"), QStringLiteral("preview"));
    QCommandLineOption frostOpt(QStringLiteral("frost"), QStringLiteral("霜化强度"),
                                QStringLiteral("v"), QStringLiteral("0.09"));
    QCommandLineOption eyeOpt(QStringLiteral("eye"), QStringLiteral("眼距"),
                              QStringLiteral("v"), QStringLiteral("2.4"));
    QCommandLineOption tapsOpt(QStringLiteral("taps"), QStringLiteral("霜化采样数 1..24"),
                               QStringLiteral("n"), QStringLiteral("24"));
    QCommandLineOption hingeOpt(QStringLiteral("hinge-pos"),
                                QStringLiteral("转轴位置：0=底边，0.5=正中，负=屏幕下方"),
                                QStringLiteral("v"), QStringLiteral("0.0"));
    QCommandLineOption sizeOpt(QStringLiteral("size"), QStringLiteral("输出尺寸 WxH"),
                               QStringLiteral("WxH"), QStringLiteral("1280x800"));
    QCommandLineOption inputOpt(QStringLiteral("input"),
                                QStringLiteral("输入图像（默认用内置测试图案）"),
                                QStringLiteral("文件"));
    QCommandLineOption flipOpt(QStringLiteral("flip-input"),
                               QStringLiteral("反向验证：按 Metal 约定上传（不镜像），而不是 KWin 约定"));
    parser.addOption(anglesOpt);
    parser.addOption(outOpt);
    parser.addOption(frostOpt);
    parser.addOption(eyeOpt);
    parser.addOption(tapsOpt);
    parser.addOption(hingeOpt);
    parser.addOption(sizeOpt);
    parser.addOption(inputOpt);
    parser.addOption(flipOpt);
    parser.process(app);

    const QStringList sizeParts = parser.value(sizeOpt).split(QLatin1Char('x'));
    if (sizeParts.size() != 2) {
        qCritical() << "--size 格式应为 WxH";
        return 1;
    }
    const QSize size(sizeParts[0].toInt(), sizeParts[1].toInt());
    if (!size.isValid()) {
        qCritical() << "--size 无效";
        return 1;
    }

    QOffscreenSurface surface;
    surface.setFormat(QSurfaceFormat::defaultFormat());
    surface.create();
    if (!surface.isValid()) {
        qCritical() << "无法创建 offscreen surface";
        return 1;
    }

    QOpenGLContext ctx;
    ctx.setFormat(surface.format());
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        qCritical() << "无法创建/激活 OpenGL 上下文";
        return 1;
    }

    QOpenGLFunctions *f = ctx.functions();
    if (!f) {
        qCritical() << "拿不到 OpenGL 函数";
        return 1;
    }
    f->initializeOpenGLFunctions();
    std::fprintf(stderr, "[gl] GL_VERSION   = %s\n", (const char *)f->glGetString(GL_VERSION));
    std::fprintf(stderr, "[gl] GLSL_VERSION = %s\n", (const char *)f->glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::fprintf(stderr, "[gl] VENDOR       = %s\n", (const char *)f->glGetString(GL_VENDOR));
    std::fprintf(stderr, "[gl] RENDERER     = %s\n", (const char *)f->glGetString(GL_RENDERER));
    std::fprintf(stderr, "[gl] isOpenGLES   = %d\n", ctx.isOpenGLES());
    std::fprintf(stderr, "[gl] format       = %d.%d %s\n", ctx.format().majorVersion(), ctx.format().minorVersion(), ctx.format().profile() == QSurfaceFormat::CoreProfile ? "core" : "compat");
    qInfo().noquote() << "GL:" << reinterpret_cast<const char *>(f->glGetString(GL_VERSION))
                      << "|" << reinterpret_cast<const char *>(f->glGetString(GL_RENDERER));

    // ---- 输入纹理：模拟 KWin 渲染进 FBO 的结果（v=1 对应屏幕顶部）----
    // 关键：复刻 KWin 的纹理方向。
    // KWin 渲染进 FBO 得到的纹理是 OpenGL 约定 —— v = 1 在顶部、v = 0 在底部
    // （gltexture.cpp:302 "our Y axis is flipped vs OpenGL"）。
    // 而 QImage 的 row 0 是顶部，原样上传时 row 0 会落在 v = 0，与 KWin 相反。
    // 所以必须镜像，否则预览器验证的是 Metal 的约定，会漏掉 KWin 里的上下颠倒。
    // 有 --input 就用它按比例铺满（居中裁剪），否则用内置测试图案
    QImage testImage;
    const QString inputPath = parser.value(inputOpt);
    if (!inputPath.isEmpty()) {
        QImage src;
        if (src.load(inputPath)) {
            testImage = src.convertToFormat(QImage::Format_RGBA8888);
            if (testImage.size() != size) {
                testImage = testImage.scaled(size, Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
                testImage = testImage.copy((testImage.width() - size.width()) / 2,
                                           (testImage.height() - size.height()) / 2,
                                           size.width(), size.height());
            }
            std::fprintf(stderr, "[input] %s -> %dx%d\n", qPrintable(inputPath),
                         testImage.width(), testImage.height());
        } else {
            std::fprintf(stderr, "[input] 读取失败，改用内置测试图案: %s\n", qPrintable(inputPath));
        }
    }
    if (testImage.isNull()) {
        testImage = makeTestImage(size);
    }
    testImage = testImage.flipped(Qt::Vertical);
    if (parser.isSet(flipOpt)) {
        testImage = testImage.flipped(Qt::Vertical); // 反向验证用
    }

    QOpenGLTexture tex(testImage, QOpenGLTexture::GenerateMipMaps);
    if (!tex.isCreated()) {
        qCritical() << "输入纹理创建失败";
        return 1;
    }
    tex.setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    tex.setMagnificationFilter(QOpenGLTexture::Linear);
    tex.setWrapMode(QOpenGLTexture::ClampToEdge);
    std::fprintf(stderr, "[mip] 输入纹理 mip 级数 = %d (1 表示没有 mip 链)\n", tex.mipLevels());

    // ---- 着色器 ----
    QOpenGLShaderProgram prog;
    prog.bindAttributeLocation("position", 0);
    prog.bindAttributeLocation("texcoord", 1);
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexSource)) {
        std::fprintf(stderr, "[vertex compile failed]\n%s\n", qPrintable(prog.log()));
        return 1;
    }
    // 关键：kGlassFragmentSource 按 KWin 约定刻意不含 #version（由 KWin 注入）。
    // 这里必须模拟同样的注入，否则 Qt 会按 GLSL 1.10 编译 —— 那个版本既没有
    // in/out 也没有 textureLod，会得到完全误导性的编译错误。
    const QByteArray fragmentSource = QByteArray("#version 140\n") + HingeGlass::kGlassFragmentSource;
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource)) {
        std::fprintf(stderr, "[fragment compile failed]\n%s\n", qPrintable(prog.log()));
        return 1;
    }
    if (!prog.link()) {
        std::fprintf(stderr, "[link failed]\n%s\n", qPrintable(prog.log()));
        return 1;
    }
    std::fprintf(stderr, "[shader] 编译链接成功\n");

    // ---- 全屏四边形。v=1 在屏幕顶部，与 KWin 的约定一致 ----
    const float verts[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    QOpenGLVertexArrayObject vao;
    vao.create();
    QOpenGLVertexArrayObject::Binder vaoBinder(&vao);
    QOpenGLBuffer vbo(QOpenGLBuffer::VertexBuffer);
    vbo.create();
    vbo.bind();
    vbo.allocate(verts, sizeof(verts));
    prog.enableAttributeArray(0);
    prog.setAttributeBuffer(0, GL_FLOAT, 0, 2, 4 * sizeof(float));
    prog.enableAttributeArray(1);
    prog.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));

    QOpenGLFramebufferObjectFormat fboFormat;
    fboFormat.setAttachment(QOpenGLFramebufferObject::NoAttachment);
    QOpenGLFramebufferObject fbo(size, fboFormat);
    if (!fbo.isValid()) {
        qCritical() << "FBO 无效";
        return 1;
    }

    const QMatrix4x4 identity;
    const float frost = parser.value(frostOpt).toFloat();
    const float eye = parser.value(eyeOpt).toFloat();
    const int taps = qBound(1, parser.value(tapsOpt).toInt(), HingeGlass::kMaxTaps);
    const QString prefix = parser.value(outOpt);

    int failures = 0;
    const QStringList angleStrings = parser.value(anglesOpt).split(QLatin1Char(','));
    for (const QString &angleString : angleStrings) {
        const float angle = angleString.trimmed().toFloat();

        fbo.bind();
        f->glViewport(0, 0, size.width(), size.height());
        f->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        f->glClear(GL_COLOR_BUFFER_BIT);

        prog.bind();
        tex.bind(0);
        prog.setUniformValue("sampler", 0);
        prog.setUniformValue("uSize", QVector2D(size.width(), size.height()));
        prog.setUniformValue("uAngle", angle);
        prog.setUniformValue("uFrost", frost);
        prog.setUniformValue("uEye", eye);
        prog.setUniformValue("uTaps", taps);
        prog.setUniformValue("uHingePos", parser.value(hingeOpt).toFloat());
        prog.setUniformValue("modelViewProjectionMatrix", identity);
        f->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        prog.release();
        fbo.release();

        const QString path = QStringLiteral("%1_%2.png")
                                 .arg(prefix, QString::number(int(angle)).rightJustified(3, QLatin1Char('0')));
        if (!fbo.toImage().save(path)) {
            std::fprintf(stderr, "保存失败: %s\n", qPrintable(path));
            ++failures;
        } else {
            std::fprintf(stderr, "已写出 %s  (theta=%g frost=%g eye=%g taps=%d)\n",
                         qPrintable(path), double(angle), double(frost), double(eye), taps);
        }
    }

    return failures == 0 ? 0 : 1;
}
