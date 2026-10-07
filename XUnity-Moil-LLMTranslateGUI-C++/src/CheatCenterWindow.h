#pragma once
#include <QDialog>
#include <QJsonObject>
#include <QHash>
#include "CheatBridgeClient.h"
#include "UnityAgentController.h"
class QLabel;
class CheatPanelWidget;
class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QPlainTextEdit;
class QTextEdit;
class QCheckBox;
class QTabWidget;

class CheatCenterWindow : public QDialog
{
    Q_OBJECT
public:
    explicit CheatCenterWindow(QWidget *parent = nullptr);
    void openSession(const GameSession &session);
    void closeSession(const QString &reason);
    void setCapabilities(const QJsonObject &capabilities);
    bool isSessionConnected() const;
    void setPlayData(const QList<QJsonObject> &rows);
    int playDataCount() const;
    CheatBridgeClient *client() const { return m_client; }
    void updateLanguage(int lang);
    void setTranslationPreference(bool enabled);
    void setAgentConfiguration(const QString &address, const QString &key, const QString &model);
    QString agentTaskState() const;
signals:
    void translationPreferenceChanged(bool enabled);
private:
    struct Pending { quint64 selection = 0; quint64 readSerial = 0; QString stage; QJsonObject arguments; };
    void updateObjectControls();
    void clearObjectView();
    void clearSelection();
    void selectObject(int index);
    void send(const QString &path, const QString &stage, const QJsonObject &args = {});
    void handle(const QString &id, const QString &path, const QJsonObject &envelope);
    QJsonObject memberArguments() const;
    void renderEvidence(const QJsonObject &evidence);
    void saveAgentConfiguration();
    void resetAgentView();
    void showAgentEvidence(const QJsonObject &evidence);
    void showAssistantResponse(const QString &markdown);

    CheatBridgeClient *m_client = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_capabilityStatus = nullptr;
    QLabel *m_translationStatus = nullptr;
    CheatPanelWidget *m_panel = nullptr;
    QTabWidget *m_tabs = nullptr;
    QLineEdit *m_type = nullptr;
    QLineEdit *m_member = nullptr;
    QLineEdit *m_value = nullptr;
    QCheckBox *m_nullValue = nullptr;
    QPushButton *m_find = nullptr;
    QPushButton *m_read = nullptr;
    QPushButton *m_write = nullptr;
    QComboBox *m_candidates = nullptr;
    QComboBox *m_operation = nullptr;
    QTableWidget *m_members = nullptr;
    QLabel *m_evidence = nullptr;
    QComboBox *m_methods = nullptr;
    QLineEdit *m_methodArguments = nullptr;
    QPushButton *m_invoke = nullptr;
    QComboBox *m_agentCandidates = nullptr;
    QPushButton *m_agentSelect = nullptr;
    QLineEdit *m_agentAddress = nullptr;
    QLineEdit *m_agentKey = nullptr;
    QLineEdit *m_agentModel = nullptr;
    QCheckBox *m_translationPreference = nullptr;
    QTextEdit *m_agentLog = nullptr;
    QLabel *m_agentStatus = nullptr;
    QLabel *m_agentEvidence = nullptr;
    QPlainTextEdit *m_agentDetails = nullptr;
    QString m_agentState;
    QJsonObject m_agentEvidenceValue;
    QPushButton *m_agentRun = nullptr;
    UnityAgentController m_agent;
    QJsonObject m_capabilities;
    QJsonObject m_inspection;
    QHash<QString, Pending> m_pending;
    quint64 m_selection = 0;
    quint64 m_readSerial = 0;
    bool m_mutationPending = false;
    QString m_uncertainMutation;
    QJsonObject m_mutationArguments;
    QString m_selectedObject;
    QString m_snapshotId;
    QJsonObject m_lastRead;
    QJsonObject m_writeEvidence;
    int m_lang = 1;
};
