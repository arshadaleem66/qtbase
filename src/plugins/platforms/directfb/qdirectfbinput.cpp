/****************************************************************************
**
** Copyright (C) 2011 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qdirectfbinput.h"
#include "qdirectfbconvenience.h"

#include <QThread>
#include <QDebug>
#include <QWindowSystemInterface>
#include <QMouseEvent>
#include <QEvent>

#include <directfb.h>

QDirectFbInput::QDirectFbInput()
    : m_dfbInterface(QDirectFbConvenience::dfbInterface())
    , m_shouldStop(false)
{
    DFBResult ok = m_dfbInterface->CreateEventBuffer(m_dfbInterface, m_eventBuffer.outPtr());
    if (ok != DFB_OK)
        DirectFBError("Failed to initialise eventbuffer", ok);

    m_dfbInterface->GetDisplayLayer(m_dfbInterface, DLID_PRIMARY, m_dfbDisplayLayer.outPtr());
}

void QDirectFbInput::run()
{
    while (!m_shouldStop) {
        if (m_eventBuffer->WaitForEvent(m_eventBuffer.data()) == DFB_OK)
            handleEvents();
    }
}

void QDirectFbInput::stopInputEventLoop()
{
    m_shouldStop = true;
    m_eventBuffer->WakeUp(m_eventBuffer.data());
}

void QDirectFbInput::addWindow(DFBWindowID id, QWindow *qt_window)
{
    m_tlwMap.insert(id,qt_window);
    QDirectFBPointer<IDirectFBWindow> window;
    m_dfbDisplayLayer->GetWindow(m_dfbDisplayLayer.data(), id, window.outPtr());

    window->AttachEventBuffer(window.data(), m_eventBuffer.data());
}

void QDirectFbInput::removeWindow(WId wId)
{
    QDirectFBPointer<IDirectFBWindow> window;
    m_dfbDisplayLayer->GetWindow(m_dfbDisplayLayer.data(), wId, window.outPtr());

    window->DetachEventBuffer(window.data(), m_eventBuffer.data());
    m_tlwMap.remove(wId);
}

void QDirectFbInput::handleEvents()
{
    DFBResult hasEvent = m_eventBuffer->HasEvent(m_eventBuffer.data());
    while(hasEvent == DFB_OK){
        DFBEvent event;
        DFBResult ok = m_eventBuffer->GetEvent(m_eventBuffer.data(), &event);
        if (ok != DFB_OK)
            DirectFBError("Failed to get event",ok);
        if (event.clazz == DFEC_WINDOW) {
            switch (event.window.type) {
            case DWET_BUTTONDOWN:
            case DWET_BUTTONUP:
            case DWET_MOTION:
                handleMouseEvents(event);
                break;
            case DWET_WHEEL:
                handleWheelEvent(event);
                break;
            case DWET_KEYDOWN:
            case DWET_KEYUP:
                handleKeyEvents(event);
                break;
            case DWET_ENTER:
            case DWET_LEAVE:
                handleEnterLeaveEvents(event);
            default:
                break;
            }

        }

        hasEvent = m_eventBuffer->HasEvent(m_eventBuffer.data());
    }
}

void QDirectFbInput::handleMouseEvents(const DFBEvent &event)
{
    QPoint p(event.window.x, event.window.y);
    QPoint globalPos = globalPoint(event);
    Qt::MouseButtons buttons = QDirectFbConvenience::mouseButtons(event.window.buttons);

    QDirectFBPointer<IDirectFBDisplayLayer> layer(QDirectFbConvenience::dfbDisplayLayer());
    QDirectFBPointer<IDirectFBWindow> window;
    layer->GetWindow(layer.data(), event.window.window_id, window.outPtr());

    long timestamp = (event.window.timestamp.tv_sec*1000) + (event.window.timestamp.tv_usec/1000);

    if (event.window.type == DWET_BUTTONDOWN) {
        window->GrabPointer(window.data());
    } else if (event.window.type == DWET_BUTTONUP) {
        window->UngrabPointer(window.data());
    }
    QWindow *tlw = m_tlwMap.value(event.window.window_id);
    QWindowSystemInterface::handleMouseEvent(tlw, timestamp, p, globalPos, buttons);
}

void QDirectFbInput::handleWheelEvent(const DFBEvent &event)
{
    QPoint p(event.window.cx, event.window.cy);
    QPoint globalPos = globalPoint(event);
    long timestamp = (event.window.timestamp.tv_sec*1000) + (event.window.timestamp.tv_usec/1000);
    QWindow *tlw = m_tlwMap.value(event.window.window_id);
    QWindowSystemInterface::handleWheelEvent(tlw, timestamp, p, globalPos,
                                          event.window.step*120,
                                          Qt::Vertical);
}

void QDirectFbInput::handleKeyEvents(const DFBEvent &event)
{
    QEvent::Type type = QDirectFbConvenience::eventType(event.window.type);
    Qt::Key key = QDirectFbConvenience::keyMap()->value(event.window.key_symbol);
    Qt::KeyboardModifiers modifiers = QDirectFbConvenience::keyboardModifiers(event.window.modifiers);

    long timestamp = (event.window.timestamp.tv_sec*1000) + (event.window.timestamp.tv_usec/1000);

    QChar character;
    if (DFB_KEY_TYPE(event.window.key_symbol) == DIKT_UNICODE)
        character = QChar(event.window.key_symbol);
    QWindow *tlw = m_tlwMap.value(event.window.window_id);
    QWindowSystemInterface::handleKeyEvent(tlw, timestamp, type, key, modifiers, character);
}

void QDirectFbInput::handleEnterLeaveEvents(const DFBEvent &event)
{
    QWindow *tlw = m_tlwMap.value(event.window.window_id);
    switch (event.window.type) {
    case DWET_ENTER:
        QWindowSystemInterface::handleEnterEvent(tlw);
        break;
    case DWET_LEAVE:
        QWindowSystemInterface::handleLeaveEvent(tlw);
        break;
    default:
        break;
    }
}

inline QPoint QDirectFbInput::globalPoint(const DFBEvent &event) const
{
    QDirectFBPointer<IDirectFBWindow> window;
    m_dfbDisplayLayer->GetWindow(m_dfbDisplayLayer.data() , event.window.window_id, window.outPtr());
    int x,y;
    window->GetPosition(window.data(), &x, &y);
    return QPoint(event.window.cx +x, event.window.cy + y);
}

