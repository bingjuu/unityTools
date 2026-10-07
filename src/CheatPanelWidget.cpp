#include "CheatPanelWidget.h"
#include "CheatBridgeClient.h"
#include "ConsolePresentation.h"
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

CheatPanelWidget::CheatPanelWidget(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *entries = new QFormLayout;
    m_provider = new QComboBox(this);
    m_provider->setObjectName("playDataProvider");
    m_console = new QComboBox(this);
    m_console->setObjectName("gameConsoleSelector");
    m_providerLabel = new QLabel(this);
    m_consoleLabel = new QLabel(this);
    entries->addRow(m_providerLabel, m_provider);
    entries->addRow(m_consoleLabel, m_console);
    layout->addLayout(entries);
    auto *row = new QHBoxLayout;
    m_cmdEdit = new QLineEdit(this);
    m_cmdEdit->setObjectName("gameCommandText");
    m_sendBtn = new QPushButton(this);
    m_sendBtn->setObjectName("gameCommandSend");
    m_refreshBtn = new QPushButton(this);
    m_refreshBtn->setObjectName("playDataRefresh");
    m_entriesBtn = new QPushButton(this);
    m_entriesBtn->setObjectName("gameEntriesRefresh");
    row->addWidget(m_cmdEdit, 1);
    row->addWidget(m_sendBtn);
    row->addWidget(m_refreshBtn);
    row->addWidget(m_entriesBtn);
    layout->addLayout(row);
    m_table = new QTableWidget(this);
    m_table->setObjectName("playDataTable");
    m_table->setColumnCount(3);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    layout->addWidget(m_table, 1);
    m_status = new QLabel(this);
    m_status->setObjectName("gameEntriesStatus");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    connect(m_refreshBtn, &QPushButton::clicked, this, &CheatPanelWidget::refreshPlayData);
    connect(m_sendBtn, &QPushButton::clicked, this, &CheatPanelWidget::sendCommand);
    connect(m_cmdEdit, &QLineEdit::returnPressed, this, &CheatPanelWidget::sendCommand);
    connect(m_entriesBtn, &QPushButton::clicked, this, [this] {
        if (m_client && m_pendingCapabilities.isEmpty()) {
            m_pendingCapabilities = m_client->request("/capabilities", "GET");
            updateControls();
        }
    });
    connect(m_provider, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        if (m_client && !m_pendingRead.isEmpty()) m_client->cancelRequest(m_pendingRead);
        m_pendingRead.clear();
        m_table->setRowCount(0);
        updateControls();
        emit entrySelectionChanged(selectedProviderId(), selectedConsoleId());
    });
    connect(m_console, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        updateControls();
        emit entrySelectionChanged(selectedProviderId(), selectedConsoleId());
    });
    updateLanguage(1);
    updateControls();
}

void CheatPanelWidget::setClient(CheatBridgeClient *client)
{
    if (m_client) disconnect(m_client, nullptr, this, nullptr);
    m_client = client;
    clearSession();
    if (!client) return;
    connect(client, &CheatBridgeClient::sessionChanged, this, &CheatPanelWidget::clearSession);
    connect(client, &CheatBridgeClient::completed, this, [this](const QString &id, const QString &path, const QJsonObject &envelope) {
        if (id != m_pendingRead && id != m_pendingCommand && id != m_pendingCapabilities) return;
        if (id == m_pendingRead) m_pendingRead.clear();
        if (id == m_pendingCommand) m_pendingCommand.clear();
        if (id == m_pendingCapabilities) m_pendingCapabilities.clear();
        const auto result = envelope.value("result").toObject();
        const auto error = envelope.value("error").toString(result.value("error").toString());
        if (!error.isEmpty()) {
            if (path == "/playdata") m_table->setRowCount(0);
            m_status->setText(consoleui::error(error, m_lang));
            updateControls();
            return;
        }
        if (path == "/capabilities") {
            emit capabilitiesReceived(result);
        } else if (path == "/playdata") {
            QList<QJsonObject> rows;
            for (const auto &item : result.value("rows").toArray()) rows.append(item.toObject());
            setPlayData(rows);
            m_status->setText(consoleui::text(m_lang, "%1 项；这里只查看数据快照，修改请使用 Unity 数据页。", "%1 entries; snapshot only. Use Unity data to change live values.").arg(rows.size()));
        } else {
            m_status->setText(consoleui::state(result.value("state").toString(envelope.value("state").toString()), m_lang));
        }
        updateControls();
    });
    connect(client, &CheatBridgeClient::failed, this, [this](const QString &id, const QString &path, const QString &error) {
        if (id != m_pendingRead && id != m_pendingCommand && id != m_pendingCapabilities) return;
        if (id == m_pendingRead) { m_pendingRead.clear(); m_table->setRowCount(0); }
        if (id == m_pendingCommand) m_pendingCommand.clear();
        if (id == m_pendingCapabilities) m_pendingCapabilities.clear();
        m_status->setText(path == "/cmd" ? consoleui::state("unknown", m_lang) : consoleui::error(error, m_lang));
        updateControls();
    });
    updateControls();
}

void CheatPanelWidget::updateLanguage(int lang)
{
    m_lang = lang;
    m_providerLabel->setText(consoleui::text(lang, "数据来源", "Data provider"));
    m_consoleLabel->setText(consoleui::text(lang, "命令入口", "Command entry"));
    m_provider->setPlaceholderText(consoleui::text(lang, "请选择数据来源", "Select a data provider"));
    m_console->setPlaceholderText(consoleui::text(lang, "请选择命令入口", "Select a command entry"));
    m_refreshBtn->setText(consoleui::text(lang, "读取数据", "Read data"));
    m_entriesBtn->setText(consoleui::text(lang, "刷新入口", "Refresh entries"));
    m_sendBtn->setText(consoleui::text(lang, "提交命令", "Submit"));
    m_cmdEdit->setPlaceholderText(consoleui::text(lang, "使用选定入口的游戏命令", "Command for the selected game entry"));
    m_table->setHorizontalHeaderLabels(lang == 1 ? QStringList{QStringLiteral("键"), QStringLiteral("值"), QStringLiteral("类型")} : QStringList{"Key", "Value", "Type"});
}

void CheatPanelWidget::setCapabilities(const QJsonObject &capabilities)
{
    m_capabilities = capabilities;
    const auto fill = [](QComboBox *box, const QJsonArray &rows, const QString &key) {
        const auto selected = box->currentData().toString();
        QSignalBlocker blocker(box);
        box->clear();
        for (const auto &item : rows) {
            const auto row = item.toObject();
            box->addItem(row.value("type").toString() + " · " + row.value("source").toString(), row.value(key).toString());
        }
        const int prior = box->findData(selected);
        box->setCurrentIndex(prior >= 0 ? prior : box->count() == 1 ? 0 : -1);
    };
    const auto oldProvider = selectedProviderId();
    fill(m_provider, capabilities.value("playDataProviders").toArray(), "providerId");
    fill(m_console, capabilities.value("gameConsoles").toArray(), "consoleId");
    if (oldProvider != selectedProviderId()) {
        if (m_client && !m_pendingRead.isEmpty()) m_client->cancelRequest(m_pendingRead);
        m_pendingRead.clear();
        m_table->setRowCount(0);
    }
    updateControls();
    emit entrySelectionChanged(selectedProviderId(), selectedConsoleId());
    if (!selectedProviderId().isEmpty()) refreshPlayData();
}

void CheatPanelWidget::updateControls()
{
    const bool connected = m_client && m_client->isConnected();
    const bool data = connected && m_capabilities.value("playData").toObject().value("status") == "available";
    const bool console = connected && m_capabilities.value("gameConsole").toObject().value("status") == "available";
    m_provider->setEnabled(data && m_provider->count() > 0);
    m_console->setEnabled(console && m_console->count() > 0);
    m_refreshBtn->setEnabled(data && !selectedProviderId().isEmpty() && m_pendingRead.isEmpty());
    m_sendBtn->setEnabled(console && !selectedConsoleId().isEmpty() && m_pendingCommand.isEmpty());
    m_cmdEdit->setEnabled(console && !selectedConsoleId().isEmpty());
    m_entriesBtn->setEnabled(connected && m_pendingCapabilities.isEmpty());
}

void CheatPanelWidget::clearSession()
{
    m_capabilities = {};
    m_pendingRead.clear(); m_pendingCommand.clear(); m_pendingCapabilities.clear();
    QSignalBlocker providerBlocker(m_provider), consoleBlocker(m_console);
    m_provider->clear(); m_console->clear();
    m_table->setRowCount(0);
    m_cmdEdit->clear(); m_status->clear();
    updateControls();
    emit entrySelectionChanged({}, {});
}

void CheatPanelWidget::setPlayData(const QList<QJsonObject> &rows)
{
    m_table->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        m_table->setItem(i, 0, new QTableWidgetItem(rows[i].value("key").toString()));
        m_table->setItem(i, 1, new QTableWidgetItem(rows[i].value("value").toString()));
        m_table->setItem(i, 2, new QTableWidgetItem(rows[i].value("type").toString()));
    }
}

int CheatPanelWidget::playDataCount() const { return m_table->rowCount(); }
QString CheatPanelWidget::selectedProviderId() const { return m_provider->currentData().toString(); }
QString CheatPanelWidget::selectedConsoleId() const { return m_console->currentData().toString(); }

void CheatPanelWidget::refreshPlayData()
{
    if (!m_refreshBtn->isEnabled() || !m_client) return;
    m_pendingRead = m_client->request("/playdata", "POST", {{"providerId", selectedProviderId()}});
    updateControls();
}

void CheatPanelWidget::sendCommand()
{
    if (!m_sendBtn->isEnabled() || !m_client) return;
    m_pendingCommand = m_client->request("/cmd", "POST", {{"consoleId", selectedConsoleId()}, {"cmd", m_cmdEdit->text()}});
    updateControls();
}
