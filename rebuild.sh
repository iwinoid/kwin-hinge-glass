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

# KWin 的头文件路径各发行版一致（上游就装在 include/kwin/ 下）
CONFIG_HEADER="/usr/include/kwin/config-kwin.h"

# Qt 插件目录**各发行版不同**，不能写死：
#   Arch    /usr/lib/qt6/plugins
#   Fedora  /usr/lib64/qt6/plugins
#   Debian  /usr/lib/x86_64-linux-gnu/qt6/plugins
# 所以问 Qt 自己。qmake6 属于 qt6-base，各发行版都有。
qt_plugin_dir() {
    local tool out
    for tool in qmake6 qtpaths6; do
        command -v "$tool" >/dev/null 2>&1 || continue
        if [ "$tool" = qmake6 ]; then
            out="$("$tool" -query QT_INSTALL_PLUGINS 2>/dev/null)"
        else
            out="$("$tool" --query QT_INSTALL_PLUGINS 2>/dev/null)"
        fi
        if [ -n "$out" ] && [ -d "$out" ]; then
            printf '%s\n' "$out"
            return 0
        fi
    done
    return 1
}

die() { echo "错误：$*" >&2; exit 1; }

[[ -f "$CONFIG_HEADER" ]] || die "读不到 $CONFIG_HEADER（kwin 的开发头文件没装？）"
KWIN_VERSION="$(sed -n 's/^#define KWIN_PLUGIN_VERSION_STRING "\(.*\)"/\1/p' "$CONFIG_HEADER")"
[[ -n "$KWIN_VERSION" ]] || die "无法从 $CONFIG_HEADER 解析 KWin 版本"

case "${1:-}" in
    --uninstall)
        # 优先按**安装清单**删除 —— cmake --install 会写下确切路径，比猜目录可靠。
        # 曾经写死过 Arch 的路径：在 Fedora 上 rm -f 一个不存在的路径会静默成功，
        # 于是报"已卸载"却什么都没删。
        MANIFEST="$ROOT/build/install_manifest.txt"
        removed=0
        if [ -f "$MANIFEST" ]; then
            # `|| [ -n "$f" ]` 是必需的：CMake 写的 install_manifest.txt
            # **最后一行没有换行符**，纯 `while read` 会把它整个丢掉 ——
            # 表现是少删一个文件，而且没有任何报错。
            while IFS= read -r f || [ -n "$f" ]; do
                case "$f" in
                    *hinge_glass*|*kwin_hinge_glass*) ;;
                    *) continue ;;
                esac
                if [ -e "$f" ]; then
                    # sudo 会从 stdin 读密码，把 while read 的输入吞掉 ——
                    # 不重定向的话循环只跑第一轮就结束。踩过。
                    sudo rm -f "$f" < /dev/null
                    echo "  已删除 $f"
                    removed=$((removed + 1))
                fi
            done < "$MANIFEST"
        fi

        if (( removed == 0 )); then
            # 没有清单（换了机器、清过 build）就退回问 Qt
            if PLUGINS="$(qt_plugin_dir)"; then
                for f in "$PLUGINS/kwin/effects/plugins/hinge_glass.so" \
                         "$PLUGINS/kwin/effects/configs/kwin_hinge_glass_config.so"; do
                    [ -e "$f" ] || continue
                    sudo rm -f "$f" < /dev/null
                    echo "  已删除 $f"
                    removed=$((removed + 1))
                done
            else
                echo "找不到 Qt 插件目录，也没有安装清单。" >&2
                echo "请手动删除 hinge_glass.so 与 kwin_hinge_glass_config.so。" >&2
            fi
        fi

        kwriteconfig6 --file kwinrc --group Plugins --key hinge_glassEnabled --notify false || true
        echo "已卸载 $removed 个文件。注销重登后完全生效。"
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
