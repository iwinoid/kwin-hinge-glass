/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    HingeState 的单元测试。零依赖：连 Qt 都不需要，直接编译成独立可执行文件。

    模型要点（与旧版不同）：
      * 强度用**绝对位置**：θ = clamp(原角度 − 平滑角, 0, 上限)，**单侧**
        —— 角度 >= 原角度时完全无效果
      * 显示时长有两种模式（persistWhileFolded），合盖在两种下都立即取消
      * θ 用指数趋近而非直接赋值，所以状态切换永远连续

    运行： ./build/bin/hingestate_test
*/

#include "hingestate.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace HingeGlass;
using Clock = std::int64_t;

static int g_failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                               \
        }                                                               \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                             \
    do {                                                                  \
        const double _a = (a);                                            \
        const double _b = (b);                                            \
        if (std::fabs(_a - _b) > (eps)) {                                 \
            std::printf("FAIL %s:%d  %s=%.4f expected %.4f (tol %.4f)\n", \
                        __FILE__, __LINE__, #a, _a, _b, double(eps));     \
            ++g_failures;                                                 \
        }                                                                 \
    } while (0)

/// 默认测试配置：弹簧调快以便快速收敛，其余贴近产品默认
static Config cfg(double originalAngle = 100.0, double maxAngle = 45.0,
                  bool persist = false, int dwell = 300, int fade = 180,
                  int minEffect = 0, double springFreq = 40.0)
{
    Config c;
    c.originalAngle = originalAngle;
    c.maxAngle = maxAngle;
    c.persistWhileFolded = persist;
    c.dwellMs = dwell;
    c.fadeMs = fade;
    c.minEffectMs = minEffect;
    c.stillToleranceDeg = 10.0;
    c.springFreq = springFreq;
    return c;
}

/// 以 8ms 步长持续喂同一个角度，精确推进 durationMs
static Clock feed(HingeState &s, int deg, Clock t, int durationMs)
{
    const Clock end = t + durationMs;
    while (t < end) {
        t = std::min(t + 8, end);
        s.onAngle(deg, t);
    }
    return t;
}

/// 持续 onNoData（模拟 500 哨兵 / 无读数）
static Clock feedNoData(HingeState &s, Clock t, int durationMs)
{
    const Clock end = t + durationMs;
    while (t < end) {
        t = std::min(t + 8, end);
        s.onNoData(t);
    }
    return t;
}

/// 持续喂同一个角度直到进入目标 phase。返回推进后的时间。
static Clock feedUntilPhase(HingeState &s, int deg, Clock t, Phase target, int maxMs = 5000)
{
    if (s.phase() == target) {
        return t;
    }
    for (int e = 0; e < maxMs; e += 8) {
        t += 8;
        s.onAngle(deg, t);
        if (s.phase() == target) {
            return t;
        }
    }
    return t;
}

// ---------------------------------------------------------------- 基础

static void test_initial_state()
{
    HingeState s(cfg());
    CHECK(s.phase() == Phase::Stable);
    CHECK(!s.active());
    CHECK_NEAR(s.theta(), 0.0, 1e-9);
}

static void test_first_reading_is_baseline()
{
    HingeState s(cfg());
    CHECK(s.onAngle(150, 0) == false);
    CHECK(s.phase() == Phase::Stable);
    CHECK(s.effectiveAngle() == 150);
}

// ---------------------------------------------------------------- 单侧绝对映射

static void test_above_original_angle_has_no_effect()
{
    // 新模型的核心行为：日常使用的 120-160° 区间完全不受打扰。
    // 旧模型（相对上次停稳位置的位移）会在这里被晃动反复触发。
    HingeState s(cfg(100.0));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 160, t, 1000);
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-9);

    t = feed(s, 130, t, 1000);
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-9);
}

static void test_jitter_above_original_angle_never_triggers()
{
    // 模拟打字时挪动笔记本：在原角度上方 ±12° 来回晃
    HingeState s(cfg(100.0));
    Clock t = 0;
    s.onAngle(150, t);
    for (int i = 0; i < 10; ++i) {
        t = feed(s, (i % 2 == 0) ? 162 : 138, t, 200);
        if (s.phase() != Phase::Stable) {
            CHECK(s.phase() == Phase::Stable);
            return;
        }
    }
    CHECK_NEAR(s.theta(), 0.0, 1e-9);
}

static void test_below_original_angle_produces_effect()
{
    // 用持续模式隔离"映射"这一件事，不受停手淡出干扰
    HingeState s(cfg(100.0, 45.0, /*persist=*/true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 80, t, 1500); // 折到 80 -> 目标 θ = 20

    CHECK(s.phase() == Phase::Active);
    CHECK_NEAR(s.theta(), 20.0, 0.5);
}

static void test_theta_is_clamped_to_max()
{
    HingeState s(cfg(100.0, 45.0, /*persist=*/true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 30, t, 2000); // 100-30 = 70 -> 钳到 45

    CHECK_NEAR(s.theta(), 45.0, 0.5);
}

static void test_exactly_at_original_angle_is_zero()
{
    HingeState s(cfg(100.0, 45.0, /*persist=*/true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 100, t, 1500);
    CHECK_NEAR(s.theta(), 0.0, 0.5);
}

static void test_holding_still_does_not_retrigger()
{
    // 停手淡出后完全不动，不应重新触发。
    HingeState s(cfg(100.0, 45.0, false, 100, 150));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 1200);
    CHECK(s.phase() == Phase::Stable);

    t = feed(s, 60, t, 800);             // 保持不动
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-6);
}

static void test_deeper_fold_retriggers()
{
    HingeState s(cfg(100.0, 45.0, false, 100, 150));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 1200);
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-6);

    t = feed(s, 40, t, 60);              // 继续往下折（dwell 100ms，别喂过头）
    CHECK(s.phase() == Phase::Active);
    CHECK(s.theta() > 0.0);
}

static void test_return_above_original_resets_gate()
{
    // 折到 60 淡出后，把屏幕开回原角度上方，再折下来应当重新算作首次。
    HingeState s(cfg(100.0, 45.0, false, 100, 150));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 1200);
    CHECK(s.phase() == Phase::Stable);

    t = feed(s, 150, t, 800);            // 回到原角度上方
    CHECK(s.phase() == Phase::Stable);

    // 弹簧从 150 走到原角度以下约需 100ms；dwell 100ms 所以这段时间内仍是 Active
    t = feed(s, 95, t, 150);             // 再次折下来（只深 5°）
    CHECK(s.phase() == Phase::Active);
}

static void test_first_entry_has_no_threshold()
{
    // 首次起效不应有任何门槛 —— 慢折时固定死区曾经吃掉几百毫秒。
    HingeState s(cfg(100.0, 45.0, true));   // 持续模式
    Clock t = 0;
    s.onAngle(105, t);                      // 起始在原角度上方
    t = feed(s, 95, t, 200);                // 只折过 5°，没有任何门槛
    CHECK(s.phase() == Phase::Active);
}

// ---------------------------------------------------------------- 两种时长模式

static void test_dwell_mode_fades_out_even_while_folded()
{
    HingeState s(cfg(100.0, 45.0, /*persist=*/false, /*dwell=*/300, /*fade=*/180));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 250); // 停手模式：dwell 300ms 尚未走完
    CHECK(s.phase() == Phase::Active);
    CHECK(s.theta() > 30.0);

    // 一直折着不动，但停手模式会淡出
    t = feed(s, 60, t, 1500);
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-6);
}

static void test_persist_mode_keeps_effect_while_folded()
{
    HingeState s(cfg(100.0, 45.0, /*persist=*/true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 400);
    CHECK(s.phase() == Phase::Active);

    // 折着不动很久也应当继续显示
    t = feed(s, 60, t, 3000);
    CHECK(s.phase() == Phase::Active);
    CHECK(s.theta() > 35.0);
}

static void test_persist_mode_ends_when_back_to_original_angle()
{
    HingeState s(cfg(100.0, 45.0, /*persist=*/true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 1000);
    CHECK(s.phase() == Phase::Active);

    t = feed(s, 150, t, 2000); // 回到原角度上方
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-6);
}

// ---------------------------------------------------------------- 连续性（指数趋近的意义）

static void test_motion_during_fade_is_continuous()
{
    HingeState s(cfg(100.0, 45.0, false, /*dwell=*/400, /*fade=*/400));
    Clock t = 0;
    s.onAngle(150, t);

    t = feedUntilPhase(s, 60, t, Phase::Active);
    t = feed(s, 60, t, 200); // 让 θ 爬起来（dwell 400ms 尚未到）
    CHECK(s.phase() == Phase::Active);
    const double thetaBefore = s.theta();
    CHECK(thetaBefore > 5.0);

    t = feedUntilPhase(s, 60, t, Phase::Release);
    CHECK(s.phase() == Phase::Release);
    const double thetaMid = s.theta();
    CHECK(thetaMid > 0.0);

    // 淡出确实在下降（注意：进入 Release 的那一刻 θ 可能还在向目标爬升，
    // 所以不能拿它和进入前的值比大小）
    t = feed(s, 60, t, 120);
    const double thetaFading = s.theta();
    CHECK(thetaFading < thetaMid);

    // 又折下去 10°，应当回到 Active。
    // θ 必须从当前位置继续，而不是一步跳回满效果。
    t = feed(s, 50, t, 40);
    CHECK(s.phase() == Phase::Active);
    CHECK(s.theta() >= thetaFading);
    CHECK(s.theta() < 45.0);             // 目标已是 45，但 θ 仍在爬升，没有一步跳满
}

static void test_retrigger_after_fade_does_not_jump()
{
    // 这是指数趋近存在的理由：折到 60° 停手淡出后，再动一下，
    // 若直接赋值 θ 会从 0 跳回满效果。
    HingeState s(cfg(100.0, 45.0, false, 100, 150));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 1500); // 起效后完全淡出
    CHECK(s.phase() == Phase::Stable);
    CHECK_NEAR(s.theta(), 0.0, 1e-6);

    // 再动一下（仍在原角度下方，超过死区）
    t = feed(s, 50, t, 40); // 只推进 40ms
    CHECK(s.phase() == Phase::Active);
    CHECK(s.theta() < 20.0); // 必须还在爬升，不能瞬间到 45
}

// ---------------------------------------------------------------- 500 哨兵

static void test_nodata_holds_theta()
{
    HingeState s(cfg(100.0, 45.0, true)); // 持续模式，隔离出"保持"行为
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 40, t, 1500);
    const double before = s.theta();
    CHECK(before > 40.0);

    t = feedNoData(s, t, 300);
    CHECK_NEAR(s.theta(), before, 1.0); // 不出现假跳变
    CHECK(s.phase() == Phase::Active);
}

static void test_nodata_drives_dwell()
{
    // 「折到 360° 时 EC 失效 -> 读数变 500 -> 停留计时 -> 淡出」必须无需特判地成立
    HingeState s(cfg(100.0, 45.0, false, 300, 180));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 200);
    CHECK(s.phase() == Phase::Active);

    t = feedNoData(s, t, 1500);
    CHECK(s.phase() == Phase::Stable);
}

// ---------------------------------------------------------------- 合盖

static void test_lid_close_aborts_immediately_both_modes()
{
    for (bool persist : {false, true}) {
        HingeState s(cfg(100.0, 45.0, persist, 5000, 180));
        Clock t = 0;
        s.onAngle(150, t);
        t = feed(s, 60, t, 500);
        CHECK(s.phase() == Phase::Active);

        CHECK(s.onLidClosed() == true); // 需要重绘一次恢复正常画面
        CHECK(s.phase() == Phase::Stable);
        CHECK_NEAR(s.theta(), 0.0, 1e-9);
        CHECK(s.onLidClosed() == false); // 幂等
    }
}

static void test_readings_ignored_while_lid_closed()
{
    HingeState s(cfg());
    Clock t = 0;
    s.onAngle(150, t);
    s.onLidClosed();
    CHECK(s.onAngle(40, t + 8) == false);
    CHECK(s.phase() == Phase::Stable);
    CHECK(s.onNoData(t + 16) == false);
}

static void test_lid_open_rebaselines()
{
    HingeState s(cfg(100.0, 45.0, true));
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 60, t, 500);
    CHECK(s.phase() == Phase::Active);

    s.onLidClosed();
    s.onLidOpened(t + 5000);

    t = 5000;
    s.onAngle(150, t); // 开盖后是 150，高于原角度
    t = feed(s, 150, t, 1000);
    CHECK(s.phase() == Phase::Stable);
}

// ---------------------------------------------------------------- 省电

static void test_settled_state_stops_requesting_repaints()
{
    HingeState s(cfg(150.0, 45.0, true)); // 原角度 150，喂 150 -> 无效果
    Clock t = 0;
    s.onAngle(150, t);
    t = feed(s, 150, t, 1000);
    CHECK(s.onNoData(t + 8) == false);
    CHECK(s.onNoData(t + 16) == false);
}

int main()
{
    test_initial_state();
    test_first_reading_is_baseline();
    test_above_original_angle_has_no_effect();
    test_jitter_above_original_angle_never_triggers();
    test_below_original_angle_produces_effect();
    test_theta_is_clamped_to_max();
    test_exactly_at_original_angle_is_zero();
    test_holding_still_does_not_retrigger();
    test_deeper_fold_retriggers();
    test_return_above_original_resets_gate();
    test_first_entry_has_no_threshold();
    test_dwell_mode_fades_out_even_while_folded();
    test_persist_mode_keeps_effect_while_folded();
    test_persist_mode_ends_when_back_to_original_angle();
    test_motion_during_fade_is_continuous();
    test_retrigger_after_fade_does_not_jump();
    test_nodata_holds_theta();
    test_nodata_drives_dwell();
    test_lid_close_aborts_immediately_both_modes();
    test_readings_ignored_while_lid_closed();
    test_lid_open_rebaselines();
    test_settled_state_stops_requesting_repaints();

    if (g_failures == 0) {
        std::printf("全部通过\n");
        return 0;
    }
    std::printf("%d 项失败\n", g_failures);
    return 1;
}
