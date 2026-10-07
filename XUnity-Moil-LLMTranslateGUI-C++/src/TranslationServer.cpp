#include "TranslationServer.h"
#include "ShutdownDiagnostics.h"
#include "json.hpp"
#include "GlossaryManager.h"
#include "GlossarySeeds.h"
#include "RegexManager.h"
#include "LogManager.h"
#include "XuaConfigHijacker.h"
#include <QEventLoop>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSet>
#include <regex>
#include <chrono>
#include <thread>
#include <QDateTime>
#include <algorithm>
#include <memory> // 🔥 引入现代C++智能指针
#include <cmath>
#include <initializer_list>
#include <limits>

using json = nlohmann::json;

// 内置提示词术语：种子键 → 词条映射（只读，注入提示词引导用词；不参与追加/自进化）
static QMap<QString, QString> builtinPromptGlossary(const QString &seedKey)
{
    const QStringList pairs = seedKey.compare("adult", Qt::CaseInsensitive) == 0 ? glossaryseeds::adult()
                            : seedKey.compare("standard", Qt::CaseInsensitive) == 0 ? glossaryseeds::standard()
                            : glossaryseeds::galgame();
    QMap<QString, QString> out;
    for (const QString &p : pairs)
        out.insert(p.section('=', 0, 0), p.section('=', 1));
    return out;
}

namespace
{
    struct ParsedTokenUsage
    {
        bool available = false;
        long long prompt = 0;
        long long completion = 0;
        long long total = 0;
    };

    long long safeTokenSum(
        long long first,
        long long second)
    {
        first = std::max(0LL, first);
        second = std::max(0LL, second);

        const long long maximum =
            std::numeric_limits<long long>::max();

        if (first > maximum - second)
            return maximum;

        return first + second;
    }

    long long jsonTokenCount(
        const json &value)
    {
        try
        {
            if (value.is_number_unsigned())
            {
                const auto number =
                    value.get<unsigned long long>();

                const auto maximum =
                    static_cast<unsigned long long>(
                        std::numeric_limits<long long>::max());

                return static_cast<long long>(
                    std::min(number, maximum));
            }

            if (value.is_number_integer())
            {
                return std::max(
                    0LL,
                    value.get<long long>());
            }

            if (value.is_number_float())
            {
                const double number =
                    value.get<double>();

                if (!std::isfinite(number) ||
                    number < 0.0)
                {
                    return -1;
                }

                const double maximum =
                    static_cast<double>(
                        std::numeric_limits<long long>::max());

                return static_cast<long long>(
                    std::min(number, maximum));
            }

            // 少数 OpenAI 兼容端点会把数字放在字符串中。
            if (value.is_string())
            {
                bool ok = false;

                const long long number =
                    QString::fromStdString(
                        value.get<std::string>())
                        .trimmed()
                        .toLongLong(&ok);

                return ok
                           ? std::max(0LL, number)
                           : -1;
            }
        }
        catch (...)
        {
        }

        return -1;
    }

    long long findTokenCount(
        const json &object,
        std::initializer_list<const char *> keys)
    {
        if (!object.is_object())
            return -1;

        for (const char *key : keys)
        {
            if (!object.contains(key))
                continue;

            const long long value =
                jsonTokenCount(object.at(key));

            if (value >= 0)
                return value;
        }

        return -1;
    }

    ParsedTokenUsage parseTokenUsage(
        const json &response)
    {
        ParsedTokenUsage result;

        const json *usage = nullptr;

        // OpenAI、DeepSeek、OpenRouter 等兼容格式。
        if (response.contains("usage") &&
            response.at("usage").is_object())
        {
            usage = &response.at("usage");
        }
        // Gemini 原生格式。
        else if (response.contains("usageMetadata") &&
                 response.at("usageMetadata").is_object())
        {
            usage = &response.at("usageMetadata");
        }
        // Ollama 部分接口直接把统计字段放在根对象。
        else if (response.contains("prompt_eval_count") ||
                 response.contains("eval_count"))
        {
            usage = &response;
        }

        if (!usage)
            return result;

        long long prompt =
            findTokenCount(
                *usage,
                {"prompt_tokens",
                 "input_tokens",
                 "promptTokenCount",
                 "promptTokens",
                 "prompt_eval_count"});

        long long completion =
            findTokenCount(
                *usage,
                {"completion_tokens",
                 "output_tokens",
                 "candidatesTokenCount",
                 "completionTokens",
                 "eval_count"});

        long long total =
            findTokenCount(
                *usage,
                {"total_tokens",
                 "totalTokenCount",
                 "totalTokens"});

        const bool hasAnyValue =
            prompt >= 0 ||
            completion >= 0 ||
            total >= 0;

        if (!hasAnyValue)
            return result;

        // 供应商只返回 total 和其中一项时，推导缺失项。
        if (prompt < 0 &&
            completion >= 0 &&
            total >= completion)
        {
            prompt = total - completion;
        }

        if (completion < 0 &&
            prompt >= 0 &&
            total >= prompt)
        {
            completion = total - prompt;
        }

        prompt = std::max(0LL, prompt);
        completion = std::max(0LL, completion);

        const long long componentTotal =
            safeTokenSum(prompt, completion);

        if (total < 0)
            total = componentTotal;
        else
            total = std::max(total, componentTotal);

        result.available = true;
        result.prompt = prompt;
        result.completion = completion;
        result.total = total;

        return result;
    }
}

// ==========================================
// 日志与常量 (HTML Optimized)
// ==========================================
const char *SV_LOG_START[] = {
    "<font color='#4CAF50'><b>Server started.</b></font> Port: %1, Threads: %2",
    "<font color='#4CAF50'><b>服务已启动</b></font>，端口：%1，并发线程数：%2"};
const char *SV_LOG_STOP[] = {
    "<font color='#F44336'><b>Server stopped</b></font>",
    "<font color='#F44336'><b>服务已停止</b></font>"};
const char *SV_LOG_REQ_PREFIX[] = {
    "Request received: ",
    "收到请求: "};
const char *SV_ERR_KEY[] = {"Error: Invalid API Key", "错误：API 密钥无效"};
const char *SV_ERR_FMT[] = {"Error: Invalid Response Format", "错误：响应格式无效"};
const char *SV_ERR_JSON[] = {"Error: JSON Parse Error", "错误：JSON 解析失败"};
const char *SV_NEW_TERM[] = {
    "<font color='#FF9800'>✨ New Term Discovered: </font>",
    "<font color='#FF9800'>✨ 发现新术语: </font>"};
const char *SV_RETRY_ATTEMPT[] = {"🔄 Retry translation (%1/%2): ", "🔄 重试翻译 (%1/%2): "};
const char *SV_RETRY_SUCCESS[] = {"<font color='#4CAF50'>✅ Retry successful</font>", "<font color='#4CAF50'>✅ 重试成功</font>"};
const char *SV_RETRY_FAILED[] = {"<font color='#F44336'>❌ Retry failed, skipping text</font>", "<font color='#F44336'>❌ 重试失败，跳过文本</font>"};
const char *SV_ABORTED[] = {"⛔ Translation Aborted", "⛔ 翻译已终止"};

struct EscapeMap
{
    QMap<QString, QString> map;
    int counter = 0;
};

// 冻结保护
QString TranslationServer::freezeEscapesLocal(const QString &input, EscapeMap &context)
{
    QString result = input;
    context.map.clear();
    context.counter = 0;
    static const QRegularExpression regex(R"(\{\{.*?\}\}|<[^>]+>)");
    int lastEnd = 0;
    QString newResult;
    QRegularExpressionMatchIterator i = regex.globalMatch(result);
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();
        newResult.append(result.mid(lastEnd, match.capturedStart() - lastEnd));
        QString original = match.captured(0);
        QString tokenKey = QString("[T_%1]").arg(context.counter++);
        context.map[tokenKey] = original;
        newResult.append(tokenKey);
        lastEnd = match.capturedEnd();
    }
    newResult.append(result.mid(lastEnd));
    return newResult;
}

// 解冻还原
QString TranslationServer::thawEscapesLocal(const QString &input, const EscapeMap &context)
{
    QString result = input;
    static const QRegularExpression tokenRegex(R"(\s*[\[<【{]\s*T_(\d+)\s*[\]>】}]\s*)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator i = tokenRegex.globalMatch(result);
    QString newResult;
    int lastEnd = 0;
    while (i.hasNext())
    {
        QRegularExpressionMatch match = i.next();
        newResult.append(result.mid(lastEnd, match.capturedStart() - lastEnd));
        QString key = QString("[T_%1]").arg(match.captured(1));
        if (context.map.contains(key))
            newResult.append(context.map[key]);
        else
            newResult.append(match.captured(0));
        lastEnd = match.capturedEnd();
    }
    newResult.append(result.mid(lastEnd));
    return newResult;
}

// 🔥 Unity 富文本 -> Qt HTML 转换器 (强化兼容版)
QString TranslationServer::unityToHtml(const QString &text)
{
    QString t = text;

    // 1. 去除转义符
    t.replace(R"(\=)", "=");

    // 2. 🛡️ 保护安全可渲染的标签，转换为不会被逃逸的安全标记
    t.replace(QRegularExpression(R"(<br\s*/?>)", QRegularExpression::CaseInsensitiveOption), "[[[BR]]]");

    static const QRegularExpression colorStart(R"-(<color\s*=\s*"?([^>"]+?)"?>)-", QRegularExpression::CaseInsensitiveOption);
    t.replace(colorStart, "[[[C:\\1]]]");
    t.replace(QRegularExpression(R"(</color>)", QRegularExpression::CaseInsensitiveOption), "[[[/C]]]");

    t.replace(QRegularExpression(R"(<(b|i|u)>)", QRegularExpression::CaseInsensitiveOption), "[[[\\1]]]");
    t.replace(QRegularExpression(R"(</(b|i|u)>)", QRegularExpression::CaseInsensitiveOption), "[[[/\\1]]]");

    // 3. 💥 核心：拦截全部破坏性未知标签（如 <line-height>, <align>），彻底转义为普通文本，防止它们弄爆 Qt 的 HTML 渲染树
    t.replace("&", "&amp;");
    t.replace("<", "&lt;");
    t.replace(">", "&gt;");

    // 4. 🎨 安全还原：把保护好的标记恢复为真正的 HTML
    t.replace("[[[BR]]]", "<span style='color:#FF5722; font-weight:bold;'>[BR]</span><br>");

    static const QRegularExpression colorStartRes(R"(\[\[\[C:(.*?)\]\]\])");
    t.replace(colorStartRes, R"(<span style="color:\1;">)");
    t.replace("[[[/C]]]", "</span>");

    t.replace(QRegularExpression(R"(\[\[\[(b|i|u)\]\]\])"), "<\\1>");
    t.replace(QRegularExpression(R"(\[\[\[/(b|i|u)\]\]\])"), "</\\1>");

    // 5. 将所有被转义过的其他破坏性标签直接物理隐藏 (保持日志区的绝对简洁)
    static const QRegularExpression remainingTags(R"(&lt;/?[a-zA-Z0-9_\-]+[^&]*&gt;)");
    t.replace(remainingTags, "");

    return t;
}

// 彩虹生成器预留实现
QString TranslationServer::makeRainbow(const QString &text)
{
    return text;
}

// ==========================================
// 🚨 终极标签克隆手术 (完全解封并强化) 🚨
// ==========================================
// ==========================================
// TranslationServer Implementation
// ==========================================

TranslationServer::TranslationServer(QObject *parent) : QObject(parent), m_running(false)
{
    m_stopRequested = false;
    m_isStopping = false;
    m_svr = nullptr;
    m_serverThread = nullptr;
    m_cleanupThread = nullptr;

    connect(this, &TranslationServer::logMessage, [](const QString &msg)
            { LogManager::instance().addLog(msg); });
}

TranslationServer::~TranslationServer()
{
    stopServer();
    if (m_cleanupThread && m_cleanupThread->joinable())
    {
        shutdownDiagnostic("previous cleanup join begin");
        m_cleanupThread->join();
        shutdownDiagnostic("previous cleanup join end");
        delete m_cleanupThread;
        m_cleanupThread = nullptr;
    }
}

void TranslationServer::updateConfig(const AppConfig &config)
{
    std::lock_guard<std::mutex> keyLock(m_keyMutex);
    std::lock_guard<std::mutex> cfgLock(m_configMutex);
    m_config = config;
    m_apiKeys.clear();
    QStringList keys = m_config.api_key.split(',', Qt::SkipEmptyParts);
    for (const auto &k : keys)
        m_apiKeys.push_back(k.trimmed());
    m_currentKeyIndex = 0;
    if (m_config.enable_glossary)
    {
        GlossaryManager::instance().setFilePath(m_config.glossary_path);
        GlossaryManager::instance().setBuiltinTerms(builtinPromptGlossary(m_config.glossary_prompt_seed));
    }
}

AppConfig TranslationServer::getConfig()
{
    std::lock_guard<std::mutex> lock(m_configMutex);
    return m_config;
}

void TranslationServer::startServer()
{
    if (m_running || m_starting || m_isStopping)
        return;

    if (m_cleanupThread && m_cleanupThread->joinable())
    {
        shutdownDiagnostic("previous cleanup join begin");
        m_cleanupThread->join();
        shutdownDiagnostic("previous cleanup join end");
        delete m_cleanupThread;
        m_cleanupThread = nullptr;
    }

    int lang = 1;
    int port = 6800;
    int threads = 64;
    QString glossaryPath = "";
    bool enableBatch = false;
    bool handleRichText = false;
    bool extractNewline = true;
    QString hijackFromLang = "ja";
    QString hijackToLang = "zh";
    const QString hijackEndpoint = QStringLiteral("CustomTranslate"); // Task 5 端点锁定
    bool hijackTextGetter = false;
    bool enableImGui = false;
    bool enableUGui = true;
    bool enableUIElements = true;
    bool enableNGUI = true;
    bool enableTextMeshPro = true;
    bool enableTextMesh = false;
    bool enableFairyGUI = true;

    {
        std::lock_guard<std::mutex> lock(m_configMutex);

        lang = m_config.language;
        port = m_config.port;
        threads = std::clamp(m_config.max_threads, 64, 256);
        glossaryPath = m_config.glossary_path;
        enableBatch = m_config.enable_batch;
        handleRichText = m_config.handle_rich_text;
        extractNewline = m_config.extract_newline;

        hijackFromLang = m_config.hijack_from_lang;
        hijackToLang = m_config.hijack_to_lang;
        hijackTextGetter = m_config.hijack_text_getter;

        enableImGui = m_config.hijack_enable_imgui;
        enableUGui = m_config.hijack_enable_ugui;
        enableUIElements = m_config.hijack_enable_ui_elements;
        enableNGUI = m_config.hijack_enable_ngui;
        enableTextMeshPro = m_config.hijack_enable_text_mesh_pro;
        enableTextMesh = m_config.hijack_enable_text_mesh;
        enableFairyGUI = m_config.hijack_enable_fairy_gui;
    }

    // Batch mode prerequisite guard:
    // A valid glossary .txt path must exist so we can backtrack and hijack the target ini.
    if (enableBatch)
    {
        QString glossaryPathTrimmed = glossaryPath.trimmed();
        QFileInfo glossaryInfo(glossaryPathTrimmed);

        if (glossaryPathTrimmed.isEmpty() || !glossaryInfo.exists() || !glossaryInfo.isFile())
        {
            emit logMessage((lang == 0)
                                ? "❌ Batch Mode blocked: glossary path is missing or invalid."
                                : "❌ 打包模式启动失败：术语表路径为空或无效。");
            return;
        }

        QString iniPath = XuaConfigHijacker::deduceIniPath(glossaryPathTrimmed);
        if (iniPath.isEmpty())
        {
            emit logMessage((lang == 0)
                                ? "❌ Batch Mode blocked: unable to deduce target.ini from glossary path."
                                : "❌ 打包模式启动失败：无法根据术语表路径反推出目标.ini。\n请确认术语表路径层级正确。");
            return;
        }

        glossaryPath = glossaryPathTrimmed;
    }

    auto listener = std::make_unique<httplib::Server>();
    configureServer(*listener, threads);
    if (!listener->bind_to_port("0.0.0.0", port))
    {
        emit logMessage((lang == 0)
                            ? QStringLiteral("Translation service failed to bind port %1.").arg(port)
                            : QStringLiteral("翻译服务无法绑定端口 %1。").arg(port));
        return;
    }

    m_running = false;
    m_stopRequested = false;
    m_starting = true;
    m_svr = listener.release();

    emit logMessage(QString(SV_LOG_START[lang]).arg(port).arg(threads));

    if (enableBatch && !glossaryPath.isEmpty())
    {

        QString hijackedFile = XuaConfigHijacker::autoDetectAndHijack(glossaryPath, port, threads, handleRichText, extractNewline,
                                                                      hijackFromLang, hijackToLang, hijackEndpoint, hijackTextGetter,
                                                                      enableImGui, enableUGui, enableUIElements,
                                                                      enableNGUI, enableTextMeshPro, enableTextMesh, enableFairyGUI);

        if (!hijackedFile.isEmpty())
        {
            QString logMsg = (lang == 0)
                                 ? QString(
                                       "🔗 <font color='#2196F3'>Batch Mode ON</font>: "
                                       "Game config injected (%1)")
                                       .arg(hijackedFile)
                                 : QString(
                                       "🔗 <font color='#2196F3'>打包模式已开启</font>："
                                       "游戏配置已智能接管 (%1)")
                                       .arg(hijackedFile);

            emit logMessage(logMsg);
        }
    }
    else
    {
        emit logMessage((lang == 0) ? "🛡️ Standard Mode: Config.ini untouched." : "🛡️ 标准模式：保持游戏原生配置不动。");
    }

    m_serverThread = new std::thread([this, listener = m_svr]()
    {
        const bool success = listener->listen_after_bind();
        m_running = false;
        m_starting = false;
        if (!m_stopRequested)
        {
            QMetaObject::invokeMethod(this, [this, success]()
            {
                if (!success)
                    emit logMessage(QStringLiteral("Translation listener exited unexpectedly."));
                stopServer();
            }, Qt::QueuedConnection);
        }
    });
}

void TranslationServer::stopServer()
{
    shutdownDiagnostic(QString("stopServer enter running=%1 starting=%2 stopping=%3 in-flight=%4")
        .arg(m_running.load()).arg(m_starting.load()).arg(m_isStopping.load()).arg(m_inFlight.load()));
    if (m_isStopping || !m_svr)
    {
        shutdownDiagnostic("stopServer no-op");
        return;
    }

    m_stopRequested = true;
    m_isStopping = true;
    auto *listener = m_svr;
    auto *worker = m_serverThread;
    m_svr = nullptr;
    m_serverThread = nullptr;

    if (m_cleanupThread && m_cleanupThread->joinable())
    {
        m_cleanupThread->join();
        delete m_cleanupThread;
        m_cleanupThread = nullptr;
    }

    m_cleanupThread = new std::thread([this, listener, worker]()
    {
        QElapsedTimer stopTimer;
        stopTimer.start();

        // httplib::stop() is a no-op until listen_after_bind() has entered.
        listener->wait_until_ready();
        shutdownDiagnostic(QString("stop listener begin elapsed=%1ms").arg(stopTimer.elapsed()));
        listener->stop();
        if (worker && worker->joinable())
        {
            shutdownDiagnostic(QString("join listener begin elapsed=%1ms").arg(stopTimer.elapsed()));
            worker->join();
            shutdownDiagnostic(QString("join listener end elapsed=%1ms").arg(stopTimer.elapsed()));
        }
        delete worker;
        delete listener;

        int lang = 1;
        int port = 6800;
        bool isDebug = false;
        QString glossaryPath;
        {
            std::lock_guard<std::mutex> lock(m_configMutex);
            lang = m_config.language;
            glossaryPath = m_config.glossary_path;
            port = m_config.port;
            isDebug = m_config.enable_debug_mode;
        }

        if (!glossaryPath.isEmpty())
        {
            shutdownDiagnostic("configuration restore begin");
            QString restoredFile = XuaConfigHijacker::autoDetectAndRestore(glossaryPath, port);
            if (!restoredFile.isEmpty())
            {
                emit logMessage((lang == 0) ? QString("✅ Config routing cleared: %1").arg(restoredFile)
                                            : QString("✅ 游戏配置路由已清除：%1").arg(restoredFile));
            }
        }

        const qint64 elapsed = stopTimer.elapsed();
        if (isDebug)
            emit logMessage(QString(SV_LOG_STOP[lang]) + QString(" <span style='color:#FF4500; font-size:medium;'>[⏱️ %1 ms]</span>").arg(elapsed));
        else
            emit logMessage(SV_LOG_STOP[lang]);

        m_running = false;
        m_starting = false;
        m_isStopping = false;
        shutdownDiagnostic(QString("serverStopped emit elapsed=%1ms").arg(stopTimer.elapsed()));
        emit serverStopped();
    });
    shutdownDiagnostic("stopServer returned; cleanup scheduled");
}

void TranslationServer::configureServer(httplib::Server &server, int threads)
{
    server.set_keep_alive_timeout(1);
    server.new_task_queue = [this, threads]
    {
        m_running = !m_stopRequested.load();
        m_starting = false;
        QMetaObject::invokeMethod(this, [this]()
        {
            if (m_running && !m_isStopping)
                emit serverStarted();
        }, Qt::QueuedConnection);
        return new httplib::ThreadPool(threads);
    };

    // ==========================================
    // Custom Handler
    // ==========================================
    auto customHandler = [this](const httplib::Request &req, httplib::Response &res)
    {
        if (m_stopRequested.load(std::memory_order_relaxed))
        {
            res.status = 503;
            res.set_content("Service Unavailable", "text/plain");
            return;
        }

        if (!req.has_param("text"))
        {
            res.set_content("", "text/plain");
            return;
        }
        QString text = QString::fromStdString(req.get_param_value("text"));
        if (text.isEmpty())
        {
            res.set_content("", "text/plain");
            return;
        }

        int langIdx = 1;
        bool isDebug = false;
        {
            std::lock_guard<std::mutex> lock(m_configMutex);
            langIdx = m_config.language;
            isDebug = m_config.enable_debug_mode;
        }


        QString logHtml = unityToHtml(text);
        QString prefix = QString("<b style='color:#00B0FF'>[Custom]</b> ") + QString(SV_LOG_REQ_PREFIX[langIdx]);

        if (isDebug)
            emit logMessage(prefix + logHtml);
        else
            emit logMessage(QString(SV_LOG_REQ_PREFIX[langIdx]) + logHtml);

        emit workStarted();
        QElapsedTimer timer;
        timer.start();

        if (m_stopRequested.load(std::memory_order_relaxed))
        {
            emit workFinished(false);
            res.status = 500;
            res.set_content("Failed", "text/plain");
            return;
        }

        const quint64 id=++m_requestSequence;
        trace(id,0,"Single input: "+text);
        QString result = performTranslation(text, QString::fromStdString(req.remote_addr),0,id);

        // 🛑 如果处理期间点下了停止，阻止最终的输出！
        if (m_stopRequested.load(std::memory_order_relaxed))
        {
            emit workFinished(false);
            res.status = 500;
            res.set_content("Failed", "text/plain");
            return;
        }

        QString resultHtml = unityToHtml(result);

        qint64 elapsed = timer.elapsed();
        emit workFinished(!result.isEmpty() && !m_stopRequested);

        if (result.isNull())
        {
            res.status = 500;
            res.set_content("Failed", "text/plain");
        }
        else
        {
            if (isDebug)
                emit logMessage(QString("  -> %1 <span style='color:#FF4500; font-size:medium;'>[⏱️ %2 ms]</span>").arg(resultHtml).arg(elapsed));
            else
                emit logMessage(QString("  -> %1").arg(resultHtml));
            res.set_content(result.toStdString(), "text/plain; charset=utf-8");
        }
    };

    server.Post("/batch", [this](const httplib::Request &req, httplib::Response &res) {
        const quint64 id = ++m_requestSequence;
        QElapsedTimer elapsed; elapsed.start();
        try {
            auto input = json::parse(req.body);
            if (!input.is_object() || !input.contains("texts") || !input["texts"].is_array() || input["texts"].empty()) {
                res.status = 400; res.set_content("Missing nonempty texts array", "text/plain"); return;
            }
            json output = input["texts"], pending = json::array();
            std::vector<size_t> positions;
            for (size_t i = 0; i < output.size(); ++i) {
                if (!output[i].is_string()) { res.status=400; res.set_content("texts must contain strings", "text/plain"); return; }
                const QString text = QString::fromStdString(output[i].get<std::string>());
                if (text.isEmpty() || isPunctuationOnly(text)) continue;
                positions.push_back(i); pending.push_back(output[i]);
            }
            trace(id, 0, QString("Batch input items=%1 model-items=%2 payload=%3")
                  .arg(output.size()).arg(pending.size()).arg(QString::fromStdString(input["texts"].dump())));
            if (!pending.empty()) {
                emit workStarted();
                const QString result=performTranslation(QString::fromStdString(pending.dump()), QString::fromStdString(req.remote_addr), int(pending.size()), id);
                emit workFinished(!result.isNull());
                if (result.isNull()) {res.status=502;res.set_content("Translation request failed; see correlated request log", "text/plain");return;}
                auto translated=json::parse(result.toStdString());
                for(size_t i=0;i<positions.size();++i) output[positions[i]]=translated[i];
            }
            trace(id,0,QString("Batch output items=%1 elapsed=%2ms payload=%3")
                  .arg(output.size()).arg(elapsed.elapsed()).arg(QString::fromStdString(output.dump())));
            res.set_content(json{{"translations",output}}.dump(),"application/json; charset=utf-8");
        } catch(const json::exception &) {res.status=400;res.set_content("Invalid request JSON", "text/plain");}
    });

    server.Get("/", customHandler);
    server.Post("/", customHandler);

}

void TranslationServer::trace(quint64 id, int attempt, const QString &message)
{
    emit logMessage(QString("[%1] [request=%2 attempt=%3 in-flight=%4] %5")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss.zzz"))
        .arg(id).arg(attempt).arg(m_inFlight.load()).arg(message.toHtmlEscaped().replace("\n", "<br>")));
}

bool TranslationServer::isPunctuationOnly(const QString &text)
{
    static const QRegularExpression only(R"(\A[\p{P}\p{S}\s]+\z)");
    return !text.trimmed().isEmpty() && only.match(text).hasMatch();
}

QString TranslationServer::performTranslation(const QString &text, const QString &clientIP, int batchCount, quint64 requestId)
{
    if (batchCount == 0 && (text.isEmpty() || isPunctuationOnly(text)))
        return text;
    const AppConfig cfg = getConfig();
    for (int attempt = 0; attempt <= cfg.max_retries; ++attempt) {
        if (m_stopRequested) return QString();
        const QString result = performSingleTranslationAttempt(text, clientIP, batchCount, requestId, attempt + 1);
        if (!result.isNull()) return result;
        trace(requestId, attempt + 1, "Protocol failure; retrying if attempts remain");
    }
    return QString();
}

bool TranslationServer::isValidTranslationResult(const QString &result)
{
    return !result.isNull();
}

// Task 3 兜底校验：译文合理性（重试循环据此决定是否附加强约束）
void TranslationServer::recordDialoguePair(const QString &src, const QString &dst)
{
    std::lock_guard<std::mutex> lock(m_dialogueMutex);
    if (m_dialogueLog.size() >= 500) m_dialogueLog.removeFirst();
    m_dialogueLog.append({src, dst});
}

QStringList TranslationServer::drainDialoguePairs()
{
    std::lock_guard<std::mutex> lock(m_dialogueMutex);
    QStringList out;
    for (const auto &p : m_dialogueLog)
        out << p.first + "\t" + p.second;
    m_dialogueLog.clear();
    return out;
}

// 🔥 终极单次请求翻译尝试：完美结合碎片化标签重组与内存防泄漏机制
QString TranslationServer::performSingleTranslationAttempt(const QString &text, const QString &clientIP, int batchCount, quint64 requestId, int attempt)
{
    if (m_stopRequested.load(std::memory_order_relaxed))
        return QString();

    // ==========================================
    // 🛠️ 预处理：物理粉碎干扰 LLM 翻译的碎片化标签 (<rotate>, <voffset>)
    // 让 LLM 能够看到完整通顺的句子！
    // ==========================================
    QString preText = text;
    bool hasRotate = false;
    QString rotateOpenTag = "";

    QRegularExpression rotFinder(R"(<rotate\s*\\?=\s*[^>]+>|<rotate>)", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch rotMatch = rotFinder.match(preText);
    if (rotMatch.hasMatch())
    {
        hasRotate = true;
        rotateOpenTag = rotMatch.captured(0);
    }

    // 无情抹除这些把字拆散的罪魁祸首
    preText.remove(QRegularExpression(R"(</?rotate[^>]*>)", QRegularExpression::CaseInsensitiveOption));
    preText.remove(QRegularExpression(R"(</?voffset[^>]*>)", QRegularExpression::CaseInsensitiveOption));

    AppConfig cfg;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        cfg = m_config;
    }

    QString apiKey = getNextApiKey();
    if (apiKey.isEmpty())
    {
        emit logMessage("<font color='#F44336'>❌ " + QString(SV_ERR_KEY[cfg.language]) + "</font>");
        return QString();
    }

    EscapeMap escapeCtx;
    // 使用纯净版文本进行标签冻结
    QString processedText = freezeEscapesLocal(preText, escapeCtx);
    if (cfg.enable_glossary)
        processedText = RegexManager::instance().processPre(processedText);
    std::string clientId = generateClientId(clientIP.toStdString()).toStdString();

    QString finalSystemPrompt = cfg.system_prompt;
    bool performExtraction = false;
    // Task 2 语言联动：源/目标语言注入系统提示（auto=自动识别）
    // 注意：这里必须直接写中文字面量。历史上 \uXXXX 转义被脚本工具啃成 \nuXXXX，
    // 注入提示词的变成 "换行+u6e90..." 垃圾文本，语言联动静默失效
    {
        const QString src = cfg.hijack_from_lang;
        const QString dst = cfg.hijack_to_lang;
        QString langLine;
        if (src.compare("auto", Qt::CaseInsensitive) == 0)
            langLine = QString::fromUtf8("源语言：自动识别。");
        else
            langLine = QString::fromUtf8("源语言：%1。").arg(src);
        langLine += QString::fromUtf8("目标语言：%1。").arg(dst);
        finalSystemPrompt += QString::fromUtf8("\n\n【Language】:\n") + langLine + QString::fromUtf8("\n");
    }


    finalSystemPrompt += "\n\n【Translation Protocol (STRICT)】:\n"
                         "0. 🛡️ PRIORITY: TAGS/VARS/Z-CODES > GRAMMAR > STYLE. Never break code structures.\n"
                         "1. 📤 OUTPUT: Return ONLY the translated result. NO explanations. NO markdown.\n"
                         "2. 🧱 IMMUTABLES (KEEP EXACTLY):\n"
                         "   - [T_0], [T_1] ... : Placeholder tokens.\n"
                         "   - Preserve actual newlines, their order and positions.\n"
                         "   - {{A}}, {{B}} ... : Variables. NEVER translate letters inside.\n"
                         "3. 📦 CONTAINERS (TRANSLATE CONTENT, KEEP WRAPPERS):\n"
                         "   - Z-Codes: 'Z[A-Z]{2}Z ... Z[A-Z]{2}Z'. Keep markers, translate inside.\n"
                         "   - HTML: '<tag>text</tag>'. Keep tags, translate 'text'.\n"
                         "4. 💬 PUNCTUATION & FORMAT:\n"
                         "   - Convert punctuation in visible text ONLY.\n"
                         "   - Do NOT modify punctuation inside tags.\n"
                         "   - Preserve spacing around tags.\n"
                         "5. 🧠 TRANSLATION LOGIC:\n"
                         "   - Treat input as independent UI fragments.\n"
                         "6. 🚫 ANTI-HALLUCINATION (CRITICAL):\n"
                         "   - DO NOT add <size>, <color>, <b>, <i> or brackets like [size=...] if they are not in the input.\n"
                         "   - DO NOT try to fix or close tags. Just keep exactly what you see.\n"
                         "   - DO NOT invent speaker names. DO NOT output </T_0>.\n"
                         "7. 🚨 FINAL SAFETY CHECK:\n"
                         "   - All tags closed\n"
                         "   - All {{X}} preserved\n"
                         "   - No new Z-codes created\n";

    if (cfg.enable_glossary)
    {
        QString glossaryContext = GlossaryManager::instance().getContextPrompt(processedText);
        if (!glossaryContext.isEmpty())
            finalSystemPrompt += "\n" + glossaryContext;
        if (batchCount == 0 && text.length() > 5)
        {
            performExtraction = true;
            finalSystemPrompt += "\n【Term Extraction】:\n"
                                 "1. Wrap translation in <tl>...</tl>.\n"
                                 "2. If you find Proper Nouns NOT in glossary, append <tm>Src=Trgt</tm> AFTER the translation.\n"
                                 "3. Keep <tm> tags OUTSIDE of <tl> tags.\n";
        }
    }

    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", finalSystemPrompt.toStdString()}});

    {
        std::lock_guard<std::mutex> lock(m_contextMutex);
        Context &ctx = m_contexts[clientId];
        if (ctx.max_len != cfg.context_num)
            ctx.max_len = cfg.context_num;
        for (const auto &pair : ctx.history)
        {
            messages.push_back({{"role", "user"}, {"content", pair.first.toStdString()}});
            messages.push_back({{"role", "assistant"}, {"content", pair.second.toStdString()}});
        }
    }

    QString prePrefix = cfg.pre_prompt;
    if (prePrefix.isEmpty() || prePrefix.contains(QString::fromUtf8("翻译成")))
        prePrefix = QString::fromUtf8("将下面的文本翻译成%1：").arg(cfg.hijack_to_lang);
    QString currentUserContent = prePrefix + processedText;
    if (batchCount > 0) {
        finalSystemPrompt += "\nBatch output: return ONLY a JSON array of strings in input order. "
                             "Each element is one item, preserve newlines inside elements. No XML wrappers.";
        messages[0]["content"] = finalSystemPrompt.toStdString();
        currentUserContent = text;
    }
    messages.push_back({{"role", "user"}, {"content", currentUserContent.toStdString()}});

    json payload;
    payload["model"] = cfg.model_name.toStdString();
    payload["messages"] = messages;
    payload["temperature"] = cfg.temperature;
    // Sakura/GalTransl 窄域翻译模型：官方推荐采样 top_p=0.8，其余模型不受影响
    if (cfg.model_name.contains("galtransl", Qt::CaseInsensitive)
        || cfg.model_name.contains("sakura", Qt::CaseInsensitive))
        payload["top_p"] = 0.8;

    // ==========================================
    // 🛠️ 特性 1：底层网络解耦 & 内存回收确认 (Modern C++ RAII)
    // ==========================================
    // Windows runs thread_local destructors during loader-locked thread teardown.
    // QNetworkAccessManager may join its Qt network thread: destroy it before leaving the request.
    QNetworkAccessManager network;

    QNetworkRequest request(QUrl(cfg.api_address + "/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());

    request.setTransferTimeout(cfg.timeout_ms);

    std::unique_ptr<QNetworkReply> reply(network.post(request, QByteArray::fromStdString(payload.dump())));

    // ==========================================
    // 🔥 异步轮询事件泵 (Async Polling Event Pump)
    // 彻底解决原生 std::thread 中导致 stopServer 卡死的问题！
    // ==========================================
    QEventLoop loop;
    QElapsedTimer timer;
    timer.start();
    ++m_inFlight;
    trace(requestId, attempt, "Model request started");
    struct Flight { std::atomic<int> &count; ~Flight() { --count; } } flight{m_inFlight};
    bool isTimeout = false;

    while (!reply->isFinished())
    {
        if (m_stopRequested.load(std::memory_order_relaxed))
        {
            reply->abort();
            network.clearAccessCache();
            network.clearConnectionCache();
            break;
        }

        if (timer.elapsed() > cfg.timeout_ms + 5000)
        {
            isTimeout = true;
            reply->abort();
            break;
        }

        loop.processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (m_stopRequested.load(std::memory_order_relaxed))
        return QString();

    if (isTimeout)
    {
        trace(requestId, attempt, QString("Request timeout elapsed=%1ms").arg(timer.elapsed()));
        return QString();
    }

    // --- ⬇️ 解析流程 ⬇️ ---
    QString resultText;
    if (reply->error() == QNetworkReply::NoError)
    {
        QByteArray responseBytes = reply->readAll();
        trace(requestId, attempt, QString("HTTP %1 elapsed=%2ms")
              .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()).arg(timer.elapsed()));
        try
        {
            json response = json::parse(responseBytes.toStdString());
            
            const ParsedTokenUsage tokenUsage =
                parseTokenUsage(response);

            if (tokenUsage.available)
            {
                emit tokenUsageReceived(
                    tokenUsage.prompt,
                    tokenUsage.completion,
                    tokenUsage.total);
            }
            else
            {
                emit tokenUsageUnavailable();
            }

            if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()
                && response["choices"][0].contains("message") && response["choices"][0]["message"].contains("content")
                && response["choices"][0]["message"]["content"].is_string())
            {
                std::string content = response["choices"][0]["message"]["content"];
                QString cleanContent = QString::fromStdString(content);
                trace(requestId, attempt, "Model response: " + cleanContent);
                if (batchCount > 0) {
                    auto values = json::parse(content);
                    if (!values.is_array() || values.size() != size_t(batchCount)) {
                        trace(requestId, attempt, QString("Batch count mismatch: expected=%1 received=%2")
                              .arg(batchCount).arg(values.is_array() ? int(values.size()) : -1));
                        return QString();
                    }
                    for (const auto &value : values) if (!value.is_string()) {
                        trace(requestId, attempt, "Batch element is not a JSON string");
                        return QString();
                    }
                    const QString batchResult = QString::fromStdString(values.dump());
                    {
                        std::lock_guard<std::mutex> lock(m_contextMutex);
                        Context &ctx = m_contexts[clientId];
                        ctx.max_len = cfg.context_num;
                        ctx.history.push_back({currentUserContent, batchResult});
                        while (ctx.history.size() > size_t(ctx.max_len)) ctx.history.pop_front();
                    }
                    return batchResult;
                }


                static const QRegularExpression thinkTag(R"(<think(?:ing)?>.*?</think(?:ing)?>)", QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
                static const QRegularExpression thinkTagShort(R"(</?think(?:ing)?>)", QRegularExpression::CaseInsensitiveOption);
                static const QRegularExpression boldTag(R"(^\*\*|\*\*$)");

                cleanContent.remove(thinkTag);
                cleanContent.remove(thinkTagShort);
                cleanContent.replace(boldTag, "");
                

                if (performExtraction)
                {
                    static const QRegularExpression reTm("<tm>\\s*(.*?)\\s*=\\s*(.*?)\\s*</tm>", QRegularExpression::DotMatchesEverythingOption);
                    static const QRegularExpression tokenRegex(R"(\[T_\d+\])");
                    static const QRegularExpression termCodeRegex("Z[A-Z]{2}Z");

                    QString reconstructionBuffer;
                    int lastPos = 0;
                    QRegularExpressionMatchIterator i = reTm.globalMatch(cleanContent);
                    while (i.hasNext())
                    {
                        QRegularExpressionMatch match = i.next();
                        QString k = match.captured(1).trimmed();
                        QString v = match.captured(2).trimmed();
                        reconstructionBuffer.append(cleanContent.mid(lastPos, match.capturedStart() - lastPos));

                        bool isValidTerm = true;
                        if (k.isEmpty() || v.isEmpty() || k.contains(tokenRegex) || v.contains(tokenRegex) || k.contains(termCodeRegex) || v.contains(termCodeRegex))
                            isValidTerm = false;

                        if (isValidTerm && processedText.contains(k, Qt::CaseInsensitive))
                        {
                            GlossaryManager::instance().addNewTerm(k, v);
                            emit logMessage(QString(SV_NEW_TERM[cfg.language]) + "<b>" + k + "</b> = <b>" + v + "</b>");
                        }
                        reconstructionBuffer.append(v);
                        lastPos = match.capturedEnd();
                    }
                    reconstructionBuffer.append(cleanContent.mid(lastPos));
                    cleanContent = reconstructionBuffer;
                }

                static const QRegularExpression reTl("<tl>(.*?)</tl>", QRegularExpression::DotMatchesEverythingOption);
                QRegularExpressionMatch matchTl = reTl.match(cleanContent);
                if (matchTl.hasMatch())
                    resultText = matchTl.captured(1);
                else
                    resultText = cleanContent;

                resultText.remove("<tl>", Qt::CaseInsensitive);
                resultText.remove("</tl>", Qt::CaseInsensitive);

                resultText = thawEscapesLocal(resultText, escapeCtx);
                if (cfg.enable_glossary)
                    resultText = RegexManager::instance().processPost(resultText);

                // 2. 🚨执行终极标签克隆手术🚨：必须使用预处理后的干净文本(preText)作比对！
                // Response content is not rewritten based on source line counts.

                // 3. 保留 Z-Code 安全检查机制
                static const QRegularExpression zTagRegex("Z[A-Z]{2}Z");
                QSet<QString> sourceTags;
                QRegularExpressionMatchIterator j = zTagRegex.globalMatch(processedText);
                while (j.hasNext())
                    sourceTags.insert(j.next().captured());

                QString finalCleaned = resultText;
                QRegularExpressionMatchIterator k = zTagRegex.globalMatch(resultText);
                QSet<QString> outputTags;
                while (k.hasNext())
                    outputTags.insert(k.next().captured());

                for (const QString &tag : outputTags)
                {
                    if (!sourceTags.contains(tag))
                        finalCleaned.replace(tag, "");
                }
                resultText = finalCleaned;

                // ==========================================
                // 4. 🔄 Unity 竖排渲染标签重建 (Rotate Reconstruction)
                // 专门为翻译后的文本，逐个真实字符套回原本的旋转标签！
                // ==========================================
                if (hasRotate && !resultText.isEmpty())
                {
                    QString rewrapped;
                    // 精准匹配：忽略已存在的 HTML 标签、忽略空白和换行，只给实体字符穿戴！
                    QRegularExpression tokenMatcher(R"(<[^>]+>|\r?\n|\s+|.)", QRegularExpression::DotMatchesEverythingOption);
                    QRegularExpressionMatchIterator rit = tokenMatcher.globalMatch(resultText);
                    while (rit.hasNext())
                    {
                        QString token = rit.next().captured(0);
                        if (token.startsWith("<") || token.trimmed().isEmpty())
                        {
                            rewrapped += token; // 保持标签和空格原封不动
                        }
                        else
                        {
                            rewrapped += rotateOpenTag + token + "</rotate>"; // 重建竖排渲染！
                        }
                    }
                    resultText = rewrapped;
                }

                if (isValidTranslationResult(resultText))
                {
                    std::lock_guard<std::mutex> lock(m_contextMutex);
                    Context &ctx = m_contexts[clientId];
                    ctx.history.push_back({currentUserContent, resultText});
                    while (ctx.history.size() > ctx.max_len)
                        ctx.history.pop_front();
                }
                else
                {
                    resultText = QString();
                }
            }
            else
            {
                trace(requestId, attempt, "Response missing string choices[0].message.content");
                resultText = QString();
            }
        }
        catch (...)
        {
            trace(requestId, attempt, "Invalid response JSON or batch JSON");
            resultText = QString();
        }
    }
    else
    {
        trace(requestId, attempt, QString("HTTP %1 elapsed=%2ms network-error=%3")
              .arg(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()).arg(timer.elapsed()).arg(reply->errorString()));
        resultText = QString();
    }

    return resultText;
}

QString TranslationServer::getNextApiKey()
{
    std::lock_guard<std::mutex> lock(m_keyMutex);
    if (m_apiKeys.empty())
        return "";
    QString key = m_apiKeys[m_currentKeyIndex];
    m_currentKeyIndex = (m_currentKeyIndex + 1) % m_apiKeys.size();
    return key;
}

QString TranslationServer::generateClientId(const std::string &ip)
{
    QByteArray hash = QCryptographicHash::hash(QByteArray::fromStdString(ip), QCryptographicHash::Md5);
    return hash.toHex().left(8);
}

void TranslationServer::clearAllContexts()
{
    std::lock_guard<std::mutex> lock(m_contextMutex);
    m_contexts.clear();
    int langIdx = 1;
    {
        std::lock_guard<std::mutex> lock(m_configMutex);
        langIdx = m_config.language;
    }
    QString msg = (langIdx == 0) ? "<font color='#9C27B0'>🧹 Context memory cleared.</font>"
                                 : "<font color='#9C27B0'>🧹 上下文记忆已清空。</font>";
    emit logMessage(msg);
}
