#!/usr/bin/env bash
#
# SPDX-FileCopyrightText: 2026 Iwinoid
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 生成 README 用的演示动图。
#
# 流程：合成一张通用桌面图 -> 用离线预览器渲染各折叠角的画面 ->
#      投影到 3D 笔记本模型上合成为帧 -> 编码为循环 WebP。
#
# 依赖：构建好的 hinge_glass_preview、python3（Pillow、numpy）、ffmpeg
#
# 用法： ./tools/demo/build.sh [输出路径]

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="${1:-$ROOT/image/README/demo.webp}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

PREVIEW="${PREVIEW:-$ROOT/build/bin/hinge_glass_preview}"
[[ -x "$PREVIEW" ]] || { echo "找不到 $PREVIEW，先跑 ./rebuild.sh --build" >&2; exit 1; }
command -v ffmpeg >/dev/null || { echo "需要 ffmpeg" >&2; exit 1; }

echo "[1/4] 合成演示用桌面图"
python3 "$ROOT/tools/demo/make_desktop.py" "$WORK/desktop.png"

echo "[2/4] 渲染各折叠角的画面"
# 开合角 100° -> 0°，带缓动；首尾各停几帧便于循环。
# θ 与状态机一致：θ = 上限 × (原角度 − 当前角) / 原角度，默认上限 45、原角度 100。
python3 - "$WORK" <<'PY'
import os, subprocess, sys
work = sys.argv[1]
def ease(t): return t * t * (3 - 2 * t)
lids = [100.0] * 5 + [100.0 * (1 - ease(i / 40)) for i in range(41)] + [0.0] * 5
thetas = sorted({round(45.0 * (100.0 - l) / 100.0, 2) for l in lids})
subprocess.run([os.environ.get("PREVIEW", "build/bin/hinge_glass_preview"),
                "--angles", ",".join(str(t) for t in thetas),
                "--taps", "24", "--input", f"{work}/desktop.png",
                "--out", f"{work}/theta", "--size", "1280x800"], check=True,
               stdout=subprocess.DEVNULL)
open(f"{work}/lids.txt", "w").write("\n".join(f"{l:.3f}" for l in lids))
print(f"      {len(lids)} 帧，{len(thetas)} 个不同折叠角")
PY

echo "[3/4] 投影到笔记本模型并合成"
python3 "$ROOT/tools/demo/make_demo.py" "$WORK" "$WORK/frames"

echo "[4/4] 编码循环 WebP"
mkdir -p "$(dirname "$OUT")"
ffmpeg -y -hide_banner -loglevel error -framerate 20 -i "$WORK/frames/f%03d.png" \
       -c:v libwebp_anim -lossless 0 -q:v 88 -loop 0 -an "$OUT"
printf '%s  %s KB\n' "$OUT" "$(( $(stat -c %s "$OUT") / 1024 ))"
