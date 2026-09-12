/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    工厂宏把 metadata.json 编进 .so。末尾的 #include "main.moc" 是必需的：
    上游 KWin 的 KWinConfig.cmake 因为变量名拼写不一致（KWINEFFECTS vs KWIN）
    没有注入 AUTOMOC 的宏名，这一行让 AUTOMOC 无条件处理本文件。
    详见 CMakeLists.txt 顶部关于 AUTOMOC 宏名的说明。
*/

#include "hinge_glass.h"

namespace KWin
{

KWIN_EFFECT_FACTORY_SUPPORTED_ENABLED(HingeGlassEffect,
                                      "metadata.json",
                                      return HingeGlassEffect::supported();
                                      ,
                                      return false;)

} // namespace KWin

#include "main.moc"
