/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "hingesensor.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTimer>

namespace HingeGlass
{

namespace
{
/// EC 用 500 表示"角度无法确定"，留出余量按 >= 400 判不可信
constexpr int kUnreliableThreshold = 400;
constexpr auto kDeviceName = "cros-ec-lid-angle";
constexpr auto kAttribute = "/in_angl_raw";
} // namespace

HingeSensor::HingeSensor(QObject *parent)
    : QObject(parent)
{
}

HingeSensor::~HingeSensor() = default;

QString HingeSensor::findDevicePath()
{
    const QDir iio(QStringLiteral("/sys/bus/iio/devices"));
    const QStringList entries = iio.entryList({QStringLiteral("iio:device*")}, QDir::Dirs);
    for (const QString &entry : entries) {
        const QString base = iio.filePath(entry);

        QFile nameFile(base + QStringLiteral("/name"));
        if (!nameFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        if (nameFile.readAll().trimmed() != QByteArray(kDeviceName)) {
            continue;
        }

        const QString anglePath = base + QLatin1StringView(kAttribute);
        if (QFile::exists(anglePath)) {
            return anglePath;
        }
    }
    return QString();
}

bool HingeSensor::start(int pollHz)
{
    const QString path = findDevicePath();
    if (path.isEmpty()) {
        qWarning() << "HingeGlass: 找不到 cros-ec-lid-angle 设备，铰链特效不会被激活";
        return false;
    }

    m_file = std::make_unique<QFile>(path);
    if (!m_file->open(QIODevice::ReadOnly)) {
        qWarning() << "HingeGlass: 无法打开" << path;
        m_file.reset();
        return false;
    }

    m_timer = std::make_unique<QTimer>(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(qMax(1, 1000 / qMax(1, pollHz)));
    connect(m_timer.get(), &QTimer::timeout, this, &HingeSensor::poll);
    m_timer->start();

    if (!m_available) {
        m_available = true;
        Q_EMIT availabilityChanged(true);
    }
    return true;
}

void HingeSensor::setPollHz(int pollHz)
{
    if (!m_timer) {
        return;
    }
    m_timer->setInterval(qMax(1, 1000 / qMax(1, pollHz)));
}

void HingeSensor::stop()
{
    if (m_timer) {
        m_timer->stop();
        m_timer.reset();
    }
    m_file.reset();

    if (m_available) {
        m_available = false;
        Q_EMIT availabilityChanged(false);
    }
}

void HingeSensor::poll()
{
    if (!m_file) {
        return;
    }

    // sysfs 属性每次都要 seek 回起点才会重新求值
    if (!m_file->seek(0)) {
        Q_EMIT unavailable();
        return;
    }

    bool ok = false;
    const int value = m_file->readLine().trimmed().toInt(&ok);
    if (!ok) {
        Q_EMIT unavailable();
        return;
    }

    if (value >= kUnreliableThreshold) {
        // "角度无法确定"，不是 360 度。交给状态机当作"这一拍没数据"。
        Q_EMIT unavailable();
        return;
    }

    Q_EMIT angleChanged(value);
}

} // namespace HingeGlass
