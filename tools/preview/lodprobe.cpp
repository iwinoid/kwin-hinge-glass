/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    探针：在 #version 140 的片元着色器里，哪些显式 LOD / 梯度采样函数可用？

    这个问题的答案决定玻璃折叠特效的架构：
      * 若 textureLod / textureGrad 可用 -> 可以照搬原版「单次带 LOD 的采样」
      * 若都不可用 -> 必须改成多次采样，或预先生成多级模糊纹理再多趟混合

    背景：KWin 的 ShaderManager 对桌面 GL 注入的就是 #version 140
    （glshader.cpp:83-95），所以这个版本的可用性就是真实约束。
    实测 KWin 全树与 better-blur-dx 都不使用显式 LOD。
*/

#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>

#include <cstdio>

namespace
{

struct Variant {
    const char *name;
    const char *source;
};

// 各变体只替换采样那一行，其余保持最小
const char *kPreamble = R"GLSL(in vec2 uv;
uniform sampler2D texUnit;
uniform float lod;
out vec4 fragColor;
void main() {
)GLSL";

const char *kPostamble = R"GLSL(    fragColor = vec4(c, 1.0);
}
)GLSL";

int g_failures = 0;

void probe(const char *version, const Variant &v)
{
    QOpenGLShaderProgram prog;
    const QByteArray vs = QByteArray(version) + R"GLSL(
in vec4 position;
void main() { gl_Position = position; }
)GLSL";
    QByteArray fs = QByteArray(version) + "\n" + kPreamble + v.source + kPostamble;

    const bool vok = prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vs);
    const bool fok = vok && prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fs);
    const bool lok = fok && prog.link();

    if (vok && fok && lok) {
        std::printf("  \033[32m可用\033[0m   %-22s @ %s\n", v.name, version);
    } else {
        std::printf("  \033[31m不可用\033[0m %-22s @ %s\n", v.name, version);
        // 只打印第一条错误行，避免刷屏
        const QString log = prog.log();
        const QStringList lines = log.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            if (line.contains(QLatin1String("error"))) {
                std::printf("           %s\n", qPrintable(line.trimmed()));
                break;
            }
        }
        ++g_failures;
    }
    prog.removeAllShaders();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setRenderableType(QSurfaceFormat::OpenGL); // 与 KWin 一致（supportInformation 显示 4.6 Core）
    QSurfaceFormat::setDefaultFormat(fmt);

    QOffscreenSurface surface;
    surface.setFormat(fmt);
    surface.create();
    if (!surface.isValid()) {
        std::fprintf(stderr, "offscreen surface 创建失败\n");
        return 1;
    }

    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create() || !ctx.makeCurrent(&surface)) {
        std::fprintf(stderr, "OpenGL 上下文创建失败\n");
        return 1;
    }

    QOpenGLFunctions *f = ctx.functions();
    f->initializeOpenGLFunctions();

    std::fprintf(stderr, "GL_VERSION   = %s\n", (const char *)f->glGetString(GL_VERSION));
    std::fprintf(stderr, "GLSL_VERSION = %s\n", (const char *)f->glGetString(GL_SHADING_LANGUAGE_VERSION));
    std::fprintf(stderr, "profile      = %s\n\n",
                 ctx.format().profile() == QSurfaceFormat::CoreProfile ? "core" : "compat");

    const Variant variants[] = {
        {"texture(tex,uv)", "    vec3 c = texture(texUnit, uv).rgb;\n"},
        {"texture(tex,uv,bias)", "    vec3 c = texture(texUnit, uv, lod).rgb;\n"},
        {"textureLod(tex,uv,lod)", "    vec3 c = textureLod(texUnit, uv, lod).rgb;\n"},
        {"texture2DLod(tex,uv,lod)", "    vec3 c = texture2DLod(texUnit, uv, lod).rgb;\n"},
        {"textureGrad(tex,uv,dx,dy)",
         "    vec3 c = textureGrad(texUnit, uv, vec2(0.01), vec2(0.01)).rgb;\n"},
        {"texture2DGradARB(...)",
         "    vec3 c = texture2DGradARB(texUnit, uv, vec2(0.01), vec2(0.01)).rgb;\n"},
        {"texelFetch(tex,ivec2,0)", "    vec3 c = texelFetch(texUnit, ivec2(0), 0).rgb;\n"},
        {"textureSize(tex,0)", "    vec3 c = vec3(textureSize(texUnit, 0));\n"},
    };

    for (const char *version : {"#version 140\n", "#version 330 core\n", "#version 400 core\n"}) {
        std::printf("%s\n", version);
        for (const Variant &v : variants) {
            probe(version, v);
        }
        std::printf("\n");
    }

    // 顺带验证：用 GL_ARB_shader_texture_lod 扩展能否在 140 下打开 texture2DLod
    std::printf("#version 140 + GL_ARB_shader_texture_lod\n");
    {
        QOpenGLShaderProgram prog;
        const QByteArray vs = QByteArray("#version 140\nin vec4 position;\nvoid main(){gl_Position=position;}\n");
        QByteArray fs = QByteArray("#version 140\n#extension GL_ARB_shader_texture_lod : enable\n")
            + kPreamble + "    vec3 c = texture2DLod(texUnit, uv, lod).rgb;\n" + kPostamble;
        const bool ok = prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)
            && prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fs) && prog.link();
        std::printf("  %s   texture2DLod 带扩展\n", ok ? "\033[32m可用\033[0m" : "\033[31m不可用\033[0m");
        if (!ok) {
            ++g_failures;
            const QStringList lines = prog.log().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &line : lines) {
                if (line.contains(QLatin1String("error"))) {
                    std::printf("           %s\n", qPrintable(line.trimmed()));
                    break;
                }
            }
        }
    }

    return 0;
}
