#pragma once
#include <QObject>
#include <QJsonObject>
#include <QPointer>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QProcess;
class QTemporaryDir;

class UpdateChecker : public QObject
{
    Q_OBJECT
public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;
    void setBaseUrl(const QString &baseUrl);
    void checkForUpdates();
    void downloadAndPrepare(const QJsonObject &meta);
    void cancel();
    bool apply(QString *error = nullptr);
    bool isBusy() const { return m_busy; }

    static int compareVersions(const QString &remote, const QString &current);
    static bool verifySha256(const QString &filePath, const QString &expectedHex);

signals:
    void updateAvailable(const QJsonObject &meta);
    void upToDate();
    void checkFailed(const QString &reason);
    void downloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void preparing();
    void readyToApply(const QString &stagedDir);
    void failed(const QString &reason);
    void cancelled();

private:
    QNetworkReply *get(const QString &url, int timeoutMs);
    void handleCheckReply(QNetworkReply *reply);
    void handleDownloadReply(QNetworkReply *reply, const QJsonObject &meta);
    void prepareArchive();
    void finishFailure(const QString &reason);
    void cleanWorkspace();

    QNetworkAccessManager *m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    QPointer<QProcess> m_helperProcess;
    std::unique_ptr<QTemporaryDir> m_workspace;
    QString m_baseUrl;
    QString m_downloadFile;
    QString m_stagedDir;
    QString m_pendingSha;
    quint64 m_generation = 0;
    bool m_busy = false;
    bool m_ready = false; // readyToApply 后置位：取消无意义，只能 apply
};
