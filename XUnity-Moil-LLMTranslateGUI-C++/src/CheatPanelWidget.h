#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QList>
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QComboBox;
class CheatBridgeClient;

class CheatPanelWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CheatPanelWidget(QWidget *parent = nullptr);
    void updateLanguage(int lang);
    void setClient(CheatBridgeClient *client);
    void setCapabilities(const QJsonObject &capabilities);
    void setPlayData(const QList<QJsonObject> &rows);
    void clearSession();
    int playDataCount() const;
    QString selectedProviderId() const;
    QString selectedConsoleId() const;

signals:
    void entrySelectionChanged(const QString &providerId, const QString &consoleId);
    void capabilitiesReceived(const QJsonObject &capabilities);

private slots:
    void refreshPlayData();
    void sendCommand();

private:
    void updateControls();
    CheatBridgeClient *m_client = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_cmdEdit = nullptr;
    QComboBox *m_provider = nullptr;
    QComboBox *m_console = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_entriesBtn = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QLabel *m_status = nullptr;
    QJsonObject m_capabilities;
    QString m_pendingRead;
    QString m_pendingCommand;
    QString m_pendingCapabilities;
    QLabel *m_providerLabel = nullptr;
    QLabel *m_consoleLabel = nullptr;
    int m_lang = 1;
};
