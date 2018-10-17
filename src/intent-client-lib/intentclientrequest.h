/****************************************************************************
**
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Pelagicore Application Manager.
**
** $QT_BEGIN_LICENSE:LGPL-QTAS$
** Commercial License Usage
** Licensees holding valid commercial Qt Automotive Suite licenses may use
** this file in accordance with the commercial license agreement provided
** with the Software or, alternatively, in accordance with the terms
** contained in a written agreement between you and The Qt Company.  For
** licensing terms and conditions see https://www.qt.io/terms-conditions.
** For further information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
** SPDX-License-Identifier: LGPL-3.0
**
****************************************************************************/

#pragma once

#include <QObject>
#include <QDir>
#include <QUuid>
#include <QString>
#include <QVariantMap>
#include <QtAppManCommon/global.h>

QT_BEGIN_NAMESPACE_AM

class IntentClient;

class IntentClientRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid requestId READ requestId NOTIFY requestIdChanged)
    Q_PROPERTY(Direction direction READ direction CONSTANT)
    Q_PROPERTY(QString intentId READ intentId CONSTANT)
    Q_PROPERTY(QString applicationId READ applicationId CONSTANT)
    Q_PROPERTY(QVariantMap parameters READ parameters CONSTANT)
    Q_PROPERTY(bool succeeded READ succeeded NOTIFY finished)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY finished)
    Q_PROPERTY(QVariantMap result READ result NOTIFY finished)

public:
    enum class Direction { ToSystem, ToApplication };
    Q_ENUM(Direction)

    static IntentClientRequest *create(const QString &requestingApplicationId, const QString &intentId, const QVariantMap &parameters);
    static IntentClientRequest *create(const QString &requestingApplicationId, const QString &intentId, const QString &applicationId, const QVariantMap &parameters);

    QUuid requestId() const;
    Direction direction() const;
    QString intentId() const;
    QString applicationId() const;
    QString requestingApplicationId() const;
    QVariantMap parameters() const;

    const QVariantMap result() const;
    bool succeeded() const;
    QString errorMessage() const;

    Q_INVOKABLE void sendReply(const QVariantMap &result);
    Q_INVOKABLE void sendErrorReply(const QString &errorMessage);

signals:
    void requestIdChanged();
    void finished();

private:
    IntentClientRequest(Direction direction, const QString &requestingApplicationId, const QUuid &id,
                        const QString &intentId, const QString &applicationId, const QVariantMap &parameters);

    void setRequestId(const QUuid &requestId);
    void setResult(const QVariantMap &result);
    void setErrorMessage(const QString &errorMessage);

    Direction m_direction;
    QUuid m_id;
    QString m_intentId;
    QString m_requestingApplicationId;
    QString m_applicationId;
    QVariantMap m_parameters;
    QVariantMap m_result;
    QString m_errorMessage;
    bool m_requestSent = false;
    bool m_replyReceived = false;
    bool m_succeeded = false;

    Q_DISABLE_COPY(IntentClientRequest)

    friend class IntentClient;
};

QT_END_NAMESPACE_AM

Q_DECLARE_METATYPE(QtAM::IntentClientRequest *)