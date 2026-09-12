/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QFile;
class QTimer;

namespace HingeGlass
{

/**
 * 轮询 cros-ec-lid-angle 的 IIO 原始角度。
 *
 * 设备按 name 属性查找，不硬编码 iio:deviceN —— 重启后编号会变。
 *
 * 读数 >= 400 一律发 unavailable() 而非当成角度值。ChromeOS EC 用
 * LID_ANGLE_UNRELIABLE（本机实测为 500）表示"角度无法确定"，
 * 不是"360 度"（见 include/motion_lid.h 的文档注释与 motion_lid.c:477-483）。
 * 把它当成 360 会在 EC 提前失效时注入一个假的跳变。
 *
 * 本类只依赖 Qt，不依赖 KWin，便于单独验证。
 */
class HingeSensor : public QObject
{
    Q_OBJECT

public:
    explicit HingeSensor(QObject *parent = nullptr);
    ~HingeSensor() override;

    /// 按 name 查找设备，返回 in_angl_raw 的路径；找不到返回空串
    static QString findDevicePath();

    /// 未找到设备或打开失败返回 false
    bool start(int pollHz);
    void stop();

    /// 运行中改变采样率（空闲/活动自适应）。未启动时只记录，下次 start 生效。
    void setPollHz(int pollHz);

    bool isAvailable() const { return m_available; }

Q_SIGNALS:
    /// 一个可信的角度读数（0..360）
    void angleChanged(int deg);
    /// 这一拍没有可信读数（500 哨兵，或读取失败）
    void unavailable();
    void availabilityChanged(bool available);

private:
    void poll();

    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QTimer> m_timer;
    bool m_available = false;
};

} // namespace HingeGlass
