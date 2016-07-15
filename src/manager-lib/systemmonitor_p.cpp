/****************************************************************************
**
** Copyright (C) 2016 Pelagicore AG
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

#include <qglobal.h>

#include "systemmonitor_p.h"
#include "global.h"


quint64 MemoryReader::s_totalValue = 0;

quint64 MemoryReader::totalValue() const
{
    return s_totalValue;
}


#if defined(Q_OS_LINUX)
#  include <qplatformdefs.h>
#  include <QElapsedTimer>
#  include <QSocketNotifier>

#  include <sys/eventfd.h>
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/ioctl.h>
#  include <errno.h>

#  define EINTR_LOOP(cmd) __extension__ ({int res = 0; do { res = cmd; } while (res == -1 && errno == EINTR); res; })

static inline int qt_safe_open(const char *pathname, int flags, mode_t mode = 0777)
{
    flags |= O_CLOEXEC;
    int fd = EINTR_LOOP(QT_OPEN(pathname, flags, mode));
    // unknown flags are ignored, so we have no way of verifying if
    // O_CLOEXEC was accepted
    if (fd != -1)
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}
#  undef QT_OPEN
#  define QT_OPEN         qt_safe_open

class SysFsReader
{
public:
    SysFsReader(const char *path, int maxRead = 2048)
        : m_path(path)
    {
        m_buffer.resize(maxRead);
        m_fd = QT_OPEN(m_path, QT_OPEN_RDONLY);
    }

    ~SysFsReader()
    {
        if (m_fd >= 0)
            QT_CLOSE(m_fd);
    }

    bool isOpen() const
    {
        return m_fd >= 0;
    }

    QByteArray fileName() const
    {
        return m_path;
    }

    QByteArray readValue() const
    {
        if (m_fd < 0)
            return QByteArray();
        if (EINTR_LOOP(QT_LSEEK(m_fd, 0, SEEK_SET)) != QT_OFF_T(0))
            return QByteArray();

        int read = EINTR_LOOP(QT_READ(m_fd, m_buffer.data(), m_buffer.size()));
        if (read < 0)
            return QByteArray();
        else if (read < m_buffer.size())
            m_buffer[read] = 0;
        return m_buffer;
    }

private:
    int m_fd = -1;
    QByteArray m_path;
    mutable QByteArray m_buffer;

    Q_DISABLE_COPY(SysFsReader)
};


QScopedPointer<SysFsReader> CpuReader::s_sysFs;

CpuReader::CpuReader()
{
    if (!s_sysFs) {
        s_sysFs.reset(new SysFsReader("/proc/stat", 256));
        if (!s_sysFs->isOpen()) {
            qCCritical(LogSystem) << "ERROR: could not read CPU statistics from" << s_sysFs->fileName();
            exit(42);
        }
    }
}

QPair<int, qreal> CpuReader::readLoadValue()
{
    QByteArray str = s_sysFs->readValue();

    int pos = 0;
    qint64 total = 0;
    QVector<qint64> values;

    while (pos < str.size() && values.size() < 4) {
        if (!isdigit(str.at(pos))) {
            ++pos;
            continue;
        }

        char *endPtr = 0;
        qint64 val = strtoll(str.constData() + pos, &endPtr, 10); // check missing for over-/underflow
        values << val;
        total += val;
        pos = endPtr - str.constData() + 1;
    }

    if (values.size() >= 4) {
        qint64 idle = values.at(3);

        m_load = qreal(1) - (qreal(idle - m_lastIdle) / qreal(total - m_lastTotal));

        m_lastIdle = idle;
        m_lastTotal = total;
    } else {
        m_load = qreal(1);
    }
    return qMakePair(m_lastCheck.restart(), m_load);
}


QScopedPointer<SysFsReader> MemoryReader::s_sysFs;

MemoryReader::MemoryReader()
{
    if (!s_sysFs) {
        s_sysFs.reset(new SysFsReader("/sys/fs/cgroup/memory/memory.usage_in_bytes", 256));
        if (!s_sysFs->isOpen()) {
            qCCritical(LogSystem) << "ERROR: could not read memory statistics from" << s_sysFs->fileName();
            exit(42);
        }

        long pageSize = ::sysconf(_SC_PAGESIZE);
        long physPages = ::sysconf(_SC_PHYS_PAGES);

        if (pageSize < 0 || physPages < 0) {
            qCCritical(LogSystem) << "ERROR: Cannot determine the amount of physical RAM in this machine.";
            exit(42);
        }

        s_totalValue = quint64(physPages) * quint64(pageSize);
    }
}

quint64 MemoryReader::readUsedValue() const
{
    return ::strtoull(s_sysFs->readValue().constData(), 0, 10);
}


IoReader::IoReader(const char *device)
    : m_sysFs(new SysFsReader(QByteArray("/sys/block/") + device + "/stat", 256))
{
    if (!m_sysFs->isOpen())
        qCWarning(LogSystem) << "WARNING: could not read I/O statistics from" << m_sysFs->fileName();
}

IoReader::~IoReader()
{ }

QPair<int, qreal> IoReader::readLoadValue()
{
    QByteArray str = m_sysFs->readValue();

    int pos = 0;
    int total = 0;
    QVector<qint64> values;

    while (pos < str.size() && values.size() < 11) {
        if (!isdigit(str.at(pos))) {
            ++pos;
            continue;
        }

        char *endPtr = 0;
        qint64 val = strtoll(str.constData() + pos, &endPtr, 10); // check missing for over-/underflow
        values << val;
        total += val;
        pos = endPtr - str.constData() + 1;
    }

    int elapsed = m_lastCheck.restart();

    if (values.size() >= 11) {
        qint64 ioTime = values.at(9);

        m_load = qreal(ioTime - m_lastIoTime) / elapsed;
        m_lastIoTime = ioTime;
    } else {
        m_load = qreal(1);
    }
    return qMakePair(elapsed, m_load);
}


MemoryThreshold::MemoryThreshold(const QList<qreal> &thresholds)
    : m_thresholds(thresholds)
{ }

MemoryThreshold::~MemoryThreshold()
{
    if (m_usageFd != -1)
        QT_CLOSE(m_usageFd);
    if (m_controlFd != -1)
        QT_CLOSE(m_controlFd);
    if (m_eventFd != -1)
        QT_CLOSE(m_eventFd);
}

QList<qreal> MemoryThreshold::thresholdPercentages() const
{
    return m_thresholds;
}

bool MemoryThreshold::isEnabled() const
{
    return m_enabled;
}

bool MemoryThreshold::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return true;
    if (enabled && !m_initialized) {
        qint64 totalMem = MemoryReader().totalValue();

        m_eventFd = ::eventfd(0, EFD_CLOEXEC);

        if (m_eventFd >= 0) {
            m_usageFd = QT_OPEN("/sys/fs/cgroup/memory/memory.usage_in_bytes", QT_OPEN_RDONLY);

            if (m_usageFd >= 0) {
                m_controlFd = QT_OPEN("/sys/fs/cgroup/memory/cgroup.event_control", QT_OPEN_WRONLY);

                if (m_controlFd >= 0) {
                    bool registerOk = true;

                    foreach (qreal percent, m_thresholds) {
                        qint64 mem = totalMem * percent / 100;
                        registerOk = registerOk && (::dprintf(m_controlFd, "%d %d %lld", m_eventFd, m_usageFd, mem) == 0);
                    }

                    if (registerOk) {
                        m_notifier = new QSocketNotifier(m_eventFd, QSocketNotifier::Read, this);
                        connect(m_notifier, &QSocketNotifier::activated, this, &MemoryThreshold::readEventFd);
                        return m_initialized = m_enabled = true;

                    } else {
                        qWarning() << "Could not register memory limit event handlers";
                    }

                    QT_CLOSE(m_controlFd);
                    m_controlFd = -1;
                } else {
                    qWarning() << "Cannot open /sys/fs/cgroup/memory/cgroup.event_control";
                }

                QT_CLOSE(m_usageFd);
                m_usageFd = -1;
            } else {
                qWarning() << "Cannot open /sys/fs/cgroup/memory/memory.usage_in_bytes";
            }

            QT_CLOSE(m_eventFd);
            m_eventFd = -1;
        } else {
            qWarning() << "Cannot create an eventfd";
        }

        return false;
    } else {
        m_enabled = enabled;
        m_notifier->setEnabled(enabled);

        return true;
    }
}

void MemoryThreshold::readEventFd()
{
    if (m_eventFd >= 0) {
        int handled = 0;
        quint64 counter;

        int r = QT_READ(m_eventFd, &counter, sizeof(counter));
        if (r == sizeof(counter)) {
            handled++;
        } else if (r < 0) {
            if (errno == EWOULDBLOCK)
                return;
             qWarning() << "Error reading from eventFD:" << strerror(errno);
        } else if (r == 0) {
             return;
        } else {
             qWarning() << "Short read" << r << "on eventFD!";
        }

        emit thresholdTriggered();
    }
}

#elif defined(Q_OS_WIN)

#include <windows.h>

CpuReader::CpuReader()
{ }

QPair<int, qreal> CpuReader::readLoadValue()
{
    auto winFileTimeToInt64 = [](const FILETIME &filetime) {
        return ((quint64(filetime.dwHighDateTime) << 32) | quint64(filetime.dwLowDateTime));
    };

    FILETIME winIdle, winKernel, winUser;
    if (GetSystemTimes(&winIdle, &winKernel, &winUser)) {
        qint64 idle = winFileTimeToInt64(winIdle);
        qint64 total = winFileTimeToInt64(winKernel) + winFileTimeToInt64(winUser);

        m_load = qreal(1) - (qreal(idle - m_lastIdle) / qreal(total - m_lastTotal));

        m_lastIdle = idle;
        m_lastTotal = total;
    } else {
        m_load = qreal(1);
    }
    return qMakePair(m_lastCheck.restart(), m_load);
}

MemoryReader::MemoryReader()
{
    if (!s_totalValue) {
        MEMORYSTATUSEX mem { sizeof(MEMORYSTATUSEX) };
        if (!GlobalMemoryStatusEx(&mem)) {
            qCCritical(LogSystem) << "Cannot determine the amount of physical RAM in this machine.";
            exit(42);
        }
        s_totalValue = mem.ullTotalPhys;
    }
}

quint64 MemoryReader::readUsedValue() const
{
    MEMORYSTATUSEX mem { sizeof(MEMORYSTATUSEX) };
    if (!GlobalMemoryStatusEx(&mem))
        return 0;
    return mem.ullTotalPhys - mem.ullAvailPhys;
}

#elif defined(Q_OS_OSX)

#include <mach/mach.h>
#include <sys/sysctl.h>

CpuReader::CpuReader()
{ }

QPair<int, qreal> CpuReader::readLoadValue()
{
    natural_t cpuCount = 0;
    processor_cpu_load_info_t cpuLoadInfo;
    mach_msg_type_number_t cpuLoadInfoCount = 0;

    if (host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO, &cpuCount,
                            (processor_info_array_t *) &cpuLoadInfo, &cpuLoadInfoCount) == KERN_SUCCESS) {
        uint64_t idle = 0, total = 0;

        for (natural_t i = 0; i < cpuCount; ++i) {
            idle += cpuLoadInfo[i].cpu_ticks[CPU_STATE_IDLE];
            total += cpuLoadInfo[i].cpu_ticks[CPU_STATE_USER] \
                    + cpuLoadInfo[i].cpu_ticks[CPU_STATE_SYSTEM] \
                    + cpuLoadInfo[i].cpu_ticks[CPU_STATE_IDLE] \
                    + cpuLoadInfo[i].cpu_ticks[CPU_STATE_NICE];
        }
        vm_deallocate(mach_task_self(), (vm_address_t) cpuLoadInfo, cpuLoadInfoCount);

        m_load = qreal(1) - (qreal(idle - m_lastIdle) / qreal(total - m_lastTotal));

        m_lastIdle = idle;
        m_lastTotal = total;
    } else {
        m_load = qreal(1);
    }
    return qMakePair(m_lastCheck.restart(), m_load);
}


int MemoryReader::s_pageSize = 0;

MemoryReader::MemoryReader()
{
    if (!s_totalValue) {
        int mib[2] = { CTL_HW, HW_MEMSIZE };
        int64_t hwMem;
        size_t hwMemSize = sizeof(hwMem);

        if (sysctl(mib, sizeof(mib) / sizeof(*mib), &hwMem, &hwMemSize, nullptr, 0) == KERN_SUCCESS)
            s_totalValue = hwMem;

        mib[1] = HW_PAGESIZE;
        int hwPageSize;
        size_t hwPageSizeSize = sizeof(hwPageSize);

        if (sysctl(mib, sizeof(mib) / sizeof(*mib), &hwPageSize, &hwPageSizeSize, nullptr, 0) == KERN_SUCCESS)
            s_pageSize = hwPageSize;
    }
}

quint64 MemoryReader::readUsedValue() const
{
    vm_statistics64_data_t vmStat;
    mach_msg_type_number_t vmStatCount = HOST_VM_INFO64_COUNT;

    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t) &vmStat, &vmStatCount) == KERN_SUCCESS) {
        quint64 app = vmStat.internal_page_count;
        quint64 compressed = vmStat.compressor_page_count;
        quint64 wired = vmStat.wire_count;

        return (app + compressed + wired) * s_pageSize;
    } else {
        return 0;
    }
}

#else // Q_OS_...

CpuReader::CpuReader()
{ }

QPair<int, qreal> CpuReader::readLoadValue()
{
    return qMakePair(0, 1);
}

MemoryReader::MemoryReader()
{ }

quint64 MemoryReader::readUsedValue() const
{
    return 0;
}

#endif  // defined(Q_OS_...)

#if !defined(Q_OS_LINUX)

IoReader::IoReader(const char *device)
{
    Q_UNUSED(device)
}

IoReader::~IoReader()
{ }

QPair<int, qreal> IoReader::readLoadValue()
{
    return qMakePair(0, 1);
}

MemoryThreshold::MemoryThreshold(const QList<qreal> &thresholds)
{
    Q_UNUSED(thresholds)
}

MemoryThreshold::~MemoryThreshold()
{ }

QList<qreal> MemoryThreshold::thresholdPercentages() const
{
    return QList<qreal>();
}

bool MemoryThreshold::isEnabled() const
{
    return false;
}

bool MemoryThreshold::setEnabled(bool enabled)
{
    Q_UNUSED(enabled)
    return false;
}

#endif // !defined(Q_OS_LINUX)