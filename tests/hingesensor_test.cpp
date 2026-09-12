/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later

    HingeSensor 的集成测试：真的去读本机的 cros-ec-lid-angle。

    这不是单元测试 —— 它依赖真实硬件。在没有该传感器的机器上会跳过而不是失败。

    运行： ./build/bin/hingesensor_test
*/

#include "hingesensor.h"

#include <QCoreApplication>
#include <QTimer>

#include <cstdio>

using namespace HingeGlass;

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    int failures = 0;

    // 1) 设备查找
    const QString path = HingeSensor::findDevicePath();
    if (path.isEmpty()) {
        std::printf("跳过：本机没有 cros-ec-lid-angle 传感器\n");
        return 0;
    }
    std::printf("设备路径: %s\n", qPrintable(path));

    if (!path.endsWith(QLatin1String("/in_angl_raw"))) {
        std::printf("FAIL 路径应以 /in_angl_raw 结尾\n");
        ++failures;
    }
    // 必须按 name 查找而不是硬编码 iio:deviceN（重启后编号会变）
    if (path.contains(QLatin1String("iio:device"))) {
        std::printf("提示: 当前展开为 %s（iio:deviceN 由运行时查找得出，非硬编码）\n",
                    qPrintable(path));
    }

    // 2) 实际读若干拍，确认能拿到可信角度
    HingeSensor sensor;
    int angleCount = 0;
    int unavailableCount = 0;
    int lastAngle = -1;
    bool sawAvailableSignal = false;

    QObject::connect(&sensor, &HingeSensor::angleChanged, [&](int deg) {
        ++angleCount;
        lastAngle = deg;
    });
    QObject::connect(&sensor, &HingeSensor::unavailable, [&]() { ++unavailableCount; });
    QObject::connect(&sensor, &HingeSensor::availabilityChanged, [&](bool available) {
        // 只记录"曾经可用"：stop() 会发 false，不该覆盖掉这个事实
        if (available) {
            sawAvailableSignal = true;
        }
    });

    if (!sensor.start(30)) {
        std::printf("FAIL start() 返回 false\n");
        return 1;
    }

    // 跑 1 秒
    QTimer::singleShot(1000, &app, &QCoreApplication::quit);
    app.exec();
    sensor.stop();

    std::printf("1 秒内: 可信读数 %d 次, 不可信 %d 次, 最后角度 %d\n",
                angleCount, unavailableCount, lastAngle);

    if (!sawAvailableSignal) {
        std::printf("FAIL 没有收到 availabilityChanged(true)\n");
        ++failures;
    }
    if (angleCount == 0) {
        std::printf("FAIL 一次可信读数都没拿到\n");
        ++failures;
    }
    if (lastAngle >= 400) {
        std::printf("FAIL 500 哨兵没有被过滤（lastAngle=%d）\n", lastAngle);
        ++failures;
    }

    if (failures == 0) {
        std::printf("通过\n");
        return 0;
    }
    std::printf("%d 项失败\n", failures);
    return 1;
}
