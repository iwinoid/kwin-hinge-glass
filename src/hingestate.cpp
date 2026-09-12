/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "hingestate.h"

#include <algorithm>
#include <cmath>

namespace HingeGlass
{

namespace
{
/// θ 变化小于此值视为"没有变化"，不必重绘
constexpr double kThetaEpsilonDeg = 1e-3;

/// Active 阶段 θ 趋近目标的时间常数。状态切换靠它保持连续。
constexpr double kThetaTauMs = 80.0;

/// 半隐式欧拉的步长夹持。与 SpringFreq=16 一起保证 freq*dt < 2 的稳定条件。
constexpr double kMinDtSec = 1.0 / 240.0;
constexpr double kMaxDtSec = 1.0 / 20.0;

/// 指数趋近：与帧率无关
double approach(double current, double target, double dtMs, double tauMs)
{
    if (tauMs <= 0.0) {
        return target;
    }
    return current + (target - current) * (1.0 - std::exp(-dtMs / tauMs));
}
} // namespace

HingeState::HingeState(const Config &cfg)
    : m_cfg(cfg)
{
}

double HingeState::thetaTarget() const
{
    // 单侧：只有低于原角度才有效果，越往下越强
    return std::clamp(m_cfg.originalAngle - m_springValue, 0.0, m_cfg.maxAngle);
}

void HingeState::advanceSpring(double dtSec)
{
    const double dt = std::clamp(dtSec, kMinDtSec, kMaxDtSec);

    // 弹簧始终跟踪真实角度 —— 与淡出解耦。
    const double target = static_cast<double>(m_effective);

    const double w = m_cfg.springFreq;
    const double accel = w * w * (target - m_springValue) - 2.0 * w * m_springVelocity;
    m_springVelocity += accel * dt;
    m_springValue += m_springVelocity * dt;
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
        m_armed = false;
        return false;
    }

    if (std::abs(deg - m_lastRaw) >= m_cfg.deadbandDeg) {
        m_armed = true;
        m_lastRaw = deg;
        m_effective = deg;
        m_lastChangeMs = nowMs;
    }

    // 静止判定用「参考点 + 容差」：只有超出容差才换参考点，
    // 于是容差内的抖动不会重置停留计时。
    if (std::abs(static_cast<double>(deg) - static_cast<double>(m_stillRef)) > m_cfg.stillToleranceDeg) {
        m_stillRef = deg;
        m_stillSinceMs = nowMs;
    }

    advanceSpring(dtMs / 1000.0);

    // 从 Stable 或 Release 都可以（重新）进入 Active。
    // Release 也要支持：淡出途中用户又折下去时应当立刻恢复，且 θ 由指数趋近
    // 保证连续、不会从当前值跳回满效果。
    if (m_armed && thetaTarget() > kThetaEpsilonDeg
        && (m_phase == Phase::Stable || m_phase == Phase::Release)) {
        if (m_phase == Phase::Stable) {
            m_startedMs = nowMs;
        }
        m_phase = Phase::Active;
        // 停留计时从**特效出现**那一刻起算，而不是从角度变化那一刻 ——
        // 否则弹簧收敛的那段时间会白白吃掉显示时长。
        m_stillSinceMs = nowMs;
        m_stillRef = m_lastRaw;
        m_armed = false;
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
                m_phase = Phase::Release;
                m_releaseStartMs = nowMs;
            }
        } else if ((nowMs - m_stillSinceMs) >= m_cfg.dwellMs
                   && (nowMs - m_startedMs) >= m_cfg.minEffectMs) {
            // 停手淡出模式：停手 DwellMs 后开始淡出，哪怕还折着
            m_phase = Phase::Release;
            m_releaseStartMs = nowMs;
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
    m_armed = false;
    return needsRepaint;
}

void HingeState::onLidOpened(std::int64_t nowMs)
{
    m_lidClosed = false;
    m_hasEffective = false; // 下一帧重新建立基准
    m_armed = false;
    m_lastTickMs = nowMs;
    m_lastChangeMs = nowMs;
    m_stillSinceMs = nowMs;
}

} // namespace HingeGlass
