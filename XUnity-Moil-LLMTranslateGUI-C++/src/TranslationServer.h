#pragma once

#include <QObject>
#include <QThread>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <deque>
#include <mutex>
#include <map>
#include <atomic>
#include <thread>
#include "ConfigManager.h"
#include "httplib.h"

struct Context
{
    std::deque<std::pair<QString, QString>> history;
    int max_len;
};

class TranslationServer : public QObject
{
    Q_OBJECT

public:
    void injectLog(const QString &msg)
    {
        emit logMessage(msg);
    }

    explicit TranslationServer(QObject *parent = nullptr);
    ~TranslationServer();

    void updateConfig(const AppConfig &config);
    AppConfig getConfig();

    void startServer();
    void stopServer();

    void clearAllContexts();
    bool isRunning() const { return m_running; }
    bool isStopping() const { return m_isStopping; }

signals:
    void logMessage(QString msg);
   
    // 一次 API 响应中，由供应商明确报告的 Token 数据。
    void tokenUsageReceived(long long prompt,long long completion,long long total);

    // API 成功返回，但响应中没有可识别的 usage 字段。
    void tokenUsageUnavailable();

    void workStarted();
    void workFinished(bool success);
    void serverStarted();
    void serverStopped();

private:
    void configureServer(httplib::Server &server, int threads);
    QString performTranslation(const QString &text, const QString &clientIP, int batchCount = 0, quint64 requestId = 0);
    QString getNextApiKey();
    QString generateClientId(const std::string &ip);

    QString performSingleTranslationAttempt(const QString &text, const QString &clientIP, int batchCount, quint64 requestId, int attempt);
    bool isValidTranslationResult(const QString &result);
    void recordDialoguePair(const QString &src, const QString &dst);
    QStringList drainDialoguePairs();
    QString freezeEscapesLocal(const QString &input, struct EscapeMap &context);
    QString thawEscapesLocal(const QString &input, const struct EscapeMap &context);

    // Unicode 标点/符号透传；数字不是标点
    bool isPunctuationOnly(const QString &text);

    // Unity富文本转Qt HTML
    QString unityToHtml(const QString &text);

    // 彩虹文字生成器 (预留)
    QString makeRainbow(const QString &text);

private:
    AppConfig m_config;
    std::atomic<bool> m_running;
    std::atomic<bool> m_stopRequested;
    std::atomic<bool> m_isStopping;
    std::atomic<bool> m_starting{false};

    std::thread *m_serverThread = nullptr;
    std::thread *m_cleanupThread = nullptr;

    httplib::Server *m_svr = nullptr;

    std::map<std::string, Context> m_contexts;
    std::mutex m_contextMutex;

    std::vector<QString> m_apiKeys;
    int m_currentKeyIndex = 0;
    std::mutex m_keyMutex;

    std::mutex m_configMutex;
    QList<QPair<QString, QString>> m_dialogueLog; // 本轮 原文/译文 对（自进化取材，cap 500）
    std::mutex m_dialogueMutex;
    std::atomic<quint64> m_requestSequence{0};
    std::atomic<int> m_inFlight{0};
    void trace(quint64 id, int attempt, const QString &message);
};