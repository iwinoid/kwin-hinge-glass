#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 iwinoid
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 重新编译并安装铰链玻璃特效。
#
# 为什么需要这个脚本：KWin 的插件工厂 IID 内嵌 KWin 版本号
# （org.kde.kwin.EffectPluginFactory6.7.5，见 effect.h:1131），
# 所以**补丁版本变化也会导致加载失败**，而且是静默的
# （effectloader.cpp:305-313 只打一行 debug 日志就 return nullptr）。
#
# 用法：
#   ./rebuild.sh            编译并安装
#   ./rebuild.sh --build    只编译，不安装
#   ./rebuild.sh --uninstall 卸载

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_HEADER="/usr/include/kwin/config-kwin.h"
PLUGIN_DIR="/usr/lib/qt6/plugins/kwin/effects/plugins"
CONFIG_DIR="/usr/lib/qt6/plugins/kwin/effects/configs"

die() { echo "错误：$*" >&2; exit 1; }

[[ -f "$CONFIG_HEADER" ]] || die "读不到 $CONFIG_HEADER（kwin 的开发头文件没装？）"
KWIN_VERSION="$(sed -n 's/^#define KWIN_PLUGIN_VERSION_STRING "\(.*\)"/\1/p' "$CONFIG_HEADER")"
[[ -n "$KWIN_VERSION" ]] || die "无法从 $CONFIG_HEADER 解析 KWin 版本"

case "${1:-}" in
    --uninstall)
        sudo rm -f "$PLUGIN_DIR/hinge_glass.so" "$CONFIG_DIR/kwin_hinge_glass_config.so"
        kwriteconfig6 --file kwinrc --group Plugins --key hinge_glassEnabled --notify false || true
        echo "已卸载。注销重登后完全生效。"
        exit 0
        ;;
    --build)
        INSTALL=0
        ;;
    "")
        INSTALL=1
        ;;
    *)
        die "未知参数 ${1}（可用：--build / --uninstall）"
        ;;
esac

echo "为 KWin $KWIN_VERSION 构建 Hinge Glass…"

# 必须干净重建：改了 kcfg / 元数据之后增量构建可能留下陈旧产物
rm -rf "$ROOT/build"

cmake -S "$ROOT" -B "$ROOT/build" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr
cmake --build "$ROOT/build"

echo
(cd "$ROOT/build" && ctest --output-on-failure)

if (( INSTALL )); then
    sudo cmake --install "$ROOT/build"
    echo
    echo "已安装。"
    echo
    echo "注意：如果之前已经装过并加载过，KWin 内存里映射的仍是旧 inode ——"
    echo "「禁用再启用」不够（不会 dlclose，/proc/<kwin>/maps 里仍标 (deleted)），"
    echo "必须注销重登（或重启 KWin）才会加载新代码。"
    echo "首次安装则可以热加载：打开「系统设置 → 桌面特效」勾选即可。"
fi
