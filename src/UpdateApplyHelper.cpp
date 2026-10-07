#include "UpdateApplyHelper.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QUuid>

QString windowsPowerShellPath()
{
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:/Windows"));
    const QString candidate = root + QStringLiteral("/System32/WindowsPowerShell/v1.0/powershell.exe");
    return QFileInfo::exists(candidate) ? candidate : QStringLiteral("powershell.exe");
}

UpdateJob writeUpdateJob(const QString &workspace, const QJsonObject &plan)
{
    UpdateJob job;
    if (!QDir().mkpath(workspace)) { job.error = QStringLiteral("update-workspace-create-failed"); return job; }
    job.helperPath = QCoreApplication::applicationDirPath() + QStringLiteral("/UpdateHelper.ps1");
    if (!QFileInfo::exists(job.helperPath)) { job.error = QStringLiteral("update-helper-missing"); return job; }
    job.planPath = workspace + QStringLiteral("/plan.json");
    job.resultPath = workspace + QStringLiteral("/result.json");
    QJsonObject copy = plan;
    copy[QStringLiteral("resultPath")] = job.resultPath;
    QSaveFile file(job.planPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(copy).toJson(QJsonDocument::Compact)) < 0 || !file.commit()) {
        job.error = QStringLiteral("update-plan-write-failed"); return job;
    }
    return job;
}

bool startUpdateJob(const UpdateJob &job, QString *error)
{
    if (!job.isValid()) { if (error) *error = job.error; return false; }
    if (!QProcess::startDetached(windowsPowerShellPath(), {QStringLiteral("-NoProfile"), QStringLiteral("-ExecutionPolicy"), QStringLiteral("Bypass"),
                                                           QStringLiteral("-File"), job.helperPath, QStringLiteral("-PlanPath"), job.planPath})) {
        if (error) *error = QStringLiteral("update-helper-start-failed");
        return false;
    }
    return true;
}
