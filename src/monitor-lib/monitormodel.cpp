/****************************************************************************
**
** Copyright (C) 2019 Luxoft Sweden AB
** Copyright (C) 2018 Pelagicore AG
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Luxoft Application Manager.
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

#include "monitormodel.h"

#include <QMetaProperty>
#include <qqmlinfo.h>

#include <QDebug>
#include <QQmlProperty>
#include <QJSValue>
#include <QQmlEngine>

/*!
    \qmltype MonitorModel
    \inqmlmodule QtApplicationManager
    \ingroup common-instantiatable
    \brief A model that can fetch data from various sources and keep a history of their values.

    MonitorModel can fetch data from various sources at regular intervals and keep a history of
    their values. Its main use is having it as a model to plot historical data in a graph for
    monitoring purposes, such as a CPU usage graph.

    The snippet below shows how to use it for plotting a system's CPU load in a simple bar graph:

    \qml
    import QtQuick 2.11
    import QtApplicationManager 2.0

    ListView {
        id: listView
        width: 400
        height: 100
        orientation: ListView.Horizontal
        spacing: (width / model.count) * 0.2
        clip: true
        interactive: false

        model: MonitorModel {
            id: monitorModel
            running: listView.visible
            CpuStatus {}
        }

        delegate: Rectangle {
            width: (listView.width / monitorModel.count) * 0.8
            height: model.cpuLoad * listView.height
            y: listView.height - height
            color: "blue"
        }
    }
    \endqml

    To add a data source to MonitorModel just declare it inside the model, as done in the example above with
    the CpuStatus component. Alternatively (such as from imperative javscript code) you can add data sources
    by assigning them to MonitorModel's dataSources property.

    A data source can be any QtObject with the following characteristics:
    \list
    \li A \c roleNames property: it's a list of strings naming the roles that this data source provides. Those role
        names will be available on each row created by MonitorModel.
    \li Properties matching the names provided in the \c roleNames property: MonitorModel will query their values
        when building each new model row.
    \li An \c update() function: MonitorModel will call it before creating each new model row, so that the data source
        can update the values of its properties.
    \endlist

    The following snippet shows MonitorModel using a custom data source written in QML:

    \qml
    MonitorModel {
        running: true
        QtObject {
            property var roleNames: ["foo", "bar"]

            function update() {
                // foo will have ever increasing values
                foo += 1;

                // bar will keep oscillating between 0 and 10
                if (up) {
                    bar += 1;
                    if (bar == 10)
                        up = false;
                } else {
                    bar -= 1;
                    if (bar == 0)
                        up = true;
                }
            }

            property int foo: 0
            property int bar: 10
            property bool up: false
        }
    }
    \endqml

    Thus, in the MonitorModel above, every row will have two roles: \c foo and \c bar. If plotted, you would see
    an ever incresing foo and an oscillating bar.

    QtApplicationManager comes with a number of components that are readily usable as data sources, namely:
    \list
    \li CpuStatus
    \li FrameTimer
    \li GpuStatus
    \li IoStatus
    \li MemoryStatus
    \li ProcessStatus
    \endlist

    While \l{MonitorModel::running}{running} is true, MonitorModel will probe its data sources every
    \l{MonitorModel::interval}{interval} milliseconds, creating a new row every time up to
    \l{MonitorModel::maximumCount}{maximumCount}. Once that value is reached the oldest row (the first one)
    is discarded whenever a new row comes in, so that \l{MonitorModel::count}{count} doesn't exceed
    \l{MonitorModel::maximumCount}{maximumCount}. New rows are always appended to the model, so rows are
    ordered chronologically from oldest (index 0) to newest (index count-1).
*/

QT_USE_NAMESPACE_AM

MonitorModel::MonitorModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &MonitorModel::readDataSourcesAndAddRow);
}

MonitorModel::~MonitorModel()
{
    qDeleteAll(m_rows);
}

/*!
    \qmlproperty list<Object> MonitorModel::dataSources

    List of data sources to be used by the MonitorModel. A data source can be any QtObject
    containing at least a \c roleNames property and a \c update() function. See MonitorModel's description
    for more information.
*/
QQmlListProperty<QObject> MonitorModel::dataSources()
{
    return QQmlListProperty<QObject>(this, nullptr, &MonitorModel::dataSources_append,
            &MonitorModel::dataSources_count,
            &MonitorModel::dataSources_at,
            &MonitorModel::dataSources_clear);
}

void MonitorModel::dataSources_append(QQmlListProperty<QObject> *property, QObject *dataSource)
{
    auto *that = static_cast<MonitorModel*>(property->object);
    that->appendDataSource(dataSource);
}

int MonitorModel::dataSources_count(QQmlListProperty<QObject> *property)
{
    auto *that = static_cast<MonitorModel*>(property->object);
    return that->m_dataSources.count();
}

QObject *MonitorModel::dataSources_at(QQmlListProperty<QObject> *property, int index)
{
    auto *that = static_cast<MonitorModel*>(property->object);
    return that && that->m_dataSources.count() > index && index >= 0 ? that->m_dataSources.at(index)->obj : nullptr;
}

void MonitorModel::dataSources_clear(QQmlListProperty<QObject> *property)
{
    auto *that = static_cast<MonitorModel*>(property->object);
    that->clearDataSources();
}

void MonitorModel::clearDataSources()
{
    qDeleteAll(m_dataSources);
    m_dataSources.clear();
    m_roleNamesList.clear();
    m_roleNameToIndex.clear();

    clear();
}

void MonitorModel::appendDataSource(QObject *dataSourceObj)
{
    DataSource *dataSource = new DataSource;
    dataSource->obj = dataSourceObj;
    m_dataSources.append(dataSource);

    if (!extractRoleNamesFromJsArray(dataSource)
            && !extractRoleNamesFromStringList(dataSource))
        qmlWarning(this) << "Could not find a roleNames property containing an array or list of strings.";
}

bool MonitorModel::extractRoleNamesFromJsArray(DataSource *dataSource)
{
    QQmlEngine *engine = qmlEngine(this);

    QJSValue jsDataSource = engine->toScriptValue<QObject*>(dataSource->obj);

    if (!jsDataSource.hasProperty(qSL("roleNames")))
        return false;

    QJSValue jsRoleNames = jsDataSource.property(qSL("roleNames"));
    if (!jsRoleNames.isArray())
        return false;

    int length = jsRoleNames.property(qSL("length")).toInt();
    for (int i = 0; i < length; i++)
        addRoleName(jsRoleNames.property(i).toString().toLatin1(), dataSource);

    return true;
}

bool MonitorModel::extractRoleNamesFromStringList(DataSource *dataSource)
{
    const QMetaObject *metaObj = dataSource->obj->metaObject();

    int index = metaObj->indexOfProperty("roleNames");
    if (index == -1)
        return false;

    QMetaProperty property = metaObj->property(index);

    QVariant variant = property.read(dataSource->obj);

    if (!variant.canConvert<QStringList>())
        return false;

    QList<QString> roleNames = variant.toStringList();
    for (int i = 0; i < roleNames.count(); i++)
        addRoleName(roleNames[i].toLatin1(), dataSource);

    return true;
}

void MonitorModel::addRoleName(QByteArray roleName, DataSource *dataSource)
{
    dataSource->roleNames.append(roleName);

    if (m_roleNamesList.contains(roleName))
        qmlWarning(this) << "roleName" << roleName << "already exists. Model won't function correctly.";

    m_roleNamesList.append(dataSource->roleNames.last());
    m_roleNameToIndex[dataSource->roleNames.last()] = m_roleNamesList.count() - 1;
}

/*!
    \qmlproperty int MonitorModel::count
    \readonly

    Number of rows in the model. It ranges from zero up to \l MonitorModel::maximumCount.

    \sa MonitorModel::maximumCount, MonitorModel::clear
*/
int MonitorModel::count() const
{
    return m_rows.count();
}

int MonitorModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        // this model is not a tree
        return 0;
    }

    return count();
}

QVariant MonitorModel::data(const QModelIndex &index, int role) const
{
    if (index.parent().isValid() || !index.isValid() || index.row() < 0 || index.row() >= m_rows.count())
        return QVariant();

    return m_rows.at(index.row())->dataFromRoleIndex[role];
}

QHash<int, QByteArray> MonitorModel::roleNames() const
{
    QHash<int, QByteArray> result;
    for (int i = 0; i < m_roleNamesList.count(); i++) {
        result[i] = m_roleNamesList.at(i);
    }

    return result;
}

/*!
    \qmlproperty bool MonitorModel::running

    While true, MonitorModel will keep probing its data sources and adding new rows every
    \l MonitorModel::interval milliseconds.

    Normally you have this property set to true only while the data is being displayed.

    \sa MonitorModel::interval
*/
bool MonitorModel::running() const
{
    return m_timer.isActive();
}

void MonitorModel::setRunning(bool value)
{
    if (value && !m_timer.isActive()) {
        m_timer.start();
        emit runningChanged();
    } else if (!value && m_timer.isActive()) {
        m_timer.stop();
        emit runningChanged();
    }
}

/*!
    \qmlproperty int MonitorModel::interval

    Interval, in milliseconds, between each row addition while MonitorModel is \l MonitorModel::running.

    \sa MonitorModel::running
*/
int MonitorModel::interval() const
{
    return m_timer.interval();
}

void MonitorModel::setInterval(int value)
{
    if (value != m_timer.interval()) {
        m_timer.setInterval(value);
        emit intervalChanged();
    }
}

void MonitorModel::readDataSourcesAndAddRow()
{
    if (m_dataSources.count() == 0)
        return;

    if (m_rows.count() < m_maximumCount) {
        // create a new row
        DataRow *dataRow = new DataRow;
        fillDataRow(dataRow);
        beginInsertRows(QModelIndex(), /* first */ m_rows.count(), /* last */ m_rows.count());
        m_rows.append(dataRow);
        endInsertRows();
        emit countChanged();
    } else {
        // recycle the oldest row
        beginMoveRows(QModelIndex(), /* sourceFirst */ 0, /* sourceLast */ 0,
                QModelIndex(), /* destination */ m_rows.count());
        m_rows.append(m_rows.takeFirst());
        endMoveRows();

        {
            fillDataRow(m_rows.last());
            QModelIndex modelIndex = index(m_rows.count() - 1 /* row */, 0 /* column */);
            emit dataChanged(modelIndex, modelIndex);
        }
    }
}

void MonitorModel::fillDataRow(DataRow *dataRow)
{
    for (int i = 0; i < m_dataSources.count(); ++i) {
        readDataSource(m_dataSources[i], dataRow);
    }
}

void MonitorModel::readDataSource(DataSource *dataSource, DataRow *dataRow)
{
    // TODO: check if successful
    QMetaObject::invokeMethod(dataSource->obj, "update", Qt::DirectConnection);

    for (int i = 0; i < dataSource->roleNames.count(); i++) {
        // TODO: check index exists
        int roleIndex = m_roleNameToIndex[dataSource->roleNames[i]];

        QVariant variant = QQmlProperty::read(dataSource->obj, QLatin1String(dataSource->roleNames[i]));
        dataRow->dataFromRoleIndex[roleIndex] = variant;
    }
}

/*!
    \qmlproperty int MonitorModel::maximumCount

    The maximum number of rows that the MonitorModel will keep. After this limit is reached the oldest rows
    start to get discarded to make room for the new ones coming in.

    \sa MonitorModel::count, MonitorModel::clear
*/
int MonitorModel::maximumCount() const
{
    return m_maximumCount;
}

void MonitorModel::setMaximumCount(int value)
{
    if (m_maximumCount == value)
        return;

    m_maximumCount = value;
    trimHistory();
    emit maximumCountChanged();
}

void MonitorModel::trimHistory()
{
    int excess = m_rows.count() - m_maximumCount;
    if (excess <= 0)
        return;

    beginRemoveRows(QModelIndex(), /* first */ 0, /* last */ excess - 1);

    while (m_rows.count() > m_maximumCount)
        delete m_rows.takeFirst();

    endRemoveRows();
}

/*!
    \qmlmethod MonitorModel::clear

    Empties the model, removing all exising rows.

    \sa MonitorModel::count
*/
void MonitorModel::clear()
{
    beginResetModel();
    qDeleteAll(m_rows);
    m_rows.clear();
    endResetModel();

    emit countChanged();
}
