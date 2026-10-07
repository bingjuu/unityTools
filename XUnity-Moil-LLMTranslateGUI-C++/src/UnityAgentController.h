#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QHash>
#include <QSet>
#include "CheatBridgeClient.h"

class UnityAgentController : public QObject
{
    Q_OBJECT
public:
    explicit UnityAgentController(QObject *parent = nullptr);
    void configure(const QString &address, const QString &key, const QString &model);
    void bindClient(CheatBridgeClient *client, const QJsonObject &capabilities);
    void setCapabilities(const QJsonObject &capabilities);
    QString start(const QString &request, const GameSession &session);
    void cancel();
    void cancel(const QString &requestId);
    void chooseCandidate(const QString &objectId);
    QString state(const QString &requestId) const;
    QString currentState() const { return m_state; }
    QJsonObject evidence() const { return m_evidence; }
    QStringList allowedTools(const QJsonObject &capabilities) const;

signals:
    void finished();
    void selectionRequired(const QList<QJsonObject> &candidates);
    void stateChanged(const QString &state);
    void message(const QString &text);
    void assistantResponse(const QString &text);
    void evidenceChanged(const QJsonObject &evidence);

private:
    void askModel();
    void nextTool();
    void completeTool(const QJsonObject &result);
    void sendBridge(const QString &path, const QJsonObject &args, const QString &stage);
    void handleBridge(const QString &id, const QString &path, const QJsonObject &envelope);
    void fail(const QString &reason, const QString &state = "failed");
    void setState(const QString &state);
    bool sameSession() const;
    bool active() const;
    QJsonArray toolDefinitions() const;
    QString modelUrl() const;
    void releaseTaskResources();
    void rememberReferences(const QJsonValue &value);
    QJsonObject readArgs() const;

    QPointer<CheatBridgeClient> m_client;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_modelReply;
    QString m_address, m_key, m_model;
    GameSession m_session;
    QString m_taskId;
    QString m_state;
    quint64 m_generation = 0;
    QJsonObject m_capabilities;
    QJsonArray m_messages;
    QJsonArray m_calls;
    int m_callIndex = 0;
    QJsonObject m_call;
    QString m_bridgeId, m_stage;
    QJsonObject m_arguments;
    QJsonObject m_writeArguments;
    QJsonObject m_evidence;
    QSet<QString> m_observedObjects;
    QHash<QString, QJsonObject> m_inspections;
    QList<QJsonObject> m_candidates;
    QString m_selectedObject;
    QString m_selectionKind;
    QHash<QString, QString> m_selectedEntries;
    QSet<QString> m_snapshots;
    QString m_uncertainRequest;
};
