#pragma once
// 防止头文件重复包含 | Prevent multiple header inclusion

#include <QMainWindow>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QCheckBox>
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <QSlider>
#include <functional>
#include "TranslationServer.h" // 翻译核心服务 | Translation core service
#include <QMenu>
#include <QDialog>          // 🌟 新增：引入 QDialog 以支持独立窗口 | Include QDialog for standalone window
#include "TokenManager.h"   // Token 计数管理器 | Token count manager
#include "LoadingOverlay.h" // 加载遮罩层 | Loading overlay

// 主窗口类：经典模式界面
// Main Window Class: Classic Mode UI
class GameLauncherWidget;
class CheatPanelWidget;
class CheatCenterWindow;
class UpdateChecker;
class QLabel;

class MainWindow : public QMainWindow
{
Q_OBJECT // 启用 Qt 元对象系统（信号/槽）| Enable Qt Meta-Object System (Signals/Slots)

    public :
    // 构造函数 | Constructor
    MainWindow(QWidget *parent = nullptr);
    // 析构函数 | Destructor
    ~MainWindow();

    // 🔥 公开接口：供 main.cpp 同步状态使用
    // 🔥 Public Interface: Used by main.cpp to sync status
    TranslationServer *getServer() { return server; }
    TranslationServer *getServer() const { return server; }
    void setUpdateBaseUrl(const QString &url); // 测试注入更新检查地址

    // 获取当前 UI 配置 | Get current UI configuration
    AppConfig getUiConfig();
    // 从配置加载 UI 状态（移动到 public 以便外部调用）
    // Load UI state from config (moved to public for external access)
    void loadConfigToUi(bool emitToggleLogs = true);
    void setLogFeedEnabled(bool enabled);

protected:
    // 重写关闭事件 | Override close event
    void closeEvent(QCloseEvent *event) override;
    // 重写显示事件 | Override show event
    void showEvent(QShowEvent *event) override;
    // 重写调整大小事件 | Override resize event
    bool eventFilter(QObject *watched, QEvent *event) override;


private slots:
    // 按钮点击槽函数 | Button click slots
    void onStartClicked();
    void onStopClicked();
    void onTestConfig();
    void onFetchModels();
    void onSaveConfig();
    void onLoadConfig();
    void onExportLog();

    // 状态更新槽函数 | Status update slots
    void updateTokenDisplay(long long total, long long prompt, long long completion);
    void onClearContext();
    void onInstallFontClicked(); // 提权安装内置字体到机器级（UAC）
    void onLogMessage(QString msg);

    // 上下文菜单槽函数 | Context menu slots
    void onLogContextMenu(const QPoint &pos);
    void onGlossaryContextMenu(const QPoint &pos);
    void onBatchContextMenu(const QPoint &pos);  // 多行模式右键菜单

    // 动画与界面控制 | Animation and UI control
    void fadeOutAndClose();
    void toggleTheme();
    void toggleLanguage();

    // 术语表功能 | Glossary features
    void onSelectGlossary();
    void onOpenAutoTranslations();
    void onGlossaryChanged();

    // HUD 模式切换 | HUD mode switching (temporarily disabled)
    // void switchToHud();
    // void restoreFromHud();

    // 全局透明度控制 | Global opacity control

    // 界面模式切换 | UI mode switching

    // 🌟 新增：术语表编辑器专用槽函数 | Glossary Editor Slots
    void openGlossaryEditor();
    void saveGlossaryEditor();
    void onApiComboContextMenu(const QPoint &pos);

signals:
    // 请求切换到流光模式（通知 main.cpp 进行窗口切换）
    // Request switch to Modern View (notify main.cpp to perform window switching)

private:
    void setupApiKeyMemory();
    void handleApiBaseUrlChanged();
    void persistCurrentApiKeyMemory();

    // 初始化 UI 布局 | Initialize UI layout
    void setupUi();
    // 控制控件启用/禁用状态 | Control widget enable/disable state
    void toggleControls(bool running);
    // 应用主题（深色/浅色）| Apply theme (Dark/Light)
    void applyTheme(bool isDark);
    // 更新界面文本（多语言支持）| Update UI text (Multi-language support)
    void updateUIText();
    // 添加术语表路径到历史记录 | Add glossary path to history
    void addToGlossaryHistory(const QString &path);
    // 平滑切换逻辑（用于动画过渡）
    // Smooth switch logic (used for animation transitions)
    void smoothSwitch(std::function<void()> changeLogic);

    // 状态标志 | State Flags
    bool m_isClosing = false;       // 是否正在关闭 | Is closing
    bool m_isDarkTheme = true;      // 当前主题 | Current theme
    int m_currentLang = 0;          // 当前语言索引 | Current language index
    bool m_isServerRunning = false; // 服务器运行状态 | Server running status
    bool m_apiKeyMemoryEnabled = false;
    QString m_lastApiBaseUrl;
    bool m_logFeedEnabled = false;
    QMetaObject::Connection m_logMessageConnection;
    QMetaObject::Connection m_logsClearedConnection;

    // 获取友好的错误消息 | Get friendly error message
    QString getFriendlyErrorMessage(int code, int lang);

    // 加载遮罩指针 | Loading overlay pointer
    LoadingOverlay *fetchLoadingOverlay = nullptr;
    LoadingOverlay *testLoadingOverlay = nullptr;

    // 🔥 核心 UI 组件定义 (确保 MainWindow.cpp 能找到它们)
    // 🔥 Core UI Component Definitions (Ensure MainWindow.cpp can find them)

    // 配置输入区 | Configuration Input Area
    QComboBox *apiAddressCombo = nullptr;  // API 地址 | API Address
    QLineEdit *apiKeyEdit = nullptr;       // API 密钥 | API Key
    QComboBox *modelCombo = nullptr;       // 模型选择 | Model Selection
    QLineEdit *portEdit = nullptr;         // 端口 | Port
    QDoubleSpinBox *tempSpin = nullptr;    // 温度参数 | Temperature Parameter
    QSpinBox *contextSpin = nullptr;       // 上下文长度 | Context Length
    QSpinBox *threadSpin = nullptr;        // 线程数 | Thread Count
    QTextEdit *systemPromptEdit = nullptr; // 系统提示词 | System Prompt
    QLineEdit *prePromptEdit = nullptr;    // 预提示词 | Pre-Prompt

    // 日志区域 | Log Area
    QPlainTextEdit *logArea = nullptr; // 日志显示（Qt 官方日志模式，防未响应） | Log Display

    // 术语表控制 | Glossary Controls
    QCheckBox *chkGlossary = nullptr;      // 启用术语表 | Enable Glossary
    QComboBox *glossaryCombo = nullptr;    // 术语表选择 | Glossary Selection (👈 补回这个 | Restored)
    QCheckBox *chkLockSysPrompt = nullptr; // 锁定系统提示 | Lock System Prompt
    QCheckBox *chkLockGlossary = nullptr;  // 锁定术语表 | Lock Glossary (👈 补回这个 | Restored)

    // 功能按钮 | Function Buttons
    QPushButton *btnSelectGlossary = nullptr; // 选择术语表 | Select Glossary
    QPushButton *btnOpenAuto = nullptr;       // 自动翻译设置 | Auto Translation Settings

    // 主控制按钮 | Main Control Buttons
    QLabel *m_serverStatusLabel = nullptr;
    class QSpinBox *retrySpin = nullptr;
    class QSpinBox *timeoutSpin = nullptr; // 服务状态（随游戏自动启停）
    QLabel *lblLang = nullptr;
    QLabel *lblDstLang = nullptr;
    QComboBox *m_srcLangCombo = nullptr;
    QComboBox *m_dstLangCombo = nullptr;
    void updateServerStatusLabel();
    GameLauncherWidget *m_launcher = nullptr; // 「启动」标签页
    CheatCenterWindow *m_cheatCenter = nullptr;
    UpdateChecker *m_updater = nullptr;
    QGroupBox *m_updateGroup = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_updateStatusLabel = nullptr;
    QLabel *m_githubLink = nullptr;
    QPushButton *m_checkUpdateButton = nullptr;
    QCheckBox *m_translationEnabled = nullptr;
    QLabel *m_translationScope = nullptr;
    QHash<QString, bool> m_translationOwners;


    QPushButton *fetchModelBtn = nullptr;   // 获取模型列表 | Fetch Models
    QPushButton *themeBtn = nullptr;        // 切换主题 | Toggle Theme
    QPushButton *testBtn = nullptr;         // 测试配置 | Test Config
    QPushButton *loadBtn = nullptr;         // 加载配置 | Load Config
    QPushButton *saveBtn = nullptr;         // 保存配置 | Save Config
    QPushButton *exportBtn = nullptr;       // 导出日志 | Export Log
    QPushButton *langBtn = nullptr;         // 切换语言 | Toggle Language
    QPushButton *clearCtxBtn = nullptr;     // 清除上下文 | Clear Context
    QPushButton *editGlossaryBtn = nullptr; // 🌟 新增：编辑术语表按钮 | Edit Glossary Button

    // 分组框 | Group Boxes
    QGroupBox *cfgGroup = nullptr; // 配置组 | Config Group
    QGroupBox *logGroup = nullptr; // 日志组 | Log Group

    // 标签 | Labels
    QLabel *lblApiAddr = nullptr;
    QLabel *lblApiKey = nullptr;
    QLabel *lblModel = nullptr;
    QLabel *lblPort = nullptr;
    QLabel *lblThread = nullptr;
    QLabel *lblTemp = nullptr;
    QLabel *lblCtx = nullptr;
    QLabel *lblSysPrompt = nullptr;
    QLabel *lblPrePrompt = nullptr;
    QLabel *lblGlossary = nullptr;
    QLabel *lblTokens = nullptr;          // Token 显示 | Token Display
    QLabel *lblGlossarySeed = nullptr;      // 内置词典标签
    QComboBox *glossarySeedCombo = nullptr; // 内置词典（galgame/adult/standard）
    QLabel *lblGlossaryPrompt = nullptr;      // 内置术语表标签（提示词注入）
    QComboBox *glossaryPromptCombo = nullptr; // 内置术语表（galgame/adult/standard，只读）
    QLabel *lblFontName = nullptr;           // 字体标签
    QComboBox *fontNameCombo = nullptr;      // 字体自定义（默认内置 Maple）
    QPushButton *fontInstallBtn = nullptr;   // 管理员安装内置字体
    QCheckBox *chkAdminFont = nullptr;       // 只读状态勾：已获管理员权限/内置字体已装入系统
    void updateFontAdminState();             // 刷新上述状态勾
    QLabel *lblConcurrency = nullptr;   // 并发数标签
    class QSpinBox *concSpin = nullptr;     // XUAT 并发请求数
    QLabel *lblBatchLines = nullptr;        // 批行数标签
    class QSpinBox *batchSpin = nullptr;    // 每请求打包行数
    QCheckBox *chkDebug = nullptr;        // ⏱️ 测速模式开关
    QCheckBox *chkHandleRichText = nullptr; // 📝 文本处理开关 (HandleRichText)
    QCheckBox *chkBatch = nullptr;        // 📦 打包翻译开关
    QCheckBox *chkExtractNewline = nullptr; // ↩️ 提取换行开关

    // 🌟 新增：术语表编辑器的持久化成员 | Persistent members for Glossary Editor
    QDialog *m_glossaryEditor = nullptr;
    QTextEdit *m_glossaryTextEdit = nullptr;
    QPushButton *m_glossarySaveBtn = nullptr;
    QPushButton *m_glossaryCancelBtn = nullptr;
    QString m_currentEditingPath; // 记住当前正在编辑的路径 | Remember current editing path

    // 核心逻辑对象 | Core Logic Objects
    TranslationServer *server = nullptr;        // 翻译服务实例 | Translation Service Instance

    // 全局透明度控制 (可折叠面板) | Global opacity control (collapsible panel)
};