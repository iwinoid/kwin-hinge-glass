#!/usr/bin/env bash
# 轮询铰链角度传感器 (cros-ec-lid-angle)
# 用法: ./hinge_poll.sh [间隔秒，默认0.2] [--once 只读一次]
# 退出: Ctrl+C

INTERVAL="${1:-0.2}"
# 允许 ./hinge_poll.sh --once 或 ./hinge_poll.sh 0.5 --once
ONCE=0
[[ "$1" == "--once" || "$2" == "--once" ]] && ONCE=1
[[ "$1" == "--once" ]] && INTERVAL=0.2

# 自动定位设备（不硬编码 iio:deviceN，重启后编号可能变）
find_hinge_dev() {
    for d in /sys/bus/iio/devices/iio:device*; do
        [[ -f "$d/name" && -f "$d/in_angl_raw" ]] || continue
        if [[ "$(cat "$d/name" 2>/dev/null)" == "cros-ec-lid-angle" ]]; then
            echo "$d/in_angl_raw"
            return 0
        fi
    done
    return 1
}

ANGLE_FILE="$(find_hinge_dev)"
if [[ -z "$ANGLE_FILE" ]]; then
    echo "错误: 没找到 cros-ec-lid-angle 设备" >&2
    echo "可用设备:" >&2
    grep -H . /sys/bus/iio/devices/iio:device*/name 2>/dev/null >&2
    exit 1
fi

mode_of() {
    local a=$1
    if (( a <= 15 )); then echo "合盖/关闭";
    elif (( a < 175 )); then echo "笔记本模式";
    elif (( a < 195 )); then echo "180° 临界";
    elif (( a < 345 )); then echo "帐篷/翻转中";
    else echo "平板模式"; fi
}

echo "设备: $ANGLE_FILE"
echo "间隔: ${INTERVAL}s，按 Ctrl+C 退出"
echo "----------------------------------------"

if (( ONCE )); then
    a=$(cat "$ANGLE_FILE" 2>/dev/null) || { echo "读取失败"; exit 1; }
    printf '%s  %3s°  %s\n' "$(date +%H:%M:%S)" "$a" "$(mode_of "$a")"
    exit 0
fi

trap 'echo; echo "退出"; exit 0' INT TERM
while true; do
    if a=$(cat "$ANGLE_FILE" 2>/dev/null); then
        # \r 原地刷新，转轴动起来数字会跟着跳
        printf '\r%s  %3s°  %s      ' "$(date +%H:%M:%S)" "$a" "$(mode_of "$a")"
    else
        printf '\r%s  读取失败      ' "$(date +%H:%M:%S)"
    fi
    sleep "$INTERVAL"
done
