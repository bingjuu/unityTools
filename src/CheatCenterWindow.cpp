#include "CheatCenterWindow.h"
#include "UnityValue.h"
#include "CheatPanelWidget.h"
#include "ConfigManager.h"
#include "ConsolePresentation.h"
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QTextBrowser>
#include <QTextDocument>
#include <QToolButton>
#include <QScrollBar>
#include <QTimer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
class ConsoleAnswerView final : public QTextBrowser
{
public:
    explicit ConsoleAnswerView(QWidget *parent) : QTextBrowser(parent)
    {
        setOpenExternalLinks(false);
        setOpenLinks(false);
    }
protected:
    QVariant loadResource(int, const QUrl &) override { return {}; }
};
}

CheatCenterWindow::CheatCenterWindow(QWidget *parent) : QDialog(parent)
{
    setObjectName("cheatCenterWindow");
    setModal(false);
    setAutoFillBackground(true);
    QPalette palette = this->palette();
    palette.setColor(QPalette::Window, QColor("#19191e"));
    palette.setColor(QPalette::WindowText, QColor("#f2f4f8"));
    palette.setColor(QPalette::Base, QColor("#25272d"));
    palette.setColor(QPalette::AlternateBase, QColor("#202228"));
    palette.setColor(QPalette::Text, QColor("#f2f4f8"));
    palette.setColor(QPalette::Button, QColor("#30343c"));
    palette.setColor(QPalette::ButtonText, QColor("#f2f4f8"));
    palette.setColor(QPalette::Highlight, QColor("#2f81f7"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    setPalette(palette);
    setStyleSheet(R"(
        QDialog#cheatCenterWindow, QDialog#cheatCenterWindow QWidget {
            background-color: #19191e;
            color: #f2f4f8;
        }
        QDialog#cheatCenterWindow QLabel {
            background-color: transparent;
            color: #f2f4f8;
        }
        QDialog#cheatCenterWindow QLineEdit,
        QDialog#cheatCenterWindow QComboBox,
        QDialog#cheatCenterWindow QPlainTextEdit,
        QDialog#cheatCenterWindow QTextEdit {
            background-color: #25272d;
            color: #f2f4f8;
            border: 1px solid #5b6472;
            border-radius: 4px;
            selection-background-color: #2f81f7;
            selection-color: #ffffff;
        }
        QDialog#cheatCenterWindow QLineEdit:focus,
        QDialog#cheatCenterWindow QComboBox:focus,
        QDialog#cheatCenterWindow QPlainTextEdit:focus,
        QDialog#cheatCenterWindow QTextEdit:focus {
            border: 1px solid #6ea8fe;
        }
        QDialog#cheatCenterWindow QLineEdit:disabled,
        QDialog#cheatCenterWindow QComboBox:disabled,
        QDialog#cheatCenterWindow QPlainTextEdit:disabled,
        QDialog#cheatCenterWindow QTextEdit:disabled {
            background-color: #202228;
            color: #9da6b3;
            border-color: #3e4652;
        }
        QDialog#cheatCenterWindow QPushButton {
            background-color: #30343c;
            color: #f2f4f8;
            border: 1px solid #687384;
            border-radius: 4px;
            padding: 5px 9px;
        }
        QDialog#cheatCenterWindow QPushButton:hover {
            background-color: #3c4655;
            border-color: #8bb8ff;
        }
        QDialog#cheatCenterWindow QPushButton:pressed {
            background-color: #255ea8;
            color: #ffffff;
        }
        QDialog#cheatCenterWindow QPushButton:disabled {
            background-color: #24272d;
            color: #8f99a6;
            border-color: #3d4550;
        }
        QDialog#cheatCenterWindow QCheckBox {
            background-color: transparent;
            color: #f2f4f8;
        }
        QDialog#cheatCenterWindow QTabWidget::pane {
            background-color: #19191e;
            border: 1px solid #424b58;
        }
        QDialog#cheatCenterWindow QTabBar::tab {
            background-color: #24272d;
            color: #c7ced8;
            border: 1px solid #424b58;
            padding: 7px 12px;
        }
        QDialog#cheatCenterWindow QTabBar::tab:selected {
            background-color: #3a4656;
            color: #ffffff;
        }
        QDialog#cheatCenterWindow QTableWidget {
            background-color: #202228;
            color: #f2f4f8;
            alternate-background-color: #252931;
            gridline-color: #414a57;
            border: 1px solid #505a68;
            selection-background-color: #2f81f7;
            selection-color: #ffffff;
        }
        QDialog#cheatCenterWindow QHeaderView::section {
            background-color: #30343c;
            color: #f2f4f8;
            border: 1px solid #4a5462;
            padding: 4px;
        }
        QDialog#cheatCenterWindow QLabel#writeEvidence {
            background-color: #22252b;
            color: #f2f4f8;
            border: 1px solid #596473;
            border-radius: 4px;
            padding: 6px;
        }
    )");
    resize(860, 720);
    auto *layout = new QVBoxLayout(this);
    m_client = new CheatBridgeClient(this);
    m_agent.setParent(this);
    m_agent.bindClient(m_client, {});
    m_status = new QLabel(tr("游戏未连接"), this);
    m_status->setObjectName("cheatSessionStatus");
    m_capabilityStatus = new QLabel(this); m_capabilityStatus->setWordWrap(true); m_capabilityStatus->setTextFormat(Qt::PlainText); m_capabilityStatus->setObjectName("capabilityStatus");
    m_translationStatus = new QLabel(tr("当前翻译状态未知；偏好变更仅下次启动生效。"), this);
    m_translationStatus->setWordWrap(true);
    auto *translationRow = new QHBoxLayout;
    m_translationPreference = new QCheckBox(tr("下次启动启用翻译（保留注入）"), this);
    translationRow->addWidget(m_translationPreference); translationRow->addWidget(m_translationStatus, 1);
    layout->addWidget(m_status); layout->addLayout(translationRow); layout->addWidget(m_capabilityStatus);
    m_tabs = new QTabWidget(this); auto *tabs = m_tabs; layout->addWidget(tabs, 1);
    auto *objectPage = new QWidget(tabs); auto *objects = new QVBoxLayout(objectPage);
    tabs->addTab(objectPage, tr("Unity 数据"));
    auto *findRow = new QHBoxLayout;
    m_type = new QLineEdit(objectPage); m_type->setObjectName("objectType"); m_type->setPlaceholderText(tr("类型名；空白可浏览已加载场景对象"));
    m_find = new QPushButton(tr("查找"), objectPage); m_find->setObjectName("objectFind");
    m_candidates = new QComboBox(objectPage); m_candidates->setObjectName("objectCandidates");
    m_candidates->setMinimumContentsLength(12); m_candidates->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    findRow->addWidget(m_type, 1); findRow->addWidget(m_find); findRow->addWidget(m_candidates, 1); objects->addLayout(findRow);
    m_members = new QTableWidget(objectPage); m_members->setObjectName("objectMembers"); m_members->setColumnCount(3);
    m_members->setHorizontalHeaderLabels({tr("成员/条目"), tr("声明类型"), tr("可写")});
    m_members->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_members->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_members->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents); m_members->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_members->setEditTriggers(QAbstractItemView::NoEditTriggers); objects->addWidget(m_members, 1);
    auto *edit = new QFormLayout;
    m_member = new QLineEdit(objectPage); m_member->setObjectName("objectMember");
    m_member->setPlaceholderText(tr("成员名或 JSON 路径，如 [\"data\",\"cash\"]、[\"items\",{\"key\":\"coins\"}]"));
    m_value = new QLineEdit(objectPage); m_value->setObjectName("objectValue");
    m_nullValue = new QCheckBox("null", objectPage);
    m_operation = new QComboBox(objectPage); m_operation->addItem(tr("设置"), "set"); m_operation->addItem(tr("增加"), "add");
    m_read = new QPushButton(tr("读取"), objectPage); m_read->setObjectName("objectRead");
    m_write = new QPushButton(tr("写入并读回"), objectPage); m_write->setObjectName("objectWrite");
    auto *valueRow = new QHBoxLayout; valueRow->addWidget(m_value, 1); valueRow->addWidget(m_nullValue); valueRow->addWidget(m_operation); valueRow->addWidget(m_read); valueRow->addWidget(m_write);
    edit->addRow(tr("选定成员 / 路径"), m_member); edit->addRow(tr("目标值"), valueRow); objects->addLayout(edit);
    m_evidence = new QLabel(objectPage); m_evidence->setObjectName("writeEvidence"); m_evidence->setWordWrap(true); m_evidence->setTextFormat(Qt::PlainText); objects->addWidget(m_evidence);
    auto *methodRow = new QHBoxLayout;
    m_methods = new QComboBox(objectPage); m_methods->setMinimumContentsLength(10); m_methods->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_methodArguments = new QLineEdit(objectPage); m_methodArguments->setPlaceholderText(tr("参数 JSON 数组 [{\"text\":\"1\"}]"));
    m_invoke = new QPushButton(tr("调用已发现方法"), objectPage);
    methodRow->addWidget(m_methods, 1); methodRow->addWidget(m_methodArguments, 1); methodRow->addWidget(m_invoke); objects->addLayout(methodRow);
    m_panel = new CheatPanelWidget(tabs); m_panel->setClient(m_client); tabs->addTab(m_panel, tr("游戏数据 / 命令入口"));
    connect(m_panel, &CheatPanelWidget::capabilitiesReceived, this, &CheatCenterWindow::setCapabilities);
    auto *agentPage = new QWidget(tabs); auto *agentLayout = new QVBoxLayout(agentPage); tabs->addTab(agentPage, "Unity Agent");
    auto *settings = new QFormLayout;
    m_agentAddress = new QLineEdit(agentPage); m_agentAddress->setObjectName("agentApiAddress"); m_agentAddress->setPlaceholderText("http://127.0.0.1:8080/v1");
    m_agentKey = new QLineEdit(agentPage); m_agentKey->setObjectName("agentApiKey"); m_agentKey->setEchoMode(QLineEdit::Password);
    m_agentModel = new QLineEdit(agentPage); m_agentModel->setObjectName("agentModel");
    settings->addRow(tr("Agent API 地址"), m_agentAddress); settings->addRow(tr("Agent API 密钥"), m_agentKey); settings->addRow(tr("支持工具的模型"), m_agentModel); agentLayout->addLayout(settings);
    auto *notice = new QLabel(tr("远端模型会收到当前任务相关对象名称、成员和值；不会上传存档或完整对象图。Agent 独立于翻译配置，Sakura 翻译模型不作为工具模型。"), agentPage); notice->setWordWrap(true); agentLayout->addWidget(notice);
    auto *save = new QPushButton(tr("保存 Agent 配置"), agentPage); agentLayout->addWidget(save);
    auto *agentRow = new QHBoxLayout;
    auto *request = new QLineEdit(agentPage); request->setObjectName("agentRequest"); request->setPlaceholderText(tr("例如：把金钱改到9999"));
    m_agentRun = new QPushButton(tr("执行 Unity 任务"), agentPage); m_agentRun->setObjectName("agentRun");
    auto *stop = new QPushButton(tr("停止后续操作"), agentPage); stop->setObjectName("agentStop");
    agentRow->addWidget(request, 1); agentRow->addWidget(m_agentRun); agentRow->addWidget(stop); agentLayout->addLayout(agentRow);
    auto *selection = new QHBoxLayout;
    m_agentCandidates = new QComboBox(agentPage); m_agentCandidates->setObjectName("agentCandidates");
    m_agentSelect = new QPushButton(tr("选定此目标继续"), agentPage); m_agentSelect->setObjectName("agentSelect");
    selection->addWidget(m_agentCandidates, 1); selection->addWidget(m_agentSelect); agentLayout->addLayout(selection);
    m_agentStatus = new QLabel(agentPage); m_agentStatus->setObjectName("agentStatus"); m_agentStatus->setTextFormat(Qt::PlainText);
    m_agentEvidence = new QLabel(agentPage); m_agentEvidence->setObjectName("agentEvidence"); m_agentEvidence->setWordWrap(true); m_agentEvidence->setTextFormat(Qt::PlainText);
    agentLayout->addWidget(m_agentStatus); agentLayout->addWidget(m_agentEvidence);
    m_agentLog = new ConsoleAnswerView(agentPage); m_agentLog->setObjectName("agentLog"); m_agentLog->setReadOnly(true);
    m_agentLog->document()->setDefaultStyleSheet(QStringLiteral("table { border-collapse: collapse; } td, th { border: 1px solid #687384; padding: 6px; } a { color: #9cc7ff; }"));
    agentLayout->addWidget(m_agentLog, 1);
    auto *details = new QToolButton(agentPage); details->setObjectName("agentDetailsToggle"); details->setText(tr("详细信息")); details->setCheckable(true); details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); details->setArrowType(Qt::RightArrow);
    m_agentDetails = new QPlainTextEdit(agentPage); m_agentDetails->setObjectName("agentDetails"); m_agentDetails->setReadOnly(true); m_agentDetails->setMaximumHeight(160); m_agentDetails->hide();
    agentLayout->addWidget(details); agentLayout->addWidget(m_agentDetails);
    connect(details, &QToolButton::toggled, this, [this, details](bool open) { m_agentDetails->setVisible(open); details->setArrowType(open ? Qt::DownArrow : Qt::RightArrow); });
    connect(m_find, &QPushButton::clicked, this, [this] { clearSelection(); send("/objects/find", "find", {{"type", m_type->text()}}); });
    connect(m_candidates, QOverload<int>::of(&QComboBox::activated), this, &CheatCenterWindow::selectObject);
    connect(m_members, &QTableWidget::cellClicked, this, [this](int row, int) { if (auto *item = m_members->item(row, 0)) m_member->setText(item->text()); });
    connect(m_member, &QLineEdit::textChanged, this, [this] { ++m_selection; m_snapshotId.clear(); m_evidence->clear(); updateObjectControls(); });
    connect(m_read, &QPushButton::clicked, this, [this] { send("/objects/read", "read", memberArguments()); });
    connect(m_write, &QPushButton::clicked, this, [this] {
        auto args = memberArguments();
        args["snapshotId"] = m_snapshotId; args["operation"] = m_operation->currentData().toString();
        args["value"] = m_nullValue->isChecked() ? QJsonObject{{"null", true}} : QJsonObject{{"text", m_value->text()}};
        m_writeEvidence = {{"before", m_lastRead}, {"requested", args["value"]}};
        send("/objects/write", "write", args);
    });
    connect(m_invoke, &QPushButton::clicked, this, [this] {
        const auto method = m_methods->currentData().toJsonObject();
        QJsonParseError parse; const auto args = QJsonDocument::fromJson(m_methodArguments->text().toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !args.isArray()) { m_evidence->setText(tr("方法参数必须是 JSON 数组")); return; }
        send("/objects/invoke", "invoke", {{"objectId", m_selectedObject}, {"methodId", method["methodId"]}, {"parameterTypes", method["parameters"]}, {"arguments", args.array()}});
    });
    connect(save, &QPushButton::clicked, this, &CheatCenterWindow::saveAgentConfiguration);
    connect(m_agentRun, &QPushButton::clicked, this, [this, request] {
        m_agent.configure(m_agentAddress->text(), m_agentKey->text(), m_agentModel->text());
        resetAgentView(); m_agent.start(request->text(), m_client->session());
    });
    connect(stop, &QPushButton::clicked, &m_agent, QOverload<>::of(&UnityAgentController::cancel));
    connect(m_agentSelect, &QPushButton::clicked, this, [this] { m_agent.chooseCandidate(m_agentCandidates->currentData().toString()); });
    connect(&m_agent, &UnityAgentController::stateChanged, this, [this](const QString &state) { m_agentState = state; m_agentStatus->setText(consoleui::state(state, m_lang)); });
    connect(&m_agent, &UnityAgentController::assistantResponse, this, &CheatCenterWindow::showAssistantResponse);
    connect(&m_agent, &UnityAgentController::message, this, [this](const QString &error) { m_agentStatus->setText(consoleui::error(error, m_lang)); m_agentDetails->appendPlainText(error); });
    connect(&m_agent, &UnityAgentController::evidenceChanged, this, &CheatCenterWindow::showAgentEvidence);
    connect(&m_agent, &UnityAgentController::selectionRequired, this, [this](const QList<QJsonObject> &rows) {
        m_agentCandidates->clear();
        for (const auto &row : rows) m_agentCandidates->addItem(row.value("type").toString() + " · " + row.value("path").toString() + " · " + row.value("name").toString(), row.value("selectionId").toString(row.value("objectId").toString()));
        m_agentCandidates->setEnabled(true); m_agentSelect->setEnabled(!rows.isEmpty());
    });
    connect(m_translationPreference, &QCheckBox::toggled, this, [this](bool enabled) { emit translationPreferenceChanged(enabled); setTranslationPreference(enabled); });
    connect(m_client, &CheatBridgeClient::completed, this, &CheatCenterWindow::handle);
    connect(m_client, &CheatBridgeClient::failed, this, [this](const QString &id, const QString &, const QString &error) {
        if (!m_pending.contains(id)) return;
        const auto operation = m_pending.take(id);
        if (operation.selection != m_selection && operation.stage != "capabilities") return;
        if (operation.stage == "write") {
            m_uncertainMutation = id; m_mutationArguments = operation.arguments;
            m_writeEvidence["state"] = "unknown"; renderEvidence(m_writeEvidence);
            send("/requests/status", "status", {{"requestId", id}}); return;
        }
        m_mutationPending = false;
        m_snapshotId.clear(); m_evidence->setText((operation.stage == "write" || operation.stage == "verify" || operation.stage == "invoke" ? consoleui::state("unknown", m_lang) + "。" : "") + consoleui::error(error, m_lang));
        updateObjectControls();
    });
    clearObjectView(); updateLanguage(1);
}
void CheatCenterWindow::saveAgentConfiguration()
{
    auto config = ConfigManager::loadConfig(); config.agent_api_address = m_agentAddress->text(); config.agent_api_key = m_agentKey->text(); config.agent_model = m_agentModel->text();
    ConfigManager::saveConfig(config); m_agent.configure(config.agent_api_address, config.agent_api_key, config.agent_model); m_agentStatus->setText(consoleui::text(m_lang, "Agent 配置已保存。", "Agent configuration saved."));
}
void CheatCenterWindow::clearSelection()
{
    const auto oldObject = m_selectedObject;
    const auto oldSnapshot = m_snapshotId;
    ++m_selection; ++m_readSerial; m_mutationPending = false; m_uncertainMutation.clear(); m_mutationArguments = {};
    m_selectedObject.clear(); m_snapshotId.clear(); m_lastRead = {}; m_inspection = {};
    if (m_client && m_client->isConnected() && (!oldObject.isEmpty() || !oldSnapshot.isEmpty()))
        m_client->releaseResources(oldObject.isEmpty() ? QStringList{} : QStringList{oldObject}, oldSnapshot.isEmpty() ? QStringList{} : QStringList{oldSnapshot});
    m_members->setRowCount(0); m_methods->clear(); m_member->clear(); m_value->clear(); m_nullValue->setChecked(false); m_evidence->clear(); updateObjectControls();
}
void CheatCenterWindow::clearObjectView()
{
    clearSelection(); QSignalBlocker blocker(m_candidates); m_candidates->clear(); m_candidates->setEnabled(false);
    m_agentCandidates->clear(); m_agentCandidates->setEnabled(false); m_agentSelect->setEnabled(false); resetAgentView(); m_pending.clear();
}
void CheatCenterWindow::openSession(const GameSession &session)
{
    m_agent.cancel(); clearObjectView(); m_client->openSession(session); m_capabilities = {}; m_agent.setCapabilities({});
    m_capabilityStatus->clear(); m_status->setText(QString("PID %1 · %2 · %3").arg(session.pid).arg(session.runtime, tr("读取能力中")));
    send("/capabilities", "capabilities"); send("/translation/state", "translation");
    show(); raise(); activateWindow();
}
void CheatCenterWindow::closeSession(const QString &reason)
{
    m_agent.cancel(); m_client->cancelSession(); m_panel->clearSession(); m_capabilities = {}; m_agent.setCapabilities({}); clearObjectView();
    m_capabilityStatus->clear(); m_status->setText(consoleui::text(m_lang, "游戏控制连接已断开", "Game control disconnected")); m_status->setToolTip(reason); m_translationStatus->setText(consoleui::text(m_lang, "当前会话已断开；翻译偏好下次启动生效。", "Session disconnected; translation preference applies next launch.")); updateObjectControls();
}
void CheatCenterWindow::send(const QString &path, const QString &stage, const QJsonObject &args)
{
    if (stage == "read" || stage == "verify" || stage == "reconcile") ++m_readSerial;
    if (stage == "write") { m_mutationPending = true; m_mutationArguments = args; ++m_readSerial; updateObjectControls(); }
    const auto id = m_client->request(path, path == "/capabilities" || path == "/translation/state" ? "GET" : "POST", args);
    if (!id.isEmpty()) m_pending.insert(id, {m_selection, m_readSerial, stage, args});
}
QJsonObject CheatCenterWindow::memberArguments() const
{
    QJsonObject args{{"objectId", m_selectedObject}};
    if (m_member->text().trimmed().startsWith('[')) {
        const auto path = QJsonDocument::fromJson(m_member->text().toUtf8());
        if (path.isArray()) args["path"] = path.array();
    } else args["member"] = m_member->text();
    return args;
}
void CheatCenterWindow::selectObject(int index)
{
    if (index < 0) return;
    const auto id = m_candidates->itemData(index).toString();
    clearSelection(); m_selectedObject = id;
    send("/objects/inspect", "inspect", {{"objectId", id}});
}
void CheatCenterWindow::handle(const QString &id, const QString &, const QJsonObject &envelope)
{
    if (!m_pending.contains(id)) return;
    const auto pending = m_pending.take(id);
    if (pending.stage != "capabilities" && pending.stage != "translation" && pending.selection != m_selection) return;
    if ((pending.stage == "read" || pending.stage == "verify" || pending.stage == "reconcile") && pending.readSerial != m_readSerial) {
        const auto snapshot = envelope.value("result").toObject().value("snapshotId").toString();
        if (!snapshot.isEmpty()) m_client->releaseResources({}, {snapshot});
        return;
    }
    const auto result = envelope["result"].toObject();
    const auto executionState = envelope.value("state").toString();
    if (pending.stage == "write" && (executionState == "unknown" || executionState == "executed-unverified")) {
        m_uncertainMutation = id; m_mutationArguments = pending.arguments; m_writeEvidence["state"] = executionState;
        renderEvidence(m_writeEvidence); send("/requests/status", "status", {{"requestId", id}}); return;
    }
    const auto error = envelope["error"].toString(result["error"].toString());
    if (!error.isEmpty() || envelope["state"] == "not-executed") {
        if (pending.stage == "translation") { m_translationStatus->setText(tr("当前翻译状态未知；下次启动生效。") + " " + error); return; }
        m_mutationPending = false; m_snapshotId.clear(); m_evidence->setText(consoleui::error(error, m_lang)); updateObjectControls(); return;
    }
    if (pending.stage == "capabilities") { setCapabilities(result); return; }
    if (pending.stage == "translation") {
        m_translationStatus->setText(tr("当前会话：%1；偏好变更仅下次启动生效。").arg(result["enabled"].isBool() ? result["enabled"].toBool() ? tr("开启") : tr("关闭") : tr("未部署/未知"))); return;
    }
    if (pending.stage == "find") {
        QSignalBlocker blocker(m_candidates); m_candidates->clear();
        const auto rows = result["candidates"].toArray();
        for (const auto &item : rows) { const auto row = item.toObject(); m_candidates->addItem(row["type"].toString() + " · " + row["path"].toString() + " · " + row["name"].toString(), row["objectId"].toString()); }
        m_candidates->setEnabled(!rows.isEmpty());
        if (rows.size() == 1) selectObject(0);
        else m_candidates->setCurrentIndex(-1);
        return;
    }
    if (pending.stage == "inspect") {
        m_inspection = result;
        for (const auto &item : result["members"].toArray()) {
            const auto member = item.toObject(); const int row = m_members->rowCount(); m_members->insertRow(row);
            m_members->setItem(row, 0, new QTableWidgetItem(member["name"].toString())); m_members->setItem(row, 1, new QTableWidgetItem(member["type"].toString())); m_members->setItem(row, 2, new QTableWidgetItem(member["writable"].toBool() ? consoleui::text(m_lang, "是", "yes") : consoleui::text(m_lang, "否", "no")));
        }
        for (const auto &item : result["entries"].toArray()) {
            const auto entry = item.toObject(); const int row = m_members->rowCount(); m_members->insertRow(row);
            const auto step = entry.contains("key") ? QJsonObject{{"key", entry["key"]}} : QJsonObject{{"index", entry["index"]}};
            m_members->setItem(row, 0, new QTableWidgetItem(QString::fromUtf8(QJsonDocument(QJsonArray{step}).toJson(QJsonDocument::Compact))));
            m_members->setItem(row, 1, new QTableWidgetItem(entry["value"].toObject()["type"].toString())); m_members->setItem(row, 2, new QTableWidgetItem(consoleui::text(m_lang, "是", "yes")));
        }
        for (const auto &method : result["methods"].toArray()) m_methods->addItem(method.toObject()["name"].toString() + " " + QString::fromUtf8(QJsonDocument(method.toObject()["parameters"].toArray()).toJson(QJsonDocument::Compact)), method.toObject());
        if (m_members->rowCount() == 1) m_member->setText(m_members->item(0, 0)->text());
        updateObjectControls(); return;
    }
    if (pending.stage == "read") {
        if (!m_snapshotId.isEmpty()) m_client->releaseResources({}, {m_snapshotId});
        m_snapshotId = result["snapshotId"].toString(); m_lastRead = result; m_value->setText(result["text"].toString()); m_nullValue->setChecked(result["null"].toBool());
        if (result.contains("objectId")) { m_evidence->setText(tr("对象引用：") + result["objectId"].toString() + tr("（可在路径中继续读取）")); }
        else m_evidence->setText(tr("读取前值：") + result["text"].toString()); updateObjectControls(); return;
    }
    if (pending.stage == "status") {
        if (result.value("executing").toBool()) {
            QTimer::singleShot(250, this, [this, selection = m_selection] {
                if (selection == m_selection && m_client->isConnected() && !m_uncertainMutation.isEmpty()) send("/requests/status", "status", {{"requestId", m_uncertainMutation}});
            });
            return;
        }
        const auto outcome = result.value("outcome").toObject();
        const auto outcomeResult = outcome.value("result").toObject();
        if (!m_uncertainMutation.isEmpty()) m_client->acknowledge(m_uncertainMutation);
        if (outcomeResult.contains("target")) m_writeEvidence["target"] = outcomeResult.value("target");
        m_writeEvidence["state"] = outcome.value("state").toString("unknown");
        QJsonObject args{{"objectId", m_mutationArguments.value("objectId")}};
        if (m_mutationArguments.contains("path")) args["path"] = m_mutationArguments.value("path"); else args["member"] = m_mutationArguments.value("member");
        send("/objects/read", outcome.value("state") == "verified" ? "verify" : "reconcile", args); return;
    }
    if (pending.stage == "reconcile") {
        m_writeEvidence["after"] = result; m_writeEvidence["state"] = "unknown"; m_mutationPending = false;
        renderEvidence(m_writeEvidence); updateObjectControls(); return;
    }
    if (pending.stage == "write") {
        m_writeEvidence["target"] = result["target"]; m_writeEvidence["after"] = result["after"]; m_writeEvidence["state"] = envelope["state"];
        renderEvidence(m_writeEvidence);
        if (envelope["state"] == "verified" || result["state"] == "verified") {
            QJsonObject args{{"objectId", pending.arguments["objectId"]}};
            if (pending.arguments.contains("path")) args["path"] = pending.arguments["path"]; else args["member"] = pending.arguments["member"];
            send("/objects/read", "verify", args);
        }
        return;
    }
    if (pending.stage == "verify") {
        m_writeEvidence["after"] = result; m_writeEvidence["state"] = sameTypedUnityValue(m_writeEvidence["target"].toObject(), result) ? "verified" : "executed-unverified";
        m_mutationPending = false; m_snapshotId = result["snapshotId"].toString(); renderEvidence(m_writeEvidence); updateObjectControls(); return;
    }
    if (pending.stage == "invoke") {
        const auto status = envelope.value("state").toString(result.value("state").toString());
        m_evidence->setText(consoleui::state(status, m_lang));
        return;
    }
}
void CheatCenterWindow::renderEvidence(const QJsonObject &value)
{
    m_evidence->setText(consoleui::evidenceSummary(value, m_lang));
}
void CheatCenterWindow::updateObjectControls()
{
    const bool connected = m_client->isConnected();
    const bool selected = connected && !m_selectedObject.isEmpty();
    m_find->setEnabled(connected && m_capabilities["objects"].toObject()["status"] == "available");
    m_read->setEnabled(selected && !m_member->text().isEmpty());
    m_write->setEnabled(selected && !m_snapshotId.isEmpty() && !m_mutationPending && m_capabilities.value("members.write").toObject().value("status") == "available");
    m_invoke->setEnabled(selected && m_methods->count() > 0);
    m_agentRun->setEnabled(connected && !m_capabilities.isEmpty());
}
void CheatCenterWindow::setCapabilities(const QJsonObject &capabilities)
{
    m_capabilities = capabilities; m_agent.setCapabilities(capabilities);
    QStringList lines;
    for (auto it = capabilities.begin(); it != capabilities.end(); ++it) if (it.value().isObject()) lines << it.key() + ": " + it.value().toObject()["status"].toString() + " " + it.value().toObject()["reason"].toString();
    m_capabilityStatus->setText(consoleui::capabilitySummary(capabilities, m_lang));
    m_capabilityStatus->setToolTip(lines.join("\n"));
    m_status->setText(QString("PID %1 · %2 · %3").arg(m_client->session().pid).arg(m_client->session().runtime, tr("控制已连接")));
    m_panel->setCapabilities(capabilities); updateObjectControls();
}
void CheatCenterWindow::setTranslationPreference(bool enabled)
{
    QSignalBlocker blocker(m_translationPreference); m_translationPreference->setChecked(enabled);
}
void CheatCenterWindow::setAgentConfiguration(const QString &address, const QString &key, const QString &model)
{
    m_agentAddress->setText(address); m_agentKey->setText(key); m_agentModel->setText(model); m_agent.configure(address, key, model);
}
void CheatCenterWindow::updateLanguage(int lang)
{
    m_lang = lang;
    const bool zh = lang == 1;
    setWindowTitle(zh ? QStringLiteral("UnityTools 游戏控制台") : QStringLiteral("UnityTools Game Console"));
    m_status->setText(m_client->isConnected()
        ? QStringLiteral("PID %1 · %2 · %3").arg(m_client->session().pid).arg(m_client->session().runtime, zh ? QStringLiteral("控制已连接") : QStringLiteral("Control connected"))
        : (zh ? QStringLiteral("游戏未连接") : QStringLiteral("Game disconnected")));
    m_translationPreference->setText(zh ? QStringLiteral("下次启动启用翻译（保留注入）") : QStringLiteral("Enable translation next launch (keep injection)"));
    m_find->setText(zh ? QStringLiteral("查找") : QStringLiteral("Find"));
    m_read->setText(zh ? QStringLiteral("读取") : QStringLiteral("Read"));
    m_write->setText(zh ? QStringLiteral("写入并读回") : QStringLiteral("Write and verify"));
    m_invoke->setText(zh ? QStringLiteral("调用已发现方法") : QStringLiteral("Invoke observed method"));
    m_members->setHorizontalHeaderLabels(zh ? QStringList{QStringLiteral("成员/条目"), QStringLiteral("声明类型"), QStringLiteral("可写")}
                                                  : QStringList{QStringLiteral("Member/entry"), QStringLiteral("Declared type"), QStringLiteral("Writable")});
    m_tabs->setTabText(0, zh ? QStringLiteral("Unity 数据") : QStringLiteral("Unity data"));
    m_tabs->setTabText(1, zh ? QStringLiteral("游戏数据 / 命令入口") : QStringLiteral("Game data / commands"));
    m_tabs->setTabText(2, QStringLiteral("Unity Agent"));
    m_operation->setItemText(0, zh ? QStringLiteral("设置") : QStringLiteral("Set"));
    m_operation->setItemText(1, zh ? QStringLiteral("增加") : QStringLiteral("Add"));
    m_agentRun->setText(zh ? QStringLiteral("执行 Unity 任务") : QStringLiteral("Run Unity task"));
    m_agentSelect->setText(zh ? QStringLiteral("选定此目标继续") : QStringLiteral("Select target and continue"));
    m_panel->updateLanguage(lang);
    m_agentStatus->setText(consoleui::state(m_agentState, lang));
    if (!m_agentEvidenceValue.isEmpty()) m_agentEvidence->setText(consoleui::evidenceSummary(m_agentEvidenceValue, lang));
    if (!m_capabilities.isEmpty()) m_capabilityStatus->setText(consoleui::capabilitySummary(m_capabilities, lang));
    if (auto *button = findChild<QToolButton *>("agentDetailsToggle")) button->setText(consoleui::text(lang, "详细信息", "Details"));
}
QString CheatCenterWindow::agentTaskState() const { return m_agent.currentState(); }
bool CheatCenterWindow::isSessionConnected() const { return m_client->isConnected(); }
void CheatCenterWindow::setPlayData(const QList<QJsonObject> &rows) { m_panel->setPlayData(rows); }
int CheatCenterWindow::playDataCount() const { return m_panel->playDataCount(); }

void CheatCenterWindow::resetAgentView()
{
    m_agentState.clear(); m_agentEvidenceValue = {};
    m_agentLog->clear(); m_agentDetails->clear(); m_agentEvidence->clear(); m_agentStatus->setText(consoleui::state({}, m_lang));
}
void CheatCenterWindow::showAssistantResponse(const QString &markdown)
{
    m_agentLog->document()->setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    m_agentLog->verticalScrollBar()->setValue(0);
}
void CheatCenterWindow::showAgentEvidence(const QJsonObject &value)
{
    m_agentEvidenceValue = value;
    m_agentEvidence->setText(consoleui::evidenceSummary(value, m_lang));
    m_agentDetails->setPlainText(QString::fromUtf8(QJsonDocument(value).toJson(QJsonDocument::Indented)));
}
