#pragma once
#include <QJsonObject>
#include <QString>

struct UpdateJob
{
    QString helperPath;
    QString planPath;
    QString resultPath;
    QString error;
    bool isValid() const { return error.isEmpty() && !planPath.isEmpty(); }
};

QString windowsPowerShellPath();
UpdateJob writeUpdateJob(const QString &workspace, const QJsonObject &plan);
bool startUpdateJob(const UpdateJob &job, QString *error = nullptr);
