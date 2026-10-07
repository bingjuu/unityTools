#include "GameLauncherWidget.h"
#include "Deployment.h"
#include "LogManager.h"
#include "ConfigManager.h"
#include <memory>

#include <QCheckBox>
#include <QDesktopServices>
#include <QDirIterator>
#include <QtConcurrent>
#include <QMetaObject>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QLabel>
#include <QHBoxLayout>
#include <QMimeData>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QApplication>
#include <QStandardPaths>
#include <QFile>
#include <QUuid>
#include <windows.h>

GameLauncherWidget::GameLauncherWidget(QWidget *parent) : QWidget(parent)
{
    setAcceptDrops(true);
    setAttribute(Qt::WA_StyledBackground, true); // 让样式表背景真正生效（底色由 MainWindow::applyTheme 随主题应用）

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addStretch();

    m_dropLabel = new QLabel(this);
    m_dropLabel->setAlignment(Qt::AlignCenter);
    m_dropLabel->setWordWrap(true);
    m_dropLabel->setText(tr("将游戏 exe 拖入此处\n或点击此处选择游戏主程序"));
    m_dropLabel->setCursor(Qt::PointingHandCursor);
    m_dropLabel->setStyleSheet("QLabel { color: palette(text); font-size: 14px; border: 2px dashed palette(mid); border-radius: 10px; padding: 40px 10px; background: rgba(127,127,127,0.06); }");
    connect(m_dropLabel, &QLabel::linkActivated, this, [this](){});
    m_dropLabel->installEventFilter(this);
    layout->addWidget(m_dropLabel);

    m_engineLabel = new QLabel(this);
    m_engineLabel->setAlignment(Qt::AlignCenter);
    m_engineLabel->setStyleSheet("QLabel { color: palette(mid); font-size: 12px; }");
    layout->addWidget(m_engineLabel);

    auto *optGroup = new QGroupBox(tr("部署选项（勾选 = 启动时自动部署）"), this);
    auto *optLayout = new QVBoxLayout(optGroup);
    optLayout->setSpacing(6);
    m_chkBepInEx  = new QCheckBox(tr("BepInEx 框架（插件运行环境，必装）"), optGroup);
    m_chkXUAT     = new QCheckBox(tr("XUAT 实时翻译插件（连接设置页的翻译接口）"), optGroup);
    m_chkDemosaic = new QCheckBox(tr("去马赛克插件（UniversalUnityDemosaics）"), optGroup);
    m_chkCheat    = new QCheckBox(tr("作弊支持（BepInEx 控制台 + RuntimeUnityEditor）"), optGroup);
    for (QCheckBox *c : { m_chkBepInEx, m_chkXUAT, m_chkDemosaic, m_chkCheat }) c->setChecked(true);
    m_chkBepInExBase = m_chkBepInEx->text();
    m_chkXuatBase = m_chkXUAT->text();
    m_chkDemosaicBase = m_chkDemosaic->text();
    m_chkCheatBase = m_chkCheat->text();
    optLayout->addWidget(m_chkBepInEx);
    optLayout->addWidget(m_chkXUAT);
    optLayout->addWidget(m_chkDemosaic);
    optLayout->addWidget(m_chkCheat);

    // 自进化子区块（默认隐藏，勾选展开）
    m_chkGlossaryEvolve = new QCheckBox(tr("词典自进化（游戏退出时用整理模型提炼人名/专名，默认关）"), optGroup);
    m_chkGlossaryEvolve->setChecked(false);
    m_evoEndpoint = new QLineEdit(optGroup);
    m_evoEndpoint->setPlaceholderText(tr("整理模型端点 (如 http://127.0.0.1:8080/v1)"));
    m_evoModel = new QLineEdit(optGroup);
    m_evoModel->setPlaceholderText(tr("整理模型名 (可与应用内主模型不同)"));
    m_evoKey = new QLineEdit(optGroup);
    m_evoKey->setPlaceholderText(tr("整理模型 API Key (可留空)"));
    m_evoEndpoint->setVisible(false);
    m_evoModel->setVisible(false);
    m_evoKey->setVisible(false);
    connect(m_chkGlossaryEvolve, &QCheckBox::toggled, this, [this](bool on){
        m_evoEndpoint->setVisible(on);
        m_evoModel->setVisible(on);
        m_evoKey->setVisible(on);
    });
    optLayout->addWidget(m_chkGlossaryEvolve);
    optLayout->addWidget(m_evoEndpoint);
    optLayout->addWidget(m_evoModel);
    optLayout->addWidget(m_evoKey);
    layout->addWidget(optGroup);

    m_startBtn = new QPushButton(tr("启动游戏"), this);
    m_startBtn->setMinimumHeight(44);
    m_startBtn->setCursor(Qt::PointingHandCursor);
    layout->addWidget(m_startBtn);

    m_status = new QLabel(this);
    m_status->setAlignment(Qt::AlignCenter);
    m_status->setWordWrap(true);
    m_status->setStyleSheet("QLabel { color: #E6B422; font-size: 12px; }");
    layout->addWidget(m_status);

    layout->addStretch();

    m_saveGroup = new QGroupBox(tr("存档位置（选择游戏后自动扫描）"), this);
    m_saveListLayout = new QVBoxLayout(m_saveGroup);
    m_saveListLayout->setSpacing(6);
    QLabel *hint = new QLabel(tr("选择游戏后此处列出检测到的存档目录；可打开目录查看结构或导入存档。"), m_saveGroup);
    hint->setWordWrap(true);
    hint->setStyleSheet("QLabel { color: #888888; font-size: 11px; }");
    m_saveListLayout->addWidget(hint);
    layout->addWidget(m_saveGroup);

    auto *cheatButton = new QPushButton(tr("打开游戏控制台"), this);
    cheatButton->setObjectName("openCheatCenter");
    cheatButton->setEnabled(false);
    layout->addWidget(cheatButton);
    connect(cheatButton, &QPushButton::clicked, this, &GameLauncherWidget::openCheatCenterRequested);
    m_handshakeTimer.setInterval(250);
    connect(&m_handshakeTimer, &QTimer::timeout, this, &GameLauncherWidget::checkBridgeHandshake);

    connect(m_startBtn, &QPushButton::clicked, this, [this](){ startClicked(); });
}

// 点击拖放区 = 弹文件选择（拖放区自己就是入口，不需要额外按钮）
bool GameLauncherWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_dropLabel && event->type() == QEvent::MouseButtonRelease && !m_busy)
        startClicked();
    return QWidget::eventFilter(watched, event);
}

bool GameLauncherWidget::validateGameExe(const QString &exePath)
{
    const QFileInfo fi(exePath);
    if (!fi.isFile()) return false; // 目录改名 .exe 的假游戏（审查 m2）
    const QDir dir(fi.absolutePath());
    return !dir.entryList(QStringList() << "*_Data", QDir::Dirs).isEmpty();
}

void GameLauncherWidget::startClicked()
{
    // 未选游戏 → 弹文件选择（选择即表达启动意图，选完直接启动）；
    // 已拖入过 → 直接启动当前游戏（拖入阶段已完成检测）
    if (m_exePath.isEmpty()) {
        const QString exe = QFileDialog::getOpenFileName(this, tr("选择游戏主程序 exe"), QString(),
                                                         tr("游戏主程序 (*.exe)"));
        if (exe.isEmpty()) return;
        launchGame(exe);
        return;
    }
    launchGame(m_exePath);
}

// 进程判活：OpenProcess 成功不代表活着（终止后的进程对象在有句柄引用期间仍可打开），
// 必须查退出码；打不开时 ERROR_INVALID_PARAMETER=pid 不存在（已退出），其它错误按活着处理
static bool isProcessAlive(qint64 pid)
{
    if (pid <= 0) return false;
    const HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return GetLastError() != ERROR_INVALID_PARAMETER;
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    if (!ok) return true;
    return code == STILL_ACTIVE;
}

void GameLauncherWidget::updateEngineLabel(const QString &exePath)
{
    const QString gameDir = QFileInfo(exePath).absolutePath();
    if (!validateGameExe(exePath)) {
        m_engineLabel->setText(tr("✘ 不是 Unity 游戏（缺少 *_Data 目录）"));
        m_engineLabel->setStyleSheet("QLabel { color: #FF5555; font-size: 12px; }");
        return;
    }
    if (deployment::isIl2CppGame(gameDir)) {
        m_engineLabel->setText(tr("✔ Unity (IL2CPP) · 首次启动将自动生成注入组件并重启"));
        m_engineLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
        updateSavePanel(exePath);
        return;
    }
    const quint16 machine = deployment::readPEMachineType(exePath);
    const QString arch = (machine == 0x014C) ? "x86" : "x64";
    m_engineLabel->setText(tr("✔ Unity (Mono) · %1").arg(arch));
    m_engineLabel->setStyleSheet("QLabel { color: #4CAF50; font-size: 12px; }");
    updateSavePanel(exePath);
}

bool GameLauncherWidget::startGameProcess(const QString &program, const QString &workDir, qint64 *pid)
{
    QFile::remove(m_handshakePath);
    QDir().mkpath(QFileInfo(m_handshakePath).absolutePath());
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("UNITYTOOLS_SESSION_ID", m_session.sessionId);
    environment.insert("UNITYTOOLS_TOKEN", m_session.token);
    environment.insert("UNITYTOOLS_HANDSHAKE", m_handshakePath);
    environment.insert("UNITYTOOLS_PORT", "0");
    QProcess process;
    process.setProgram(program);
    process.setArguments(QStringList());
    process.setWorkingDirectory(workDir);
    process.setProcessEnvironment(environment);
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    return process.startDetached(pid);
}

void GameLauncherWidget::newControlIdentity(bool il2cpp)
{
    m_session = {QUuid::createUuid().toString(QUuid::WithoutBraces), 0, il2cpp ? "il2cpp" : "mono", 0,
        QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces)};
    m_handshakePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + "/runtime/" + m_session.sessionId + ".json";
}

void GameLauncherWidget::trackPlayingSession()
{
    const auto session = m_session;
    const auto handshake = m_handshakePath;
    const bool translation = m_launchTranslationEnabled;
    emit translationSessionStarted(session.sessionId, session.pid, translation);
    auto *timer = new QTimer(this);
    auto processHandle = std::shared_ptr<void>(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(session.pid)),
        [](void *handle) { if (handle) CloseHandle(handle); });
    timer->setInterval(500);
    connect(timer, &QTimer::timeout, this, [this, timer, session, handshake, processHandle] {
        const bool alive = processHandle ? WaitForSingleObject(processHandle.get(), 0) == WAIT_TIMEOUT : isProcessAlive(session.pid);
        if (alive) return;
        timer->stop();
        emit gameSessionEnded(session.sessionId, session.pid);
        QFile::remove(handshake);
        timer->deleteLater();
    });
    timer->start();
}

void GameLauncherWidget::setCheatCenterAvailable(bool available)
{
    if (auto *button = findChild<QPushButton *>("openCheatCenter")) button->setEnabled(available);
}

void GameLauncherWidget::checkBridgeHandshake()
{
    if (m_phase != Phase::Playing || m_handshakePath.isEmpty() || !isProcessAlive(m_pid)) return;
    QFile file(m_handshakePath);
    if (!file.open(QIODevice::ReadOnly)) return;
    const auto document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return;
    const auto object = document.object();
    const auto sessionId = object["sessionId"].toString();
    const auto runtime = object["runtime"].toString();
    const auto handshakePid = object["pid"].toInteger();
    const auto port = object["port"].toInteger();
    if (object["protocolVersion"].toString() != "1" || sessionId != m_session.sessionId || runtime != m_session.runtime || handshakePid != m_pid || port <= 0 || port > 65535) return;
    m_session.port = static_cast<quint16>(port);
    m_handshakeTimer.stop();
    setCheatCenterAvailable(true);
    emit bridgeSessionReady(m_session);
}

void GameLauncherWidget::updateSavePanel(const QString &exePath)
{
    // 清空旧行
    while (QLayoutItem *item = m_saveListLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    QLabel *scanning = new QLabel(tr("正在扫描存档位置..."), m_saveGroup);
    scanning->setStyleSheet("QLabel { color: #888888; }");
    m_saveListLayout->addWidget(scanning);
    // 扫描放线程池：LocalLow 目录多时避免冻结 UI（点不动 Tab 的根因）
    const QString gameDir = QFileInfo(exePath).absolutePath();
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [this, watcher](){
        watcher->deleteLater();
        renderSavePanel(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([gameDir, exePath]() {
        return deployment::locateSaveDirs(gameDir, exePath);
    }));
}

void GameLauncherWidget::renderSavePanel(const QStringList &dirs)
{
    // 清空旧行
    while (QLayoutItem *item = m_saveListLayout->takeAt(0)) {
        if (QWidget *w = item->widget()) w->deleteLater();
        delete item;
    }
    if (dirs.isEmpty()) {
        QLabel *none = new QLabel(tr("未找到常见存档目录"), m_saveGroup);
        none->setStyleSheet("QLabel { color: #888888; }");
        m_saveListLayout->addWidget(none);
        return;
    }
    for (const QString &dir : dirs) {
        int files = 0;
        QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) { it.next(); ++files; }
        auto *row = new QHBoxLayout;
        auto *lbl = new QLabel(tr("%1 个存档文件 · %2").arg(files).arg(QDir::toNativeSeparators(dir)), m_saveGroup);
        lbl->setWordWrap(true);
        lbl->setStyleSheet("QLabel { color: #CCCCCC; font-size: 11px; }");
        auto *openBtn = new QPushButton(tr("打开"), m_saveGroup);
        openBtn->setFixedWidth(60);
        connect(openBtn, &QPushButton::clicked, this, [dir](){
            QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
        });
        row->addWidget(lbl, 1);
        row->addWidget(openBtn);
        m_saveListLayout->addLayout(row);
    }
}

// Playing 阶段允许放弃旧轮询切换新游戏（游戏是 detached 的，会继续独立运行到退出）
void GameLauncherWidget::releasePolling()
{
    if (m_busy && m_phase == Phase::Playing) {
        m_pollTimer.stop();
        m_handshakeTimer.stop();
        setCheatCenterAvailable(false);
        emit controlSessionDetached();
        m_session = {};
        m_busy = false;
        m_startBtn->setEnabled(true);
    }
}

// 拖入/选择游戏：只做检测与状态展示，不启动（启动只由「启动游戏」按钮触发）
void GameLauncherWidget::prepareGame(const QString &exePath)
{
    releasePolling();
    if (m_busy) return;
    m_exePath = QDir::toNativeSeparators(exePath);
    m_dropLabel->setText(m_exePath);
    updateEngineLabel(m_exePath); // 内部含 validate 提示与存档面板刷新
    updateDeployStatus();
    if (validateGameExe(m_exePath))
        m_status->setText(tr("检测完成，确认部署选项后点击「启动游戏」"));
}

// 依据当前游戏目录刷新各部署项 ✓/✗（判定与 ensure* 的幂等短路一致）
void GameLauncherWidget::updateDeployStatus()
{
    const QString gameDir = QFileInfo(m_exePath).absolutePath();
    const bool valid = !m_exePath.isEmpty() && validateGameExe(m_exePath);
    auto mark = [valid](bool deployed) -> QString {
        if (!valid) return QString();
        return deployed ? tr("　✔ 已部署") : tr("　✘ 未部署（启动时自动部署）");
    };
    m_chkBepInEx->setText(m_chkBepInExBase + mark(deployment::isBepInExDeployed(gameDir)));
    m_chkXUAT->setText(m_chkXuatBase + mark(deployment::isXuatDeployed(gameDir)));
    m_chkDemosaic->setText(m_chkDemosaicBase + mark(deployment::isDemosaicDeployed(gameDir)));
    m_chkCheat->setText(m_chkCheatBase + mark(deployment::isCheatDeployed(gameDir)));
}

void GameLauncherWidget::launchGame(const QString &exePath)
{
    releasePolling();
    if (m_busy) return;
    prepareGame(exePath); // 同步路径显示/引擎检测/部署状态（含非法路径提示）

    if (!validateGameExe(m_exePath)) { // 功能必需的最小判定（无 *_Data 则无法定位游戏）
        const QString reason = tr("不是有效的 Unity 游戏主程序（同目录需存在 *_Data 文件夹）");
        m_status->setText(reason);
        emit launchFailed(reason);
        return;
    }
    const QString gameDir = QFileInfo(m_exePath).absolutePath();
    emit aboutToLaunch(gameDir); // 部署前同步：主窗口保存当前 UI 配置（含并发/批行数）

    m_busy = true;
    m_startBtn->setEnabled(false);
    QString err;
    QString launchPath = m_exePath;
    QString workDir; // 启动工作目录（goto fail 会跳过初始化，故声明前置）
    bool needInteropRun = false;
    int step = 0, total = m_chkBepInEx->isChecked() + m_chkXUAT->isChecked()
                        + m_chkDemosaic->isChecked() + m_chkCheat->isChecked();
    auto stepLabel = [&](const QString &name){
        ++step;
        m_status->setText(tr("[%1/%2] 部署 %3...").arg(step).arg(total).arg(name));
        QApplication::processEvents();
    };
    const deployment::Injector injector = deployment::detectInjector(gameDir);
    if (m_chkBepInEx->isChecked()) {
        stepLabel("BepInEx");
        if (injector == deployment::Injector::Mono) {
            if (!deployment::ensureBepInEx(gameDir, m_exePath, &err)) goto fail;
        } else if (!deployment::ensureBepInExIl2Cpp(gameDir, m_exePath, &err)) goto fail;
    }
    if (m_chkXUAT->isChecked()) {
        stepLabel(tr("XUAT 翻译插件"));
        if (injector == deployment::Injector::Mono) {
            if (!deployment::ensureXUAT(gameDir, &err)) goto fail;
        } else if (!deployment::ensureXUATIl2Cpp(gameDir, &err)) goto fail;
    }
    if (m_chkDemosaic->isChecked()) {
        stepLabel(tr("去马赛克插件"));
        if (injector == deployment::Injector::Mono) {
            if (!deployment::ensureDemosaic(gameDir, &err)) goto fail;
        } else if (!deployment::ensureDemosaicIl2Cpp(gameDir, &err)) goto fail;
    }
    if (m_chkCheat->isChecked()) {
        stepLabel(tr("作弊支持"));
        if (injector == deployment::Injector::Mono) {
            if (!deployment::ensureCheatSupport(gameDir, &err)) goto fail;
        } else if (!deployment::ensureCheatSupportIl2Cpp(gameDir, &err)) goto fail;
    }

    if ((m_chkXUAT->isChecked() || m_chkCheat->isChecked()) && !deployment::ensureBridge(gameDir, injector == deployment::Injector::Il2Cpp, &err)) goto fail;

    m_status->setText(tr("启动游戏..."));
    QApplication::processEvents();
    // 非 ASCII 路径 → 8.3 短路径 / junction 解析成纯英文启动路径（MTool 同款思路，原目录不动）
    if (!deployment::isPureAscii(m_exePath)) {
        m_status->setText(tr("启动路径含非 ASCII 字符，正在解析纯英文启动路径..."));
        QApplication::processEvents();
        QString aerr;
        launchPath = deployment::asciiLaunchPath(m_exePath, &aerr);
        if (launchPath.isEmpty()) { err = aerr; goto fail; }
        LogManager::instance().addLog(tr("ASCII 启动路径: %1").arg(launchPath));
    }
    // 两阶段启动：Mono / interop 已生成的 IL2CPP 一次启动即玩；
    // interop 未生成的 IL2CPP 先跑一次生成组件（BepInEx 6 Cpp2IL），完成后自动重启
    needInteropRun = (injector == deployment::Injector::Il2Cpp)
                      && !deployment::interopGenerated(gameDir);
    m_phase = needInteropRun ? Phase::InteropFirstRun : Phase::Playing;
    m_ticks = 0;
    if (needInteropRun)
        m_status->setText(tr("第一次启动：正在生成注入组件（可能需要数分钟，完成后将自动重启游戏）"));

    // startDetached：工具退出不影响游戏；PID 轮询维持状态机（审查 B1/M1）
    // 工作目录必须用 QFileInfo 解析：此前用 left(lastIndexOf('/'))，纯 ASCII 路径经
    // toNativeSeparators 变反斜杠后取不到目录（返回整个文件路径），工作目录非法导致启动失败
    workDir = QFileInfo(launchPath).absolutePath();
    m_launchPath = launchPath;
    m_launchTranslationEnabled = m_chkXUAT->isChecked() && ConfigManager::loadConfig().translation_enabled;
    newControlIdentity(injector == deployment::Injector::Il2Cpp);
    if (!startGameProcess(launchPath, workDir, &m_pid)) {
        m_busy = false;
        m_startBtn->setEnabled(true);
        const QString reason = tr("游戏启动失败（无法执行 %1）").arg(m_exePath);
        m_status->setText(reason);
        emit launchFailed(reason);
        return;
    }
    LogManager::instance().addLog(tr("游戏已启动: %1 (pid %2)").arg(m_exePath).arg(m_pid));
    m_status->setText(tr("游戏运行中（pid %1）——关闭游戏后此处自动复位；可直接拖入新游戏切换").arg(m_pid));
    m_session.pid = m_pid;
    setCheatCenterAvailable(false);
    m_handshakeTimer.start();
    emit gameLaunched();
    if (m_phase == Phase::Playing) trackPlayingSession();

    if (!m_pollConnected) { // 仅连接一次：launchGame 可能被多次调用（重复连接会导致重复拉起）
        m_pollConnected = true;
    connect(&m_pollTimer, &QTimer::timeout, this, [this](){
        const bool alive = isProcessAlive(m_pid);

        if (m_phase == Phase::InteropFirstRun) {
            ++m_ticks;
            const QString gameDir = QFileInfo(m_exePath).absolutePath();
            if (deployment::interopGenerated(gameDir)) {
                // 组件生成完成：结束第一次进程，自动二次启动（注入完整生效）
                m_status->setText(tr("注入组件生成完成，正在重启游戏..."));
                QApplication::processEvents();
                if (alive) {
                    HANDLE k = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(m_pid));
                    if (k) { TerminateProcess(k, 0); CloseHandle(k); }
                    Sleep(2000);
                }
                QFile::remove(m_handshakePath);
                newControlIdentity(true);
                qint64 newPid = 0;
                const QString dir2 = QFileInfo(m_launchPath).absolutePath();
                if (!startGameProcess(m_launchPath, dir2, &newPid)) {
                    m_pollTimer.stop();
                    m_busy = false;
                    m_startBtn->setEnabled(true);
                    const QString reason = tr("注入完成但二次启动失败（无法执行 %1）").arg(m_exePath);
                    m_status->setText(reason);
                    emit launchFailed(reason);
                    return;
                }
                m_pid = newPid;
                m_session.pid = m_pid;
                m_phase = Phase::Playing;
                m_handshakeTimer.start();
                trackPlayingSession();
                m_ticks = 0;
                m_status->setText(tr("注入完成，进入游戏"));
                LogManager::instance().addLog(tr("IL2CPP 注入完成，游戏已重启进入正常游玩"));
            } else if (!alive) {
                m_pollTimer.stop();
                m_busy = false;
                m_startBtn->setEnabled(true);
                m_dropLabel->setText(tr("将游戏 exe 拖入此处\n或点击此处选择游戏主程序"));
                m_engineLabel->clear();
                const QString reason = tr("注入组件生成失败：游戏在组件生成前退出，详见游戏目录 BepInEx 日志");
                m_status->setText(reason);
                LogManager::instance().addLog("❌ " + reason);
                emit launchFailed(reason);
            } else if (m_ticks >= 240) { // 8 分钟超时
                m_pollTimer.stop();
                m_busy = false;
                m_startBtn->setEnabled(true);
                const QString reason = tr("注入组件生成超时（8 分钟），详见游戏目录 BepInEx 日志");
                m_status->setText(reason);
                LogManager::instance().addLog("❌ " + reason);
                emit launchFailed(reason);
            } else {
                m_status->setText(tr("第一次启动：正在生成注入组件...（%1 秒）").arg(m_ticks * 2));
            }
            return;
        }

        // Playing：存活轮询 + 前 60 秒顺带验证注入状态
        if (alive && m_ticks <= 30 && !deployment::chainloaderSucceeded(QFileInfo(m_exePath).absolutePath())) {
            ++m_ticks;
            if (m_ticks == 30)
                LogManager::instance().addLog(tr("警告：未能从日志确认注入状态（游戏可能仍可玩），详见 BepInEx 日志"));
            return;
        }
        if (!alive) {
            m_pollTimer.stop();
            m_busy = false;
            m_startBtn->setEnabled(true);
            m_dropLabel->setText(tr("将游戏 exe 拖入此处\n或点击此处选择游戏主程序"));
            m_engineLabel->clear();
            m_status->setText(tr("游戏已退出"));
            m_handshakeTimer.stop();
            setCheatCenterAvailable(false);
            QFile::remove(m_handshakePath);
            m_session = GameSession{};
            emit gameFinished(0);
        }
    });
    }
    m_pollTimer.start(2000);
    return;

fail:
    m_busy = false;
    m_startBtn->setEnabled(true);
    m_status->setText(tr("部署失败: %1").arg(err));
    LogManager::instance().addLog(tr("❌ 部署失败: %1").arg(err));
    emit launchFailed(err);
}

void GameLauncherWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void GameLauncherWidget::dropEvent(QDropEvent *event)
{
    if (m_busy && m_phase != Phase::Playing) return;
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty()) return;
    const QString path = urls.first().toLocalFile();
    if (!path.endsWith(".exe", Qt::CaseInsensitive)) {
        m_status->setText(tr("请拖入 .exe 文件"));
        return;
    }
    prepareGame(path); // 拖入仅检测；点「启动游戏」才部署并启动
}
