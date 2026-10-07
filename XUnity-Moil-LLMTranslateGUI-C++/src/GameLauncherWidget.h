#pragma once
#include <QWidget>
#include <QTimer>
#include <QCheckBox>
#include <QLineEdit>
#include "CheatBridgeClient.h"
class QLabel;
class QPushButton;
class QVBoxLayout;
class QCheckBox;
class QLineEdit;
class QGroupBox;

// ==========================================
// 启动页（MTool 式）：拖入游戏 exe → 引擎检测 → 按勾选部署 → 启动
// 作为主窗口「启动」标签页；游戏控制台为独立非模态窗口。
// ==========================================

// 两个标签页的统一背景色：设置页背景必须与启动页一致（MainWindow 滚动区/pane 同用此值）
inline const char *kUnifiedTabBg = "#19191e";

class GameLauncherWidget : public QWidget
{
    Q_OBJECT
public:
    explicit GameLauncherWidget(QWidget *parent = nullptr);

    // 按钮点击与测试共用的唯一入口：校验 → 按勾选幂等部署 → 启动游戏
    void launchGame(const QString &exePath);
    bool isBusy() const { return m_busy; }
    QString gameExePath() const { return m_exePath; } // 运行面板/语言写入定位游戏用
    qint64 processId() const { return m_pid; }
    QString sessionId() const { return m_session.sessionId; }
    QString sessionToken() const { return m_session.token; }
    QString sessionRuntime() const { return m_session.runtime; }
    QString handshakePath() const { return m_handshakePath; }
    void setCheatCenterAvailable(bool available);
    bool glossaryEvolveEnabled() const { return m_chkGlossaryEvolve && m_chkGlossaryEvolve->isChecked(); }
    QString evoEndpoint() const { return m_evoEndpoint ? m_evoEndpoint->text() : QString(); }
    QString evoModel() const { return m_evoModel ? m_evoModel->text() : QString(); }
    QString evoKey() const { return m_evoKey ? m_evoKey->text() : QString(); }

    // 纯逻辑：exe 存在且同目录存在 *_Data（单测覆盖）
    static bool validateGameExe(const QString &exePath);

signals:
    void aboutToLaunch(const QString &gameDir); // 部署前发出（同步直连），主窗口落盘当前 UI 配置
    void gameLaunched();                      // 游戏进程成功拉起
    void bridgeSessionReady(const GameSession &session); // Bridge 握手已匹配当前 PID/runtime
    void gameFinished(int exitCode);          // 游戏退出（PID 轮询发现）
    void launchFailed(const QString &reason); // 校验/部署/启动失败
    void openCheatCenterRequested();
    void controlSessionDetached();
    void translationSessionStarted(const QString &sessionId, qint64 pid, bool enabled);
    void gameSessionEnded(const QString &sessionId, qint64 pid);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void startClicked();
    void releasePolling(); // Playing 阶段放弃旧轮询，允许直接切换新游戏
    void prepareGame(const QString &exePath); // 拖入/选择：仅检测与状态展示，不启动
    void updateDeployStatus();                // 依据 m_exePath 刷新各部署项 ✓/✗
    void updateEngineLabel(const QString &exePath); // 选择后显示引擎/架构检测结果
    void updateSavePanel(const QString &exePath);   // 选择后异步扫描存档位置
    void renderSavePanel(const QStringList &dirs);  // 回填扫描结果（UI 线程）
    bool startGameProcess(const QString &program, const QString &workDir, qint64 *pid);
    void checkBridgeHandshake();
    void trackPlayingSession();
    void newControlIdentity(bool il2cpp);

    QLabel *m_dropLabel = nullptr;
    QLabel *m_engineLabel = nullptr;
    QString m_chkBepInExBase;    // 各部署项基础文案（追加 ✓/✗ 状态用）
    QString m_chkXuatBase;
    QString m_chkDemosaicBase;
    QString m_chkCheatBase;
    QCheckBox *m_chkBepInEx = nullptr;
    QCheckBox *m_chkXUAT = nullptr;
    QCheckBox *m_chkDemosaic = nullptr;
    QCheckBox *m_chkCheat = nullptr;
    QPushButton *m_startBtn = nullptr;
    QLabel *m_status = nullptr;
    QCheckBox *m_chkGlossaryEvolve = nullptr; // 术语表全局化+自进化（默认关）
    QLineEdit *m_evoEndpoint = nullptr;
    QLineEdit *m_evoModel = nullptr;
    QLineEdit *m_evoKey = nullptr;
    QGroupBox *m_saveGroup = nullptr;
    QVBoxLayout *m_saveListLayout = nullptr;
    QString m_exePath;
    QString m_launchPath;
    bool m_launchTranslationEnabled = false;
    QTimer m_pollTimer;   // 游戏进程存活轮询（startDetached 后游戏独立于本工具存活）
    QTimer m_handshakeTimer;
    enum class Phase { Deploy, InteropFirstRun, Playing }; // InteropFirstRun=IL2CPP 首启生成组件
    Phase m_phase = Phase::Deploy;
    int m_ticks = 0;      // 轮询计数（2s/tick）
    qint64 m_pid = 0;
    GameSession m_session;
    QString m_handshakePath;
    bool m_busy = false;
    bool m_pollConnected = false;
};
