/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "input_event_spy.h"

#include <QObject>

namespace KWin
{
struct SwitchEvent;
}

namespace HingeGlass
{

/**
 * 监听合盖开关事件。
 *
 * KWin 内部已有 LidSwitchTracker，但它是 Workspace 的私有成员
 * （workspace.h:744），第三方特效拿不到，所以这里用同样的公共 API
 * 自己装一个 InputEventSpy。
 *
 * 用硬件开关而不是角度阈值来判断合盖：不依赖标定，且与 KWin 自身
 * 用于重配输出的是同一个信号。删对象时 KWin 会自动卸载 spy。
 */
class LidSensor : public QObject, public KWin::InputEventSpy
{
    Q_OBJECT

public:
    explicit LidSensor(QObject *parent = nullptr);

Q_SIGNALS:
    void lidClosed();
    void lidOpened();

private:
    void switchEvent(KWin::SwitchEvent *event) override;

    bool m_isClosed = false;
};

} // namespace HingeGlass
