#include "UpdateChecker.h"
#include "UpdateApplyHelper.h"
#include "AppVersion.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QTemporaryDir>

namespace {
bool validMetadata(const QJsonObject &meta)
{
    static const QRegularExpression asset(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]*\\.zip$"));
    static const QRegularExpression digest(QStringLiteral("^[A-Fa-f0-9]{64}$"));
    return UpdateChecker::compareVersions(meta.value("version").toString(), "0.0.0") != -2
        && asset.match(meta.value("asset").toString()).hasMatch()
        && digest.match(meta.value("sha256").toString()).hasMatch()
        && meta.value("notes").isString();
}
}

UpdateChecker::UpdateChecker(QObject *parent) : QObject(parent), m_baseUrl("https://github.com/bingjuu/unityTools") {}
UpdateChecker::~UpdateChecker()
{
    ++m_generation;
    if (m_reply) { disconnect(m_reply, nullptr, this, nullptr); m_reply->abort(); }
    if (m_helperProcess) { disconnect(m_helperProcess, nullptr, this, nullptr); m_helperProcess->kill(); m_helperProcess->waitForFinished(3000); }
    cleanWorkspace();
}
void UpdateChecker::setBaseUrl(const QString &baseUrl) { if (!m_busy) m_baseUrl = baseUrl; }
int UpdateChecker::compareVersions(const QString &remote, const QString &current)
{
    static const QRegularExpression format(QStringLiteral("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$"));
    if (!format.match(remote).hasMatch() || !format.match(current).hasMatch()) return -2;
    const auto left = remote.split('.'), right = current.split('.');
    for (int i = 0; i < 3; ++i) {
        if (left[i].size() != right[i].size()) return left[i].size() > right[i].size() ? 1 : -1;
        const int order = left[i].compare(right[i]);
        if (order) return order > 0 ? 1 : -1;
    }
    return 0;
}
bool UpdateChecker::verifySha256(const QString &filePath, const QString &expectedHex)
{
    QFile file(filePath); if (!file.open(QIODevice::ReadOnly)) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && QString::fromLatin1(hash.result().toHex()).compare(expectedHex, Qt::CaseInsensitive) == 0;
}
QNetworkReply *UpdateChecker::get(const QString &url, int timeoutMs)
{
    if (!m_network) m_network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(timeoutMs);
    request.setRawHeader("User-Agent", "UnityTools/" + QByteArray(kAppVersion));
    return m_network->get(request);
}
void UpdateChecker::checkForUpdates()
{
    if (m_busy) return;
    m_busy = true;
    auto *reply = get(m_baseUrl + "/releases/latest/download/latest.json", 15000); m_reply = reply;
    const auto generation = ++m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != m_generation) { reply->deleteLater(); return; }
        handleCheckReply(reply);
    });
}
void UpdateChecker::handleCheckReply(QNetworkReply *reply)
{
    const auto bytes = reply->readAll(); const auto error = reply->error(); const auto reason = reply->errorString();
    reply->deleteLater(); m_reply = nullptr; m_busy = false;
    if (error != QNetworkReply::NoError) { emit checkFailed(reason); return; }
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(bytes, &parse); const auto meta = doc.object();
    if (parse.error != QJsonParseError::NoError || !doc.isObject() || !validMetadata(meta)) { emit checkFailed("invalid-update-metadata"); return; }
    if (compareVersions(meta.value("version").toString(), QString::fromLatin1(kAppVersion)) > 0) emit updateAvailable(meta);
    else emit upToDate();
}
void UpdateChecker::cleanWorkspace() { m_workspace.reset(); m_downloadFile.clear(); m_stagedDir.clear(); }
void UpdateChecker::finishFailure(const QString &reason) { m_busy = false; m_reply = nullptr; cleanWorkspace(); emit failed(reason); }
void UpdateChecker::downloadAndPrepare(const QJsonObject &meta)
{
    if (m_busy) return;
    if (!validMetadata(meta)) { emit failed("invalid-update-metadata"); return; }
    cleanWorkspace(); m_workspace = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/UnityTools-update-XXXXXX");
    if (!m_workspace->isValid()) { finishFailure("update-workspace-create-failed"); return; }
    m_downloadFile = m_workspace->filePath("update.zip");
    m_pendingSha = meta.value("sha256").toString(); m_busy = true;
    auto *reply = get(m_baseUrl + "/releases/download/v" + meta.value("version").toString() + "/" + meta.value("asset").toString(), 180000); m_reply = reply;
    const auto generation = ++m_generation;
    auto *file = new QFile(m_downloadFile, reply);
    if (!file->open(QIODevice::WriteOnly)) { reply->abort(); reply->deleteLater(); finishFailure("update-download-open-failed"); return; }
    auto drain = [reply, file] {
        const auto bytes = reply->readAll();
        if (file->write(bytes) != bytes.size()) { reply->setProperty("writeFailed", true); reply->abort(); }
    };
    connect(reply, &QNetworkReply::readyRead, this, [this, generation, drain] { if (generation == m_generation) drain(); });
    connect(reply, &QNetworkReply::downloadProgress, this, &UpdateChecker::downloadProgress);
    connect(reply, &QNetworkReply::finished, this, [this, reply, file, generation, meta, drain] {
        if (generation != m_generation) { file->close(); reply->deleteLater(); return; }
        drain();
        if (!file->flush()) reply->setProperty("writeFailed", true);
        file->close();
        handleDownloadReply(reply, meta);
    });
}
void UpdateChecker::handleDownloadReply(QNetworkReply *reply, const QJsonObject &)
{
    const auto error = reply->error(); const auto reason = reply->errorString(); const bool writeFailed = reply->property("writeFailed").toBool();
    reply->deleteLater(); m_reply = nullptr;
    if (writeFailed) { finishFailure("update-download-write-failed"); return; }
    if (error != QNetworkReply::NoError) { finishFailure(reason); return; }
    if (!verifySha256(m_downloadFile, m_pendingSha)) { finishFailure("download-sha256-mismatch"); return; }
    prepareArchive();
}
void UpdateChecker::prepareArchive()
{
    emit preparing();
    const auto job = writeUpdateJob(m_workspace->path(), {{"mode", "prepare"}, {"archivePath", m_downloadFile}, {"extractPath", m_workspace->filePath("extracted")}});
    if (!job.isValid()) { finishFailure(job.error); return; }
    auto *process = new QProcess(this); m_helperProcess = process;
    const auto generation = m_generation;
    connect(process, &QProcess::errorOccurred, this, [this, generation](QProcess::ProcessError error) { if (generation == m_generation && error == QProcess::FailedToStart) finishFailure("update-helper-start-failed"); });
    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this, process, generation, job](int exitCode, QProcess::ExitStatus status) {
        process->deleteLater(); if (m_helperProcess == process) m_helperProcess = nullptr;
        if (generation != m_generation || !m_busy) return;
        QFile file(job.resultPath); const auto result = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject{};
        if (status != QProcess::NormalExit || exitCode != 0 || result.value("state") != "prepared") { finishFailure(result.value("error").toString("extract-failed")); return; }
        m_stagedDir = result.value("stagedDir").toString(); m_busy = false; emit readyToApply(m_stagedDir);
    });
    process->start(windowsPowerShellPath(), {"-NoProfile", "-ExecutionPolicy", "Bypass", "-File", job.helperPath, "-PlanPath", job.planPath});
}
void UpdateChecker::cancel()
{
    if (!m_busy && !m_workspace) return;
    ++m_generation;
    if (m_reply) { auto reply = m_reply; m_reply = nullptr; reply->abort(); }
    if (m_helperProcess) { auto process = m_helperProcess; m_helperProcess = nullptr; process->kill(); process->waitForFinished(3000); }
    m_busy = false; cleanWorkspace(); emit cancelled();
}
bool UpdateChecker::apply(QString *error)
{
    if (m_busy || !m_workspace || m_stagedDir.isEmpty()) { if (error) *error = "update-not-prepared"; return false; }
    const auto helper = m_workspace->filePath("UpdateHelper.ps1");
    if (!QFile::copy(QCoreApplication::applicationDirPath() + "/UpdateHelper.ps1", helper)) { if (error) *error = "update-helper-copy-failed"; return false; }
    auto job = writeUpdateJob(m_workspace->path(), {{"mode", "apply"}, {"stagedDir", m_stagedDir}, {"installDir", QCoreApplication::applicationDirPath()},
        {"parentPid", QCoreApplication::applicationPid()}, {"parentExePath", QCoreApplication::applicationFilePath()}, {"workingDir", QDir::currentPath()}});
    job.helperPath = helper;
    if (!startUpdateJob(job, error)) return false;
    m_workspace->setAutoRemove(false); return true;
}
