# 铰链玻璃

[English](README.md)

[![standard-readme compliant](https://img.shields.io/badge/readme%20style-standard-brightgreen?style=for-the-badge)](https://github.com/RichardLitt/standard-readme)
[![CI](https://img.shields.io/github/actions/workflow/status/iwinoid/kwin-hinge-glass/ci.yml?style=for-the-badge&label=CI&logo=github)](https://github.com/iwinoid/kwin-hinge-glass/actions/workflows/ci.yml)
[![License: GPL-3.0-or-later](https://img.shields.io/badge/License-GPL--3.0--or--later-blue?style=for-the-badge)](https://spdx.org/licenses/GPL-3.0-or-later.html)
[![powered by DeepSeek](https://img.shields.io/badge/powered_by-DeepSeek_V4.1-4D6BFE?style=for-the-badge&logo=deepseek&logoColor=white)](https://deepseek.com)
[![powered by dsh](https://img.shields.io/badge/powered_by-dsh-4D6BFE?style=for-the-badge&logo=deepseek&logoColor=white)](https://github.com/deepseek-ai/deepseek-harness)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)](https://www.linux.org/)
[![KDE Plasma 6](https://img.shields.io/badge/KDE_Plasma-6-1D99F3?style=for-the-badge&logo=kde&logoColor=white)](https://kde.org/plasma-desktop)

笔记本屏幕开合时，把桌面折成磨砂玻璃的 KWin 特效。

铰链玻璃读取翻转本的转轴角度传感器。它把屏幕渲染成一块玻璃，这块玻璃从**固定在空间中的内容平面**上转开。平面固定，玻璃在它上面移动，两者的间距决定霜化程度。这个效果回应 iPhone Duo 的折叠动画，并适配到笔记本的底部转轴。

特效把场景渲染进自己的帧缓冲，再用自定义着色器画回屏幕。它直接取用实时场景。因此无需录屏授权，也能跟随转轴。

## 截图

![屏幕合上时桌面折成磨砂玻璃，再展开](image/README/hinge-glass-demo.webp)

屏幕由 100° 合上再展开，动图循环播放。

## 目录

- [截图](#截图)
- [背景](#背景)
- [安装](#安装)
  - [依赖](#依赖)
  - [先检查你的机器](#先检查你的机器)
  - [其他传感器](#其他传感器)
  - [从 AUR 安装](#从-aur-安装)
  - [其他发行版](#其他发行版)
  - [从源码构建](#从源码构建)
  - [卸载](#卸载)
- [用法](#用法)
- [配置](#配置)
  - [转轴位置](#转轴位置)
- [疑难](#疑难)
- [架构](#架构)
- [维护者](#维护者)
- [致谢](#致谢)
- [参与](#参与)
- [更新记录](#更新记录)
- [许可](#许可)

## 背景

翻转本可以折过 360 度。转轴角度传感器报告这个动作。多数软件忽略它。

iPhone Duo 给出了另一种做法。内屏展开时，一半已经放平并显示正常内容。另一半仍然倾斜，一个动画让它看起来像是一块停在水平面里的屏幕。

铰链玻璃把这个做法用到笔记本上。整块屏幕扮演那一半倾斜的屏幕。桌面充当固定在空间中的平面。屏幕移动时，内容看起来留在原地，而玻璃在它上面翻转。

着色器来自一个 macOS 原型。见[致谢](#致谢)。

## 安装

### 依赖

- KDE Plasma 6，Wayland 下的 KWin
- OpenGL 合成后端
- 转轴角度传感器。特效读取 `cros-ec-lid-angle` 这个 IIO 设备，由 ChromeOS EC 驱动提供。

构建依赖：

```
base-devel cmake ninja extra-cmake-modules
qt6-base qt6-tools kwin kconfig kcoreaddons ki18n kwidgetsaddons kcmutils
vulkan-headers
```

KWin 在构建时依赖 `vulkan-headers`。装好它，`find_package(KWin)` 才能通过。

### 先检查你的机器

运行这条命令：

```bash
./hinge_poll.sh --once
```

如果它打印出一个角度，说明你的机器有这个传感器，特效就能用。

Chromebook 全系都有。驱动来自 ChromeOS EC，因此其他使用 Chrome EC 的笔记本也可能有。这条检查只要一秒，值得一试。

### 其他传感器

特效只读一个 IIO 设备：`cros-ec-lid-angle`。本特效需要这个设备。

传感器层只有一个文件：`src/hingesensor.cpp`。函数 `findDevicePath()` 负责挑设备，读取部分把 `in_angl_raw` 当作整数度数，范围 0 到 360。

要接入别的传感器，改 `findDevicePath()`，或替换该文件里的读取逻辑。请遵守两条规则：

- 角度用度数，范围 0 到 360。
- 特效把 400 及以上的值当作"无数据"，此时保持上一个有效角度。读数不可信时用这个值。

### 从 AUR 安装

```bash
paru -S kwin-hinge-glass
```

### 其他发行版

AUR 上的包只管 Arch。其他地方请从源码构建。构建需要 KWin 的开发头文件、Qt 6、KF6 与 libepoxy。

Arch：

```bash
sudo pacman -S --needed base-devel cmake ninja extra-cmake-modules qt6-base \
  qt6-tools kwin kconfig kcoreaddons ki18n kwidgetsaddons kcmutils \
  kwindowsystem kcolorscheme kconfigwidgets libepoxy vulkan-headers
```

Debian 与 Ubuntu：

```bash
sudo apt install cmake ninja-build extra-cmake-modules qt6-base-dev \
  qt6-tools-dev kwin-dev libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev \
  libkf6widgetsaddons-dev libkf6kcmutils-dev libkf6windowsystem-dev \
  libkf6colorscheme-dev libkf6configwidgets-dev libepoxy-dev libvulkan-dev
```

Fedora：

```bash
sudo dnf install cmake ninja-build extra-cmake-modules qt6-qtbase-devel \
  qt6-qttools-devel kwin-devel kf6-kconfig-devel kf6-kcoreaddons-devel \
  kf6-ki18n-devel kf6-kwidgetsaddons-devel kf6-kcmutils-devel \
  kf6-kwindowsystem-devel kf6-kcolorscheme-devel kf6-kconfigwidgets-devel \
  libepoxy-devel vulkan-headers
```

然后运行 `./rebuild.sh`。

KWin 特效装不成 Flatpak 或 Snap。它是加载进合成器进程里的插件，必须放在系统插件目录。所以只剩源码构建和发行版打包两条路。

目前没有其他发行版打包这个特效。如果你想打，源码用的是标准 CMake 与 extra-cmake-modules；`rebuild.sh` 通过 `qmake6` 询问插件目录，而不是假定 `/usr/lib/qt6/plugins`，所以在 Fedora 与 Debian 的目录布局下也能用。

### 从源码构建

```bash
git clone https://github.com/iwinoid/kwin-hinge-glass.git
cd hinge-glass
./rebuild.sh
```

`rebuild.sh` 会干净重建、跑测试，并把两个文件装进 Qt 插件目录。安装步骤需要 `sudo`。

`./rebuild.sh --build` 只构建。`./rebuild.sh --uninstall` 卸载。

首次加载：打开 **系统设置 → 桌面特效**，在*外观*组里启用 **铰链玻璃**。KWin 会立即加载，即时生效。

如果你之前构建过并装了新版本，请注销重登。KWin 会把加载过的插件一直留在内存里。见[疑难](#疑难)。

### 卸载

```bash
./rebuild.sh --uninstall
```

## 用法

1. 把**原角度**设成你日常使用的角度。把屏幕开到那个位置，用 `./hinge_poll.sh --once` 读出数值。默认是 100 度。
2. 把屏幕折到该角度以下。玻璃效果出现，并随屏幕合拢而增强。
3. 停住屏幕。300 毫秒后画面恢复正常。

`hinge_poll.sh` 打印实时转轴角度和所处模式。用它检查传感器，或找一个合适的原角度。

```bash
./hinge_poll.sh          # 实时，Ctrl+C 退出
./hinge_poll.sh --once   # 读一次
```

## 配置

打开 **系统设置 → 桌面特效 → 铰链玻璃 → ⚙**。

| 配置项 | 默认 | 作用 |
|---|---|---|
| 原角度 | 100° | 角度大于等于它时特效关闭。低于它则出现，并随折下去而增强。 |
| 折叠角上限 | 45° | 完全合上时的折叠角。强度按「折了多少 / 原角度」增长，因此全程都在变化。 |
| 强度曲线指数 | 0.65 | 强度曲线的形状。取 1.0 时线性增长，要约 370 毫秒才看得出来；取 0.65 时约 190 毫秒，折叠中段也更强。 |
| 判定已停手的容差 | 10° | 容差内的移动，停留计时继续走。 |
| 转轴位置 | 0 | 有效旋转圆心到屏幕底边的距离，除以屏高。见下节。 |
| 停手后保持特效 | 300 毫秒 | 屏幕停住后特效保留的时长。可填 0 到 5000 的任意值。 |
| 停手时长分段 | `80:2000,100:300` | 按角度分段的停手时长，格式「角度:毫秒」。低于 80° 时停手后保持 2000 毫秒，80° 到 100° 保持 300 毫秒。留空则全程用上面的统一值。 |
| 停手后永久保持 | 关 | 折着就一直保留，回到原角度才结束。适合演示。 |
| 恢复正常显示用时 | 180 毫秒 | 淡出时长。「永久保持」关闭时生效。 |
| 最短显示时长 | 350 毫秒 | 特效在屏幕上停留的最短时间，防止一闪而过。 |
| 霜化强度 | 0.09 | 霜化程度。可用范围很窄，0 到 0.18。 |
| 眼距 | 2.4 | 透视强度。数值越大，效果越平。 |
| 霜化采样数 | 24 | 霜化的采样次数。取值越低越省，模糊量也越少。 |
| 空闲采样率 | 20 Hz | 屏幕静止时的传感器采样率。调高起效更快，调低更省电。 |
| 活动采样率 | 30 Hz | 屏幕移动时的传感器采样率。 |
| 角度平滑弹簧 | 30 rad/s | 把传感器读数平滑到屏幕刷新率。这一项决定响应速度，取值偏低会让效果明显来迟。 |
| 演示模式 | 关 | 固定住一个折叠角，特效忽略传感器。演示与调参时无需移动屏幕。 |

效果淡出后，**再往下折就会重新出现**。原地小幅移动不会重触发，往反方向折也不会。

所有配置项都存在 `kwinrc` 的 `[Effect-hinge_glass]` 组里。

### 转轴位置

着色器让屏幕绕一条线转动。这一项说明那条线在哪里。

**理想单转轴。** 旋转圆心正好在屏幕底边上。底边落在圆心上，只有顶边移动。这是默认值，也是参考实现的行为。

**实际单转轴。** 旋转圆心在屏幕底边**下方**，因为转轴机构需要空间。于是底边也会移动一点。底边和顶边绕同一个圆心各走一个圆。这种情况填负值：把偏移量除以屏高。屏幕高 200 毫米、圆心在底边下方 16 毫米时，填 -0.08。

**圆心在屏幕内部。** 正值会把圆心抬到底边以上。圆心以下的部分保持正常。

面板的物理尺寸常常缺失。自己量出屏高和偏移量，再相除。

## 疑难

### KWin 升级后特效失效

CI 每天对 Arch 仓库里的 KWin 版本构建一次。绿色表示代码仍能对当天的 KWin 编译；红色表示要改代码才能重建。

AUR 上的包从源码构建，所以产物总是匹配你机器上的 KWin。KWin 升级后重新跑一次 `paru -S kwin-hinge-glass` 即可。包的版本号会体现这一点：需要重建时 `0.2.0-1` 会变成 `0.2.0-2`。

插件工厂 ID 里含 KWin 版本号，例如 `org.kde.kwin.EffectPluginFactory6.7.5`。KWin 的补丁版本升级会改变这个 ID。KWin 随后拒绝该插件，并且只写一行调试日志。每次 KWin 升级后运行 `./rebuild.sh`。

Plasma 的补丁版本按固定排期发布，间隔依次是 1、1、2、3、5、8 周。每四个月来一次大版本，然后重新开始这个节奏。所以大版本之后的三个月里大约要重编五次，之后会安静一阵。排期见 [Plasma 6 schedule](https://community.kde.org/Schedules/Plasma_6)。

### 新构建仍在跑旧代码

KWin 会把加载过的插件一直留在内存里。禁用再启用只会产生新实例，代码仍是旧的。请注销重登。

确认方法：检查被映射的文件。

```bash
sudo grep hinge_glass /proc/$(pgrep -x kwin_wayland)/maps
```

出现 `(deleted)` 标记，就说明 KWin 用的是已被替换掉的文件。

### 特效保持关闭

检查传感器是否存在：

```bash
./hinge_poll.sh --once
```

如果该命令失败，说明机器缺少 `cros-ec-lid-angle` 传感器，KWin 会跳过这个特效。桌面特效页对插件特效一律显示为可用。因此缺少传感器时，看起来就像特效坏了。见[先检查你的机器](#先检查你的机器)。

## 架构

| 部分 | 文件 | 职责 |
|---|---|---|
| 特效入口 | `src/main.cpp` | 声明插件工厂。 |
| 特效 | `src/hinge_glass.cpp` | 接入 KWin，把场景渲染进帧缓冲，画出玻璃。 |
| 着色器 | `src/glassshader.h` | 玻璃片元着色器。 |
| 状态机 | `src/hingestate.cpp` | 把转轴角度映射成折叠角。只有纯 C++，因此测试无需显示环境。 |
| 传感器 | `src/hingesensor.cpp`、`src/lidsensor.cpp` | 从 sysfs 读角度，从 KWin 输入事件读合盖开关。 |
| 设置面板 | `src/kcm/` | KCM 页面。 |
| 离线预览 | `tools/preview/` | 脱离 KWin 把着色器渲染成 PNG。用它检查着色器改动。 |
| 演示生成 | `tools/demo/` | 生成本 README 里的演示动图。运行 `./tools/demo/build.sh`。 |

在构建目录里用 `ctest` 跑测试。

## 维护者

- [Iwinoid](https://github.com/iwinoid)

## 致谢

- **MacBook Duo / Hinge Glass**（[jlxc2001/MacBook-Duo](https://github.com/jlxc2001/MacBook-Duo)）：一个 macOS 原型。`src/glassshader.h` 里的玻璃着色器来自它。该仓库没有许可证，而且是作者自己说明的。这意味着什么见 [NOTICE](NOTICE)。
- [Atomicx7/Duo-animation](https://github.com/Atomicx7/Duo-animation)。原始折叠动画的投影模型。
- [sumimakito/Mac-Duo](https://github.com/sumimakito/Mac-Duo)，Apache-2.0，Copyright 2026 Makito。时序与平滑机制来自这里。
- [KWin](https://invent.kde.org/plasma/kwin)，GPL-2.0-or-later。特效 API，以及 `screentransform` 与 `zoom` 两个特效。它们示范了离屏帧缓冲的用法。
- [kwin-effects-better-blur-dx](https://github.com/xarblu/kwin-effects-better-blur-dx)，GPL-3.0。第三方 KWin 6 特效，作为构建系统的参考。

## 参与

问题和缺陷报告欢迎发到 [GitHub Issues](https://github.com/iwinoid/kwin-hinge-glass/issues)。接受拉取请求。

大改动请先开 issue。着色器改动请先用 `tools/preview` 检查，因为错误的着色器在实际会话里很难看出来。

## 更新记录

- **0.2.0**：首个版本。含转轴角度传感器、玻璃着色器、状态机、设置面板、离线预览工具。

## 许可

[GPL-3.0-or-later](LICENSE) © Iwinoid

本项目使用 [KWin](https://invent.kde.org/plasma/kwin)（GPL-2.0-or-later）与 [Qt 6](https://qt.io)（LGPL-3.0 / GPL-2.0）。完整归属见 [NOTICE](NOTICE)。
