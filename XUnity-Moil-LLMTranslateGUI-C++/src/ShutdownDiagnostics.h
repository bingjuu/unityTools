#pragma once
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QThread>

// A separate file remains useful when queued UI log delivery is itself blocked.
inline void shutdownDiagnostic(const QString &message)
{
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    QFile file(QCoreApplication::applicationDirPath() + "/shutdown-diagnostics.log");
    if (file.open(QIODevice::WriteOnly | QIODevice::Append)) {
        file.write(QString("%1 thread=%2 %3\n")
            .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"))
            .arg(quintptr(QThread::currentThreadId()), 0, 16).arg(message).toUtf8());
    }
}
