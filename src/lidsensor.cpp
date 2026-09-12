/*
    SPDX-FileCopyrightText: 2026 iwinoid

    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "lidsensor.h"

#include "core/inputdevice.h"
#include "input.h"
#include "input_event.h"

namespace HingeGlass
{

LidSensor::LidSensor(QObject *parent)
    : QObject(parent)
{
    KWin::input()->installInputEventSpy(this);
}

void LidSensor::switchEvent(KWin::SwitchEvent *event)
{
    if (!event->device->isLidSwitch()) {
        return;
    }

    const bool closed = (event->state == KWin::SwitchState::On);
    if (closed == m_isClosed) {
        return;
    }
    m_isClosed = closed;

    if (closed) {
        Q_EMIT lidClosed();
    } else {
        Q_EMIT lidOpened();
    }
}

} // namespace HingeGlass
