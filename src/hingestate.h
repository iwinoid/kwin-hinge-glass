/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace HingeGlass
{

/// 一个「停手时长」分段：角度低于 maxAngleDeg 时，停手后保持 dwellMs 毫秒。
struct DwellSegment
{
    double maxAngleDeg = 0.0;
    int dwellMs = 300;
};

/**
 * 解析分段配置字符串，形如 "80:2000,100:300"。
 *
 * 语义：角度 < 80 用 2000ms；80 <= 角度 < 100 用 300ms；再往上没有匹配项。
 * 返回按 maxAngleDeg 升序排好的列表。无法解析的条目直接跳过 ——
 * 配置写错不该让整个特效失效。
 */
std::vector<DwellSegment> parseDwellSegments(const std::string &spec);

struct Config
{
    /// 停手后保持特效的时长（没有匹配到分段时的兜底）
    int dwellMs = 300;

    /// 按角度分段的停手时长，按 maxAngleDeg 升序。空表示全程用 dwellMs。
    std::vector<DwellSegment> segments;

    /// 取某个角度对应的停手时长
    int dwellFor(double angleDeg) const;
    /// 恢复正常显示的淡出时长
    int fadeMs = 180;
    /// 进入 Active 后至少显示这么久，防止刚触发就熄灭
    int minEffectMs = 350;

    /// 原角度：**角度大于等于它时完全无效果**，低于它才出现，越往下越强。
    ///
    /// 单侧是有意的。原版 HingeGlass 的语义就是"展开到位即归零"
    /// （remaining = 1 - angle/openAngle）；iPhone Duo 也是"展开到水平面那一半
    /// 显示正常内容，未到位的那一半才有效果"。若改成双侧（偏离就有效果），
    /// 日常使用的 120-160° 区间会一直处于满效果。
    double originalAngle = 100.0;

    /// 折叠角上限：(原角度 − maxAngle) 及以下达到满效果
    double maxAngle = 45.0;

    /// 显示时长模式（两种都保留，可切换）：
    ///   false = 停手 dwellMs 后淡出恢复正常显示，哪怕还折着（默认）
    ///   true  = 只要角度还低于原角度就持续显示，回到原角度才消失
    ///           （= 纯绝对位置模型，iPhone Duo 的做法）
    /// 合盖在两种模式下都会立即取消。
    bool persistWhileFolded = false;

    /// 判定"已停手"的位移容差
    double stillToleranceDeg = 10.0;
    /// 角度平滑弹簧频率（rad/s）
    double springFreq = 16.0;
};

enum class Phase {
    Stable,
    Active,
    Release,
};

/**
 * 铰链角度 → 折叠角 θ 的映射与状态机。
 *
 * 纯逻辑，零 Qt / 零 KWin 依赖（连 Qt 头文件都不引），可脱离图形环境单测。
 *
 * 强度用**绝对位置**：θ = clamp(原角度 − 平滑角, 0, 上限)。
 * 显示时长用**相对位移**：停手 DwellMs 后淡出，哪怕还折着。
 *
 * 两层平滑：
 *   1. 角度弹簧 —— 把低频传感器读数平滑到屏幕刷新率，同时吸收抖动
 *   2. θ 的指数趋近 —— 让状态切换连续。**这一层是必需的**：
 *      否则"折到 55° 停手淡出后再动 10°"会让 θ 从 0 直接跳回满效果。
 *
 * 关于「不可信读数」：cros-ec 的 500 哨兵由调用方转成 onNoData()，本类不感知 ——
 * "保持上一个有效角度"自然等价于"这一拍没有新数据"，
 * 「折到 360° 时 EC 失效 → 停留计时 → 淡出」这条路径无需任何特判。
 */
class HingeState
{
public:
    explicit HingeState(const Config &cfg = Config());

    /// 收到一个可信角度读数（0..360）。需要重绘返回 true。
    bool onAngle(int deg, std::int64_t nowMs);

    /// 这一拍没有可信读数（500 哨兵 / 读取失败）。需要重绘返回 true。
    bool onNoData(std::int64_t nowMs);

    /// 合盖。立即终止，跳过停留与淡出。需要立刻重绘一次返回 true。
    bool onLidClosed();

    /// 开盖。重新建立基准。
    void onLidOpened(std::int64_t nowMs);

    Phase phase() const { return m_phase; }
    bool active() const { return m_phase != Phase::Stable; }

    /// 折叠角（度），已平滑且非负
    double theta() const { return m_theta; }
    /// 角度弹簧输出的平滑角度（度）
    double smoothedAngle() const { return m_springValue; }
    /// 最近一次可信的原始角度
    int effectiveAngle() const { return m_effective; }

private:
    bool step(std::int64_t nowMs, double dtMs);
    /// 进入淡出：记录当前折叠深度，作为再次触发的门槛
    void enterRelease(std::int64_t nowMs);
    void advanceSpring(double dtSec);
    /// 该平滑角度对应的目标 θ（单侧：只有低于原角度才非零）
    double thetaTarget() const;

    Config m_cfg;

    Phase m_phase = Phase::Stable;

    int m_effective = 0;
    int m_lastRaw = 0;   // 最近一次原始读数（仅用于合盖/开盖的重建基准）
    bool m_hasEffective = false;
    bool m_lidClosed = false;

    /// 上次淡出结束时的 θ 目标值，用作"再次触发"的门槛。
    ///
    /// 重触发判据是「θ 目标超过这个值」—— 也就是**比上次淡出时折得更深**。
    /// 用它取代固定死区的原因：
    ///   * 折着不动、原地晃动都不会重触发
    ///   * 继续往下折则立刻回来，且不受死区门槛限制
    ///   * 首次起效也没有门槛（参考值为 0），慢折时不再被死区吃掉几百毫秒
    /// 角度回到原角度上方时复位为 0。
    double m_retriggerTarget = 0.0;

    // 角度弹簧（半隐式欧拉）
    double m_springValue = 0.0;
    double m_springVelocity = 0.0;
    std::int64_t m_lastTickMs = 0;

    // θ 自身的状态，用指数趋近保证状态切换连续
    double m_theta = 0.0;

    // 静止判定：参考点 + 容差（参考点只在超出容差时才更新）
    int m_stillRef = 0;
    std::int64_t m_stillSinceMs = 0;

    std::int64_t m_lastChangeMs = 0;
    std::int64_t m_startedMs = 0;
    std::int64_t m_releaseStartMs = 0;

    // 用于判断"是否需要重绘"，稳定后不再请求重绘（省电）
    double m_prevTheta = 0.0;
    Phase m_prevPhase = Phase::Stable;
};

} // namespace HingeGlass
