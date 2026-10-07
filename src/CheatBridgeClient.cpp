#include "CheatBridgeClient.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkRequest>
#include <QUrl>
#include <QUuid>

CheatBridgeClient::CheatBridgeClient(QObject *parent) : QObject(parent) {}

void CheatBridgeClient::openSession(const GameSession &session)
{
    cancelSession();
    m_session = session;
    m_connected = !session.sessionId.isEmpty() && session.pid > 0 && session.port != 0
        && !session.token.isEmpty() && (session.runtime == "mono" || session.runtime == "il2cpp");
    emit sessionChanged();
}

bool CheatBridgeClient::openHandshake(const QString &path, qint64 expectedPid, const QString &expectedRuntime, const QString &token)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const auto object = document.object();
    const auto sessionId = object["sessionId"].toString();
    const auto pid = object["pid"].toInteger();
    const auto runtime = object["runtime"].toString();
    const auto port = object["port"].toInteger();
    if (object["protocolVersion"] != "1" || sessionId.isEmpty() || pid != expectedPid
        || runtime != expectedRuntime || port <= 0 || port > 65535) return false;
    openSession({sessionId, pid, runtime, static_cast<quint16>(port), token});
    return isConnected();
}

void CheatBridgeClient::sendControl(const GameSession &session, const QString &path, const QJsonObject &arguments)
{
    if (session.sessionId.isEmpty() || session.token.isEmpty() || session.port == 0) return;
    QNetworkRequest request(QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(session.port).arg(path)));
    request.setTransferTimeout(1500);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    request.setRawHeader("X-UnityTools-Session", session.sessionId.toUtf8());
    request.setRawHeader("X-UnityTools-Token", session.token.toUtf8());
    request.setRawHeader("X-UnityTools-Request", QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8());
    request.setRawHeader("X-UnityTools-Deadline", QByteArray::number(QDateTime::currentMSecsSinceEpoch() + 1500));
    auto *reply = m_network.post(request, QJsonDocument(arguments).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
}

void CheatBridgeClient::releaseResources(const QStringList &objects, const QStringList &snapshots)
{
    if (!m_connected || (objects.isEmpty() && snapshots.isEmpty())) return;
    sendControl(m_session, "/objects/release", {{"objectIds", QJsonArray::fromStringList(objects)},
        {"snapshotIds", QJsonArray::fromStringList(snapshots)}});
}

void CheatBridgeClient::acknowledge(const QString &requestId)
{
    if (m_connected && !requestId.isEmpty()) sendControl(m_session, "/requests/ack", {{"requestId", requestId}});
}

void CheatBridgeClient::cancelSession()
{
    ++m_sessionGeneration;
    const auto oldSession = m_session;
    const auto oldRequests = m_requests.keys();
    for (const auto &requestId : oldRequests) sendControl(oldSession, "/requests/cancel", {{"requestId", requestId}});
    if (m_connected) sendControl(oldSession, "/objects/release", {{"all", true}});
    m_connected = false;
    const auto replies = m_requests.values();
    m_requests.clear();
    m_cancelledRequests.clear();
    for (auto *reply : replies) if (reply) reply->abort();
    m_session = {};
    emit sessionChanged();
}

bool CheatBridgeClient::acceptResponse(const CheatResponse &response) const
{
    return m_connected && response.sessionId == m_session.sessionId && response.pid == m_session.pid
        && response.runtime == m_session.runtime;
}

bool CheatBridgeClient::acceptEnvelope(const QJsonObject &envelope, const QString &requestId) const
{
    return acceptResponse({envelope["sessionId"].toString(), envelope["pid"].toInteger(), envelope["runtime"].toString()})
        && envelope["requestId"].toString() == requestId && !envelope["state"].toString().isEmpty()
        && (envelope["result"].isObject() || envelope.contains("error"));
}

QString CheatBridgeClient::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_session.port);
}

QString CheatBridgeClient::request(const QString &path, const QByteArray &method, const QJsonObject &arguments)
{
    if (!m_connected) return {};
    const QString requestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QNetworkRequest request(QUrl(baseUrl() + path));
    request.setTransferTimeout(15000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
    request.setRawHeader("X-UnityTools-Session", m_session.sessionId.toUtf8());
    request.setRawHeader("X-UnityTools-Token", m_session.token.toUtf8());
    request.setRawHeader("X-UnityTools-Request", requestId.toUtf8());
    request.setRawHeader("X-UnityTools-Deadline", QByteArray::number(QDateTime::currentMSecsSinceEpoch() + 15000));
    auto *reply = method == "GET" ? m_network.get(request)
        : m_network.sendCustomRequest(request, method, QJsonDocument(arguments).toJson(QJsonDocument::Compact));
    m_requests.insert(requestId, reply);
    const auto session = m_session;
    const quint64 generation = m_sessionGeneration;
    connect(reply, &QNetworkReply::finished, this, [this, reply, requestId, path, session, generation] {
        m_requests.remove(requestId);
        if (generation != m_sessionGeneration || !m_connected || m_cancelledRequests.remove(requestId)) {
            reply->deleteLater();
            return;
        }
        const auto doc = QJsonDocument::fromJson(reply->isOpen() ? reply->readAll() : QByteArray());
        const auto envelope = doc.object();
        if (doc.isObject() && acceptEnvelope(envelope, requestId)) {
            emit completed(requestId, path, envelope);
            const auto state = envelope["state"].toString();
            if (state != "unknown" && state != "executed-unverified" && state != "executing" && state != "queued")
                sendControl(session, "/requests/ack", {{"requestId", requestId}});
        } else if (reply->error() != QNetworkReply::NoError)
            emit failed(requestId, path, reply->errorString());
        else
            emit failed(requestId, path, QStringLiteral("invalid bridge session or response envelope"));
        reply->deleteLater();
    });
    return requestId;
}

void CheatBridgeClient::cancelRequest(const QString &requestId)
{
    if (!m_requests.contains(requestId)) return;
    m_cancelledRequests.insert(requestId);
    sendControl(m_session, "/requests/cancel", {{"requestId", requestId}});
    if (auto *reply = m_requests.value(requestId)) reply->abort();
}
