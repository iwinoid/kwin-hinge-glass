/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "hingestate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace HingeGlass
{

namespace
{
/// θ 变化小于此值视为"没有变化"，不必重绘
constexpr double kThetaEpsilonDeg = 1e-3;

/// Active 阶段 θ 趋近目标的时间常数。状态切换靠它保持连续。
constexpr double kThetaTauMs = 80.0;

/// 再次触发所需的额外折叠深度（度）。
/// 1° 足以滤掉传感器噪声，又不会让人感到门槛。
constexpr double kRetriggerEpsDeg = 1.0;

/// 半隐式欧拉的步长夹持。下限防止 dt=0，上限只用来兜住进程被挂起这类异常停顿。
constexpr double kMinDtSec = 1.0 / 240.0;
constexpr double kMaxDtSec = 0.25;

/// 弹簧积分允许的最大 ω·dt。
///
/// 半隐式欧拉在 ω·dt 接近 1 时不再收敛：临界阻尼的弹簧会持续振荡而不是稳定下来。
/// 空闲轮询是 10Hz（dt=100ms），SpringFreq=16 时 ω·dt 会到 1.6 —— 实测平滑角
/// 在 55~106 之间来回跳，thetaTarget 因此大半时间算出来是 0，
/// 表现为「一动就抖、然后才跟上」。
///
/// 所以按这个上限把 dt 细分成多个子步。这样弹簧在任何轮询率下都按真实时间收敛，
/// 而不是随轮询率变慢或发散。
constexpr double kMaxOmegaDt = 0.25;

/// 指数趋近：与帧率无关
double approach(double current, double target, double dtMs, double tauMs)
{
    if (tauMs <= 0.0) {
        return target;
    }
    return current + (target - current) * (1.0 - std::exp(-dtMs / tauMs));
}
} // namespace

std::vector<DwellSegment> parseDwellSegments(const std::string &spec)
{
    // 本项目以 -fno-exceptions 编译，所以不能用 std::stod / try-catch。
    // 用 strtod/strtol 加 endptr 校验，且必须整段被消费 ——
    // 否则 "80abc" 这类会被当成合法值。
    const auto parseItem = [](const std::string &item, double &ang, long &ms) {
        const std::size_t colon = item.find(':');
        if (colon == std::string::npos) {
            return false;
        }
        std::string a = item.substr(0, colon);
        std::string b = item.substr(colon + 1);
        const auto trim = [](std::string &t) {
            const std::size_t f = t.find_first_not_of(" \t");
            const std::size_t l = t.find_last_not_of(" \t");
            t = (f == std::string::npos) ? std::string() : t.substr(f, l - f + 1);
        };
        trim(a);
        trim(b);
        if (a.empty() || b.empty()) {
            return false;
        }
        char *endA = nullptr;
        char *endB = nullptr;
        ang = std::strtod(a.c_str(), &endA);
        ms = std::strtol(b.c_str(), &endB, 10);
        return endA == a.c_str() + a.size() && endB == b.c_str() + b.size() && ang > 0.0 && ms >= 0;
    };

    std::vector<DwellSegment> out;
    std::size_t pos = 0;
    while (pos < spec.size()) {
        const std::size_t comma = spec.find(',', pos);
        const std::string item = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        double ang = 0.0;
        long ms = 0;
        if (parseItem(item, ang, ms)) {
            out.push_back({ang, int(ms)});
        }
        if (comma == std::string::npos) {
            break;
        }
        pos = comma + 1;
    }
    std::sort(out.begin(), out.end(),
              [](const DwellSegment &a, const DwellSegment &b) { return a.maxAngleDeg < b.maxAngleDeg; });
    return out;
}

int Config::dwellFor(double angleDeg) const
{
    for (const DwellSegment &seg : segments) {
        if (angleDeg < seg.maxAngleDeg) {
            return seg.dwellMs;
        }
    }
    return dwellMs;
}

HingeState::HingeState(const Config &cfg)
    : m_cfg(cfg)
{
}

double HingeState::thetaTarget() const
{
    if (m_cfg.originalAngle <= 0.0) {
        return 0.0;
    }
    // 单侧：只有低于原角度才有效果；强度按「折了多少 / 原角度」归一化，
    // 于是**完全合上时达到满效果**，全程连续响应。
    //
    // 归一化之后开头涨得太慢（线性时折下去 4° 才 θ=1.8°，肉眼看不见），
    // 起效因此要等三百多毫秒。用指数曲线把开头抬起来：折深 4° 时
    // p=0.65 能到 θ=4.7°，起效延迟从 370ms 降到 192ms。
    //
    // 参考实现就是这么做的（remaining = 1 - lidAngle/openAngle，angle = remaining * 80）。
    // 曾经写成 clamp(原角度 − 当前角, 0, 上限) —— 那会在「原角度 − 上限」
    // （默认 100 − 45 = 55°）就触顶，之后一路折到合上，画面都毫无变化。
    const double fraction = std::clamp((m_cfg.originalAngle - m_springValue) / m_cfg.originalAngle,
                                       0.0, 1.0);
    return std::pow(fraction, m_cfg.curveExponent) * m_cfg.maxAngle;
}

void HingeState::advanceSpring(double dtSec)
{
    const double dt = std::clamp(dtSec, kMinDtSec, kMaxDtSec);

    // 弹簧始终跟踪真实角度 —— 与淡出解耦。
    const double target = static_cast<double>(m_effective);

    const double w = m_cfg.springFreq;
    const int steps = std::max(1, int(std::ceil(w * dt / kMaxOmegaDt)));
    const double h = dt / steps;
    for (int i = 0; i < steps; ++i) {
        const double accel = w * w * (target - m_springValue) - 2.0 * w * m_springVelocity;
        m_springVelocity += accel * h;
        m_springValue += m_springVelocity * h;
    }
}

bool HingeState::onAngle(int deg, std::int64_t nowMs)
{
    if (m_lidClosed) {
        return false;
    }

    double dtMs = static_cast<double>(nowMs - m_lastTickMs);
    if (dtMs <= 0.0) {
        dtMs = 1000.0 * kMinDtSec;
    }
    m_lastTickMs = nowMs;

    if (!m_hasEffective) {
        // 首帧只建立基准，不产生效果
        m_effective = deg;
        m_lastRaw = deg;
        m_springValue = deg;
        m_springVelocity = 0.0;
        m_hasEffective = true;
        m_lastChangeMs = nowMs;
        m_startedMs = nowMs;
        m_stillRef = deg;
        m_stillSinceMs = nowMs;
        m_prevTheta = 0.0;
        m_prevPhase = Phase::Stable;
        m_retriggerTarget = 0.0;
        return false;
    }

    // 弹簧目标始终跟随最新读数。平滑交给弹簧本身，不再用死区做第二层量化 ——
    // 曾经用死区限制更新，慢折时 10° 的门槛要等几百毫秒才累积到，
    // 起效因此被明显拖慢。弹簧本来就把噪声滤掉了，不需要再量化一遍。
    m_lastRaw = deg;
    m_effective = deg;

    // 静止判定用「参考点 + 容差」：只有超出容差才换参考点，
    // 于是容差内的抖动不会重置停留计时。
    if (std::abs(static_cast<double>(deg) - static_cast<double>(m_stillRef)) > m_cfg.stillToleranceDeg) {
        m_stillRef = deg;
        m_stillSinceMs = nowMs;
    }

    advanceSpring(dtMs / 1000.0);

    const double target = thetaTarget();
    if (target <= kThetaEpsilonDeg) {
        // 回到原角度上方：彻底复位，下次折下来重新算作首次
        m_retriggerTarget = 0.0;
    }

    // 重触发判据：**比上次淡出时折得更深**。
    // 这样折着不动、原地晃动都不会重触发，而继续往下折则立刻回来。
    // 从 Stable 与 Release 都可以进入 —— 淡出途中继续折下去应当马上恢复，
    // θ 由指数趋近保证连续，不会从当前值跳回满效果。
    if (target > m_retriggerTarget + kRetriggerEpsDeg
        && (m_phase == Phase::Stable || m_phase == Phase::Release)) {
        if (m_phase == Phase::Stable) {
            m_startedMs = nowMs;
        }
        m_phase = Phase::Active;
        // 停留计时从**特效出现**那一刻起算，而不是从角度变化那一刻 ——
        // 否则弹簧收敛的那段时间会白白吃掉显示时长。
        m_stillSinceMs = nowMs;
        m_stillRef = m_lastRaw;
    }

    return step(nowMs, dtMs);
}

bool HingeState::onNoData(std::int64_t nowMs)
{
    if (m_lidClosed || !m_hasEffective) {
        return false;
    }

    double dtMs = static_cast<double>(nowMs - m_lastTickMs);
    if (dtMs <= 0.0) {
        dtMs = 1000.0 * kMinDtSec;
    }
    m_lastTickMs = nowMs;

    // 读数不可信：不更新角度，只推进弹簧与计时。
    // 「折到 360° 时 EC 失效」因此自然落入停留逻辑，无需特判。
    advanceSpring(dtMs / 1000.0);
    return step(nowMs, dtMs);
}

void HingeState::enterRelease(std::int64_t nowMs)
{
    m_phase = Phase::Release;
    m_releaseStartMs = nowMs;
    // **进入淡出时就记下深度**，而不是等淡出结束。
    // 否则淡出途中 target(40) 恒大于旧门槛(0)，会立刻被重触发，淡出永远完不成。
    m_retriggerTarget = thetaTarget();
}

bool HingeState::step(std::int64_t nowMs, double dtMs)
{
    switch (m_phase) {
    case Phase::Stable:
        m_theta = 0.0;
        break;

    case Phase::Active:
        // θ 用指数趋近目标，而不是直接赋值 —— 这让"淡出途中又重新转动"
        // 以及"停手淡出后再动一下"都不会跳变（直接赋值会让 θ 从 0 跳回满效果）。
        m_theta = approach(m_theta, thetaTarget(), dtMs, kThetaTauMs);

        if (m_cfg.persistWhileFolded) {
            // 持续模式：只要还低于原角度就一直显示，回到原角度才消失
            if (thetaTarget() <= kThetaEpsilonDeg) {
                enterRelease(nowMs);
            }
        } else if ((nowMs - m_stillSinceMs) >= m_cfg.dwellFor(m_springValue)
                   && (nowMs - m_startedMs) >= m_cfg.minEffectMs) {
            // 停手淡出模式：停手 DwellMs 后开始淡出，哪怕还折着
            enterRelease(nowMs);
        }
        break;

    case Phase::Release: {
        const double tau = std::max(1.0, m_cfg.fadeMs / 3.0);
        m_theta = approach(m_theta, 0.0, dtMs, tau);
        const bool elapsed = (nowMs - m_releaseStartMs) >= m_cfg.fadeMs;
        if (elapsed || m_theta <= kThetaEpsilonDeg) {
            m_phase = Phase::Stable;
            m_theta = 0.0;
            m_stillRef = m_lastRaw;
            m_stillSinceMs = nowMs;
        }
        break;
    }
    }

    const bool changed = (std::abs(m_theta - m_prevTheta) > kThetaEpsilonDeg) || (m_phase != m_prevPhase);
    m_prevTheta = m_theta;
    m_prevPhase = m_phase;
    return changed;
}

bool HingeState::onLidClosed()
{
    if (m_lidClosed) {
        return false;
    }
    m_lidClosed = true;
    const bool needsRepaint = (m_phase != Phase::Stable) || (m_theta != 0.0);
    m_phase = Phase::Stable;
    m_theta = 0.0;
    m_springVelocity = 0.0;
    m_prevTheta = 0.0;
    m_prevPhase = Phase::Stable;
    m_retriggerTarget = 0.0;
    return needsRepaint;
}

void HingeState::onLidOpened(std::int64_t nowMs)
{
    m_lidClosed = false;
    m_hasEffective = false; // 下一帧重新建立基准
    m_retriggerTarget = 0.0;
    m_lastTickMs = nowMs;
    m_lastChangeMs = nowMs;
    m_stillSinceMs = nowMs;
}

} // namespace HingeGlass
