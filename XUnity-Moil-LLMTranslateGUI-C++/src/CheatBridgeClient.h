#pragma once
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QHash>
#include <QSet>

struct GameSession
{
    QString sessionId;
    qint64 pid = 0;
    QString runtime;
    quint16 port = 0;
    QString token;
};

struct CheatResponse
{
    QString sessionId;
    qint64 pid = 0;
    QString runtime;
    QString requestId;
    QString state;
    QJsonObject result;
};

class CheatBridgeClient : public QObject
{
    Q_OBJECT
public:
    explicit CheatBridgeClient(QObject *parent = nullptr);

    void openSession(const GameSession &session);
    bool openHandshake(const QString &path, qint64 expectedPid, const QString &expectedRuntime, const QString &token);
    void cancelSession();
    const GameSession &session() const { return m_session; }
    bool isConnected() const { return m_connected; }
    bool acceptResponse(const CheatResponse &response) const;
    QString request(const QString &path, const QByteArray &method, const QJsonObject &arguments = {});
    void cancelRequest(const QString &requestId);
    void releaseResources(const QStringList &objects, const QStringList &snapshots);
    void acknowledge(const QString &requestId);

signals:
    void sessionChanged();
    void completed(const QString &requestId, const QString &path, const QJsonObject &envelope);
    void failed(const QString &requestId, const QString &path, const QString &error);

private:
    void sendControl(const GameSession &session, const QString &path, const QJsonObject &arguments);
    bool acceptEnvelope(const QJsonObject &envelope, const QString &requestId) const;
    QString baseUrl() const;
    GameSession m_session;
    bool m_connected = false;
    QNetworkAccessManager m_network;
    QHash<QString, QNetworkReply *> m_requests;
    QSet<QString> m_cancelledRequests;
    quint64 m_sessionGeneration = 0;
};
