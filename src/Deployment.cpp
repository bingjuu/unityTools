#include "Deployment.h"

#include "ConfigManager.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QEventLoop>
#include <QRegularExpression>

namespace deployment {

// —— 版本锚定的官方下载直链（vendor 缺失时的网络回退；刷新见 vendor_manifest.txt）——
static const char *URL_BEPINEX[] = {
    "https://github.com/BepInEx/BepInEx/releases/download/v5.4.23.5/BepInEx_win_x64_5.4.23.5.zip",
    "https://github.com/BepInEx/BepInEx/releases/download/v5.4.23.5/BepInEx_win_x86_5.4.23.5.zip"};
static const char *URL_XUAT =
    "https://github.com/bbepis/XUnity.AutoTranslator/releases/download/v5.6.2/XUnity.AutoTranslator-BepInEx-5.6.2.zip";
static const char *URL_RR =
    "https://github.com/bbepis/XUnity.AutoTranslator/releases/download/v5.6.2/XUnity.ResourceRedirector-BepInEx-2.1.0.zip";
static const char *URL_UUD =
    "https://github.com/ManlyMarco/UniversalUnityDemosaics/releases/download/v1.7/UniversalUnityDemosaics_BepInEx5_v1.7.zip";
static const char *URL_BEPINEX_IL2CPP[] = {
    "https://builds.bepinex.dev/projects/bepinex_be/788/BepInEx-Unity.IL2CPP-win-x64-6.0.0-be.788+5b766a3.zip",
    "https://builds.bepinex.dev/projects/bepinex_be/788/BepInEx-Unity.IL2CPP-win-x86-6.0.0-be.788+5b766a3.zip"};
static const char *URL_XUAT_IL2CPP =
    "https://github.com/bbepis/XUnity.AutoTranslator/releases/download/v5.6.2/XUnity.AutoTranslator-BepInEx-IL2CPP-5.6.2.zip";
static const char *URL_RR_IL2CPP =
    "https://github.com/bbepis/XUnity.AutoTranslator/releases/download/v5.6.2/XUnity.ResourceRedirector-BepInEx-IL2CPP-2.1.0.zip";
static const char *URL_UUD_IL2CPP =
    "https://github.com/ManlyMarco/UniversalUnityDemosaics/releases/download/v1.7/UniversalUnityDemosaics_BepInEx6_IL2CPP_net6_v1.7.zip";
static const char *URL_RUE_IL2CPP =
    "https://github.com/ManlyMarco/RuntimeUnityEditor/releases/download/v6.3.2/RuntimeUnityEditor.Bepin6.IL2CPP_v6.3.2.zip";
static const char *URL_RUE =
    "https://github.com/ManlyMarco/RuntimeUnityEditor/releases/download/v6.3.2/RuntimeUnityEditor.Bepin5_v6.3.2.zip";

quint16 readPEMachineType(const QString &exePath)
{
    QFile f(exePath);
    if (!f.open(QIODevice::ReadOnly))
        return 0;
    const QByteArray all = f.readAll();
    if (all.size() < 0x40)
        return 0;
    const quint32 eLfanew = quint32(quint8(all[0x3C])) | (quint32(quint8(all[0x3D])) << 8)
                          | (quint32(quint8(all[0x3E])) << 16) | (quint32(quint8(all[0x3F])) << 24);
    if (quint64(eLfanew) + 6 > quint64(all.size())) // 回绕安全：PE 签名(4)+Machine(2) 必须在文件内
        return 0;
    if (!(all[int(eLfanew)] == 'P' && all[int(eLfanew) + 1] == 'E' && all[int(eLfanew) + 2] == 0 && all[int(eLfanew) + 3] == 0))
        return 0;
    const quint16 machine = quint16(quint8(all[int(eLfanew) + 4])) | (quint16(quint8(all[int(eLfanew) + 5])) << 8);
    return (machine == 0x014C || machine == 0x8664) ? machine : quint16(0);
}

QString ghpUrl(const QString &url)
{
    static const QStringList MIRRORS = {
        "https://gh.xmly.dev/",
        "https://github.dpik.top/",
        "https://mirror.ghproxy.com/"};
    return MIRRORS[QRandomGenerator::global()->bounded(MIRRORS.size())] + url;
}

QString findVendorZip(const QString &prefix)
{
    const QByteArray env = qgetenv("UNITYTOOLS_VENDOR_DIR");
    QDir dir(QString::fromLocal8Bit(env));
    if (!env.isEmpty() && dir.exists())
        dir.setPath(QString::fromLocal8Bit(env));
    else {
        dir = QDir(QCoreApplication::applicationDirPath());
        bool found = false;
        for (int i = 0; i < 6 && !found; ++i) {
            const QString cand = dir.absoluteFilePath("vendor");
            if (QDir(cand).exists()) { dir = QDir(cand); found = true; }
            else if (!dir.cdUp()) break;
        }
        if (!found) return QString();
    }
    for (const QFileInfo &fi : dir.entryInfoList(QStringList() << (prefix + "*.zip"), QDir::Files))
        return fi.absoluteFilePath();
    return QString();
}

bool isIl2CppGame(const QString &gameDir)
{
    const QDir d(gameDir);
    for (const QFileInfo &di : d.entryInfoList(QStringList() << "*_Data", QDir::Dirs))
        if (QDir(di.absoluteFilePath()).exists("il2cpp_data")) return true;
    return false;
}

bool isPureAscii(const QString &s)
{
    for (const QChar c : s)
        if (c.unicode() > 127) return false;
    return true;
}

QString versionString(const QString &exePath, const QString &field)
{
#ifdef Q_OS_WIN
    const QString native = QDir::toNativeSeparators(exePath);
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(reinterpret_cast<const wchar_t *>(native.utf16()), &handle);
    if (!size) return QString();
    QByteArray data(int(size), 0);
    if (!GetFileVersionInfoW(reinterpret_cast<const wchar_t *>(native.utf16()), 0, size, data.data())) return QString();
    struct LangCodePage { WORD lang; WORD codepage; } *tl = nullptr;
    UINT len = 0;
    const wchar_t *transPath = L"\\VarFileInfo\\Translation";
    if (!VerQueryValueW(data.data(), transPath, reinterpret_cast<LPVOID *>(&tl), &len) || !tl) return QString();
    const QString query = QString("\\StringFileInfo\\%1%2\\%3")
                              .arg(tl->lang, 4, 16, QChar('0'))
                              .arg(tl->codepage, 4, 16, QChar('0'))
                              .arg(field);
    wchar_t *value = nullptr;
    if (!VerQueryValueW(data.data(), reinterpret_cast<const wchar_t *>(query.utf16()), reinterpret_cast<LPVOID *>(&value), &len) || !value) return QString();
    return QString::fromWCharArray(value);
#else
    Q_UNUSED(exePath); Q_UNUSED(field);
    return QString();
#endif
}

QStringList locateSaveDirs(const QString &gameDir, const QString &exePath)
{
    QStringList out;
    auto add = [&out](const QString &p) {
        if (!p.isEmpty() && QDir(p).exists() && !out.contains(p)) out << p;
    };
    // 1) 游戏目录内常见存档目录（galgame/Unity 命名习惯，含日文）
    const QDir d(gameDir);
    const QStringList names = {
        QStringLiteral("Save"), QStringLiteral("save"), QStringLiteral("saves"),
        QStringLiteral("SaveData"), QStringLiteral("Savedata"), QStringLiteral("SAVE"),
        QStringLiteral("SavingGames"), QStringLiteral("SaveGame"), QStringLiteral("SavingLoadData"),
        QString::fromUtf8("\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96"),
        QString::fromUtf8("\xe3\x82\xbb\xe3\x83\xbc\xe3\x83\x96\xe3\x83\x87\xe3\x83\xbc\xe3\x82\xbf") };
    for (const QString &name : names)
        add(d.absoluteFilePath(name));
    // 2) Unity persistentDataPath：AppData/LocalLow/<公司>/<产品>
    const QString lowRoot = QDir::homePath() + "/AppData/LocalLow";
    const QString company = versionString(exePath, "CompanyName");
    const QString product = versionString(exePath, "ProductName");
    if (!company.isEmpty()) {
        const QString c = lowRoot + "/" + company;
        if (!product.isEmpty()) add(c + "/" + product);
        if (QDir(c).exists()) { // 公司目录下扫含 exe 名的产品目录
            const QString base = QFileInfo(exePath).completeBaseName();
            for (const QFileInfo &di : QDir(c).entryInfoList(QDir::Dirs))
                if (di.fileName().contains(base, Qt::CaseInsensitive)) add(di.absoluteFilePath());
        }
    }
    if (QDir(lowRoot).exists()) { // 公司名未知：模糊匹配 LocalLow 两级中含 exe 名的目录
        const QString base = QFileInfo(exePath).completeBaseName();
        for (const QFileInfo &ci : QDir(lowRoot).entryInfoList(QDir::Dirs))
            for (const QFileInfo &pi : QDir(ci.absoluteFilePath()).entryInfoList(QDir::Dirs))
                if (pi.fileName().contains(base, Qt::CaseInsensitive)) add(pi.absoluteFilePath());
    }
    return out;
}

QString asciiLaunchPath(const QString &exePath, QString *err)
{
    if (isPureAscii(exePath)) return exePath;
#ifdef Q_OS_WIN
    // junction 目录联接：整个游戏目录原样映射到卷根的纯 ASCII 路径——exe 文件名
    // 与全部按名字的相对查找（<exe名>_Data、BepInEx/、UnityPlayer.dll 等）保持
    // 原名不变。不用 8.3 短路径：它会把 exe 文件名截断成 XXXXXX~1，导致 Unity
    // 找不到数据目录而报 "Data folder not found"
    QFileInfo linkFi(exePath);
    QString volume = QDir(linkFi.absolutePath()).absolutePath().left(3); // 形如 "E:/"
    if (!volume.contains(':')) volume = "C:/";
    const QString linkRoot = volume + "unityTools_links";
    if (!QDir().mkpath(linkRoot)) {
        if (err) *err = QObject::tr("无法创建联接目录 %1").arg(linkRoot);
        return QString();
    }
    const QString linkPath = linkRoot + "/g" + QString::number(qHash(exePath), 36);
    if (!QFileInfo(linkPath).exists()) {
        if (QProcess::execute("cmd", QStringList() << "/c" << "mklink" << "/J"
                              << QDir::toNativeSeparators(linkPath)
                              << QDir::toNativeSeparators(QFileInfo(exePath).absolutePath())) != 0) {
            if (err) *err = QObject::tr("创建 ASCII 联接目录失败（mklink /J）");
            return QString();
        }
    }
    return linkPath + "/" + QFileInfo(exePath).fileName();
#else
    Q_UNUSED(err);
    return exePath;
#endif
}

QString findGameExe(const QString &gameDir)
{
    const QDir d(gameDir);
    for (const QFileInfo &di : d.entryInfoList(QStringList() << "*_Data", QDir::Dirs)) {
        const QString exe = d.absoluteFilePath(di.fileName().chopped(5) + ".exe");
        if (QFile::exists(exe)) return exe;
    }
    for (const QFileInfo &fi : d.entryInfoList(QStringList() << "*.exe", QDir::Files)) {
        const QString n = fi.fileName();
        if (n.contains("Crash", Qt::CaseInsensitive) || n.startsWith("unins", Qt::CaseInsensitive)
            || n.contains("UnityPlayer", Qt::CaseInsensitive))
            continue;
        return fi.absoluteFilePath();
    }
    return QString();
}

bool extractZip(const QString &zipPath, const QString &destDir, QString *err)
{
    QDir().mkpath(destDir);
    // 固定用 System32 的 bsdtar（支持 zip）；裸 "tar" 会因 PATH 命中 GNU tar 而无法解 zip
    const QString tarExe = QFile::exists("C:/Windows/System32/tar.exe")
                               ? "C:/Windows/System32/tar.exe" : "tar";
    QProcess process;
    process.start(tarExe, QStringList() << "-xf" << QDir::toNativeSeparators(zipPath)
                                       << "-C" << QDir::toNativeSeparators(destDir));
    if (!process.waitForStarted(10000)) {
        if (err) *err = QObject::tr("无法启动 tar（Windows 10 自带），请检查系统环境");
        return false;
    }
    if (!process.waitForFinished(120000)) {
        process.kill();
        process.waitForFinished(5000);
        if (err) *err = QObject::tr("解压超时（120s）");
        return false;
    }
    if (process.exitCode() != 0) {
        if (err) *err = QObject::tr("解压失败: %1").arg(QString::fromLocal8Bit(process.readAllStandardError().left(200)));
        return false;
    }
    return true;
}

bool downloadFileSync(const QString &url, const QString &dest, QString *err)
{
    QNetworkAccessManager nam;
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(url)));
    QEventLoop loop; // 防下载期间用户输入重入见 exec 处（审查 F1）
    bool timedOut = false;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(180000, &loop, [&]{ timedOut = true; reply->abort(); });
    loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (timedOut || reply->error() != QNetworkReply::NoError) {
        if (err) *err = timedOut ? QObject::tr("下载超时（180s）") : reply->errorString();
        reply->deleteLater();
        return false;
    }
    QFile f(dest);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        reply->deleteLater();
        return false;
    }
    f.write(reply->readAll());
    f.close();
    reply->deleteLater();
    return true;
}

// 本地 vendor 优先 → 镜像加速网络回退，返回最终可用的 zip 路径
static bool obtainZip(const QString &vendorPrefix, const QString &directUrl,
                      const QString &tempName, QString *zipOut, QString *err)
{
    QString zip = findVendorZip(vendorPrefix);
    if (!zip.isEmpty()) { *zipOut = zip; return true; }
    static QTemporaryDir tmpDir; // 进程内存活，多次部署共用
    if (!tmpDir.isValid()) {
        if (err) *err = QObject::tr("无法创建临时目录");
        return false;
    }
    const QString dest = tmpDir.filePath(tempName);
    if (!downloadFileSync(ghpUrl(directUrl), dest, err)) {
        if (err) *err = QObject::tr("本地 vendor 未找到 %1，且下载失败: %2").arg(vendorPrefix, *err);
        return false;
    }
    *zipOut = dest;
    return true;
}

QString findToolFile(const QString &relPath)
{
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 5; ++i) {
        const QString cand = dir.absoluteFilePath(relPath);
        if (QFile::exists(cand)) return cand;
        if (!dir.cdUp()) break;
    }
    return QString();
}

bool hasPluginDll(const QString &gameDir, const QString &nameContains)
{
    const QString plugins = gameDir + "/BepInEx/plugins";
    if (!QDir(plugins).exists()) return false;
    QDirIterator it(plugins, QStringList() << "*.dll", QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        if (QFileInfo(it.next()).fileName().contains(nameContains, Qt::CaseInsensitive))
            return true;
    return false;
}

// —— 部署状态检测（判定条件与各 ensure* 的幂等短路完全一致）——

bool isBepInExDeployed(const QString &gameDir)
{
    return QFile::exists(gameDir + "/BepInEx/core/BepInEx.dll")                // 5.x Mono
        || QFile::exists(gameDir + "/BepInEx/core/BepInEx.Core.dll")           // 6.x Mono
        || QFile::exists(gameDir + "/BepInEx/core/BepInEx.Unity.IL2CPP.dll");  // 6.x IL2CPP
}

bool isXuatDeployed(const QString &gameDir)
{
    return QFile::exists(gameDir + "/BepInEx/core/XUnity.Common.dll")
        && hasPluginDll(gameDir, "ResourceRedirector")
        && QFile::exists(gameDir + "/BepInEx/config/AutoTranslatorConfig.ini");
}

bool isDemosaicDeployed(const QString &gameDir)
{
    return hasPluginDll(gameDir, "Demosaic");
}

bool isCheatDeployed(const QString &gameDir)
{
    if (isIl2CppGame(gameDir)) return QFile::exists(gameDir + "/BepInEx/plugins/UnityToolsBridge.dll");
    return hasPluginDll(gameDir, "RuntimeUnityEditor") || QFile::exists(gameDir + "/BepInEx/plugins/UnityToolsBridge.dll");
}

// ============ 注入链路判定与 IL2CPP 部署（BepInEx 6 BE） ============

Injector detectInjector(const QString &gameDir)
{
    return isIl2CppGame(gameDir) ? Injector::Il2Cpp : Injector::Mono;
}

bool interopGenerated(const QString &gameDir)
{
    QDirIterator it(gameDir + "/BepInEx/interop", QStringList() << "*.dll", QDir::Files);
    return it.hasNext(); // 至少 1 个 dll 即视为已生成（功能必需的最小判定）
}

bool chainloaderSucceeded(const QString &gameDir)
{
    for (const QString &name : { QStringLiteral("LogOutput.log"), QStringLiteral("LogOutput.txt") }) {
        QFile f(gameDir + "/BepInEx/" + name);
        if (!f.open(QIODevice::ReadOnly)) continue;
        const bool ok = f.readAll().contains("Chainloader startup complete");
        f.close();
        if (ok) return true;
    }
    return false;
}


// Task C: 内置字体部署（复制 + 安装 + ini 写字体键；均幂等）
// XUAT 从 [Behaviour] 读 OverrideFont/FallbackFontTextMeshPro（此前误写 [Font] 段从未生效）。
// 关键约束：Unity 的 Font.GetOSInstalledFontNames 只枚举全机器级字体（HKLM/C:\Windows\Fonts），
// 用户级安装（HKCU）对 Unity 不可见 → XUAT 报 "font is not available"。
// 因此优先把 Maple 安装到机器级（需可写 C:\Windows\Fonts，无管理员权限则失败跳过），
// 安装不上时回退到机器级已存在的 CJK 字体（YaHei/SimHei/Noto/DengXian），保证不出方块。
// 机器级安装内置字体（需可写 C:\Windows\Fonts，通常要求管理员权限）。
// 被 ensureFont（部署时静默尝试）和 --install-font 提权模式（设置页按钮触发）共用
bool installFontMachineWide()
{
    const QString fontSrc = findToolFile("fonts/MapleMono-NF-CN-Regular.ttf");
    if (fontSrc.isEmpty()) return false;
    const QString machineFont = "C:/Windows/Fonts/MapleMono-NF-CN-Regular.ttf";
    if (!QFile::exists(machineFont) && !QFile::copy(fontSrc, machineFont)) return false;
    QSettings reg(R"(HKEY_LOCAL_MACHINE\Software\Microsoft\Windows NT\CurrentVersion\Fonts)",
                  QSettings::NativeFormat);
    reg.setValue("Maple Mono NF CN (TrueType)", QDir::toNativeSeparators(machineFont));
#ifdef Q_OS_WIN
    AddFontResourceW(reinterpret_cast<const wchar_t *>(machineFont.utf16()));
#endif
    return true;
}

QString pickMachineFontName(const QString &fontsDir)
{
    if (QFile::exists(fontsDir + "/MapleMono-NF-CN-Regular.ttf"))
        return QStringLiteral("Maple Mono NF CN");
    if (QFile::exists(fontsDir + "/msyh.ttc") || QFile::exists(fontsDir + "/msyhbd.ttc"))
        return QStringLiteral("Microsoft YaHei");
    if (QFile::exists(fontsDir + "/simhei.ttf"))
        return QStringLiteral("SimHei");
    if (QDirIterator(fontsDir, {"NotoSansSC*"}, QDir::Files).hasNext())
        return QStringLiteral("Noto Sans SC");
    if (QDirIterator(fontsDir, {"Deng*.ttf", "Deng*.ttc"}, QDir::Files).hasNext())
        return QStringLiteral("DengXian");
    return QStringLiteral("Microsoft YaHei"); // Win10/11 必备，最后兜底
}

bool ensureFont(const QString &gameDir, QString *err)
{
    const QString fontSrc = findToolFile("fonts/MapleMono-NF-CN-Regular.ttf");
    if (fontSrc.isEmpty()) {
        if (err) *err = QObject::tr("内置字体文件不存在");
        return false;
    }
    QDir().mkpath(gameDir + "/fonts");
    const QString fontDst = gameDir + "/fonts/MapleMono-NF-CN-Regular.ttf";
    if (!QFile::exists(fontDst)) {
        if (!QFile::copy(fontSrc, fontDst)) {
            if (err) *err = QObject::tr("字体复制失败");
            return false;
        }
    }

    // 用户选的字体（默认内置 Maple）；部署时静默尝试机器级安装，失败也无妨
    const AppConfig cfg = ConfigManager::loadConfig();
    const QString want = cfg.font_name.isEmpty() ? QStringLiteral("Maple Mono NF CN") : cfg.font_name;
    if (want == QStringLiteral("Maple Mono NF CN"))
        installFontMachineWide(); // 已装则幂等；无权限则跳过，走下面的兜底链

    // 用户级安装保留：GDI 应用可用（Unity 不枚举此层级）
    const QString fontsDir = qEnvironmentVariable("LOCALAPPDATA") + "/Microsoft/Windows/Fonts";
    if (!fontsDir.isEmpty()) {
        QDir().mkpath(fontsDir);
        const QString userFont = fontsDir + "/MapleMono-NF-CN-Regular.ttf";
        if (!QFile::exists(userFont))
            QFile::copy(fontSrc, userFont);
        QSettings reg(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows NT\CurrentVersion\Fonts)",
                      QSettings::NativeFormat);
        const QString native = QDir::toNativeSeparators(userFont);
        if (reg.value("Maple Mono NF CN (TrueType)").toString() != native)
            reg.setValue("Maple Mono NF CN (TrueType)", native);
#ifdef Q_OS_WIN
        AddFontResourceW(reinterpret_cast<const wchar_t *>(userFont.utf16()));
#endif
    }

    // 字体名必须取 Unity 能枚举到的（机器级）：
    // 指定 Maple → 机器级有就用，没有则按 YaHei/SimHei/Noto/DengXian 兜底；
    // 用户显式选了机器字体 → 原样使用
    const QString chosen = (want == QStringLiteral("Maple Mono NF CN"))
                               ? pickMachineFontName("C:/Windows/Fonts")
                               : want;
    const bool ini1 = setXuatIniValue(gameDir, "Behaviour", "OverrideFont", chosen, err);
    const bool ini2 = setXuatIniValue(gameDir, "Behaviour", "FallbackFontTextMeshPro", chosen, err);
    return ini1 && ini2;
}

bool setXuatIniValue(const QString &gameDir, const QString &section, const QString &key,
                     const QString &value, QString *err)
{
    QDir().mkpath(gameDir + "/BepInEx/config");
    const QString path = gameDir + "/BepInEx/config/AutoTranslatorConfig.ini";
    QFile f(path);
    QStringList lines;
    if (f.exists()) {
        if (!f.open(QIODevice::ReadOnly)) { if (err) *err = f.errorString(); return false; }
        lines = QString::fromUtf8(f.readAll()).split("\n");
        f.close();
    } else {
        lines << "[" + section + "]";
    }
    // 定位 section 与 key（已存在则原位改写；section 在 key 缺失时紧随节头插入）
    int sec = -1, keyLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed.startsWith("[")) {
            if (sec >= 0) break;
            if (trimmed == "[" + section + "]") sec = i;
            continue;
        }
        if (sec >= 0 && keyLine < 0 && trimmed.startsWith(key) && trimmed.contains('=')) { keyLine = i; break; }
    }
    const QString kv = key + "=" + value;
    if (keyLine >= 0) {
        if (lines[keyLine].trimmed() == kv) return true; // 幂等早退
        lines[keyLine] = kv;
    } else if (sec >= 0) {
        lines.insert(sec + 1, kv);
    } else {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) lines << "";
        lines << "[" + section + "]" << kv;
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(lines.join("\n").toUtf8());
    return true;
}

bool ensureBepInExIl2Cpp(const QString &gameDir, const QString &exePath, QString *err)
{
    if (QFile::exists(gameDir + "/BepInEx/core/BepInEx.Unity.IL2CPP.dll")) // 6.x IL2CPP 主模块
        return true;
    const quint16 machine = readPEMachineType(exePath); // 0/未知 → x64
    const int idx = (machine == 0x014C) ? 1 : 0;
    QString zip;
    if (!obtainZip(QString("BepInEx-Unity.IL2CPP-win-%1-").arg(idx == 1 ? "x86" : "x64"),
                   URL_BEPINEX_IL2CPP[idx], "BepInEx6Il2Cpp.zip", &zip, err))
        return false;
    if (!extractZip(zip, gameDir, err)) return false;
    if (!QFile::exists(gameDir + "/BepInEx/core/BepInEx.Unity.IL2CPP.dll")) {
        if (err) *err = QObject::tr("解压完成但未找到 BepInEx 6 IL2CPP 主模块");
        return false;
    }
    return true;
}

// XUAT 外部端点插件部署：源 = 工具目录 bridge/ 下编译产物，目标 = 游戏 Translators 目录。
// 源缺失（未构建对应变体）静默 false；目标已存在幂等跳过。调用方据返回值决定是否切端点。
bool ensureTranslatorPlugin(const QString &gameDir, const QString &sourceDllName,
                            const QString &deployedName, QString *err)
{
    const QString src = findToolFile("bridge/" + sourceDllName);
    if (src.isEmpty()) return false;
    const QString dir = gameDir + "/BepInEx/plugins/XUnity.AutoTranslator/Translators";
    QDir().mkpath(dir);
    const QString dst = dir + "/" + deployedName;
    QFile source(src);
    if (!source.open(QIODevice::ReadOnly)) {
        if (err) *err = source.errorString();
        return false;
    }
    QSaveFile target(dst);
    if (!target.open(QIODevice::WriteOnly) || target.write(source.readAll()) < 0 || !target.commit()) {
        if (err) *err = target.errorString();
        return false;
    }
    return true;
}

bool ensureBridge(const QString &gameDir, bool il2cpp, QString *err)
{
    const auto sourcePath = findToolFile(il2cpp ? "bridge/UnityToolsBridge-il2cpp.dll" : "bridge/UnityToolsBridge.dll");
    QFile source(sourcePath);
    if (sourcePath.isEmpty() || !source.open(QIODevice::ReadOnly)) {
        if (err) *err = sourcePath.isEmpty() ? QObject::tr("缺少 %1 Bridge 产物").arg(il2cpp ? "IL2CPP" : "Mono") : source.errorString();
        return false;
    }
    const auto bytes = source.readAll();
    const auto directory = gameDir + "/BepInEx/plugins";
    if (!QDir().mkpath(directory)) { if (err) *err = QObject::tr("无法创建 Bridge 插件目录"); return false; }
    const auto path = directory + "/UnityToolsBridge.dll";
    QFile old(path);
    if (old.open(QIODevice::ReadOnly) && old.readAll() == bytes) return true;
    old.close();
    QSaveFile target(path);
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit()) {
        if (err) *err = target.errorString();
        return false;
    }
    return true;
}

static bool configureTranslationLaunch(const QString &gameDir, const AppConfig &config, bool il2cpp, QString *err)
{
    if (config.translation_enabled && !ensureTranslatorPlugin(gameDir, il2cpp ? "UnityToolsTranslate-il2cpp.dll" : "UnityToolsTranslate.dll", "UnityToolsTranslate.dll", err)) {
        if (err && err->isEmpty()) *err = QObject::tr("缺少翻译端点产物");
        return false;
    }
    return setXuatIniValue(gameDir, "Service", "Endpoint", config.translation_enabled ? "UnityToolsTranslate" : "Passthrough", err)
        && setXuatIniValue(gameDir, "Service", "FallbackEndpoint", "", err)
        && setXuatIniValue(gameDir, "General", "MaxConcurrentTranslations", QString::number(config.max_concurrency), err)
        && setXuatIniValue(gameDir, "General", "MaxTranslationsPerRequest", QString::number(config.max_batch_lines), err);
}

bool ensureXUATIl2Cpp(const QString &gameDir, QString *err)
{
    const AppConfig scheduling = ConfigManager::loadConfig();
    const bool complete = QFile::exists(gameDir + "/BepInEx/core/XUnity.Common.dll")
        && hasPluginDll(gameDir, "BepInEx-IL2CPP")
        && QFile::exists(gameDir + "/BepInEx/config/AutoTranslatorConfig.ini");
    if (complete) {
        ensureFont(gameDir, nullptr);
        return configureTranslationLaunch(gameDir, scheduling, true, err);
    }
    QString xuat, rr;
    if (!obtainZip("XUnity.AutoTranslator-BepInEx-IL2CPP-", URL_XUAT_IL2CPP, "XUAT6.zip", &xuat, err)) return false;
    if (!obtainZip("XUnity.ResourceRedirector-BepInEx-IL2CPP-", URL_RR_IL2CPP, "RR6.zip", &rr, err)) return false;
    if (!extractZip(xuat, gameDir, err)) return false;
    if (!extractZip(rr, gameDir, err)) return false;
    if (!hasPluginDll(gameDir, "BepInEx-IL2CPP")) {
        if (err) *err = QObject::tr("解压完成但未找到 XUAT IL2CPP 插件");
        return false;
    }
    // 接管配置与 Mono 链路相同（键名见 Mono 版 ensureXUAT）
    QDir().mkpath(gameDir + "/BepInEx/config");
    const QString ini = gameDir + "/BepInEx/config/AutoTranslatorConfig.ini";
    if (!QFile::exists(ini)) {
        QFile f(ini);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (err) *err = f.errorString();
            return false;
        }
        f.write(QString("[General]\nMaxConcurrentTranslations=%1\nMaxTranslationsPerRequest=%2\n\n[Service]\nEndpoint=%3\nFallbackEndpoint=\n\n[Custom]\nUrl=http://127.0.0.1:6800\n")
                    .arg(scheduling.max_concurrency)
                    .arg(scheduling.max_batch_lines)
                    .arg(scheduling.translation_enabled ? "UnityToolsTranslate" : "Passthrough").toUtf8());
    } else {
        // 已有 ini（旧模板建的）可能缺并发键，补齐防串行
        setXuatIniValue(gameDir, "General", "MaxConcurrentTranslations",
                        QString::number(scheduling.max_concurrency), nullptr);
        setXuatIniValue(gameDir, "General", "MaxTranslationsPerRequest",
                        QString::number(scheduling.max_batch_lines), nullptr);
    }
    // TMP 中文字体回退（IL2CPP 游戏同样可能用 TMP 渲染中文）
    ensureFont(gameDir, nullptr);
    // IL2CPP 变体端点插件部署成功才切端点
    return configureTranslationLaunch(gameDir, scheduling, true, err);
}

bool ensureDemosaicIl2Cpp(const QString &gameDir, QString *err)
{
    if (hasPluginDll(gameDir, "Il2Cpp_net6"))
        return true;
    if (!QFile::exists(gameDir + "/BepInEx/core/BepInEx.Unity.IL2CPP.dll")) {
        if (err) *err = QObject::tr("需要先部署 BepInEx 6 IL2CPP 框架");
        return false;
    }
    QString zip;
    if (!obtainZip("UniversalUnityDemosaics_BepInEx6_IL2CPP_net6_", URL_UUD_IL2CPP, "UUD6.zip", &zip, err)) return false;
    if (!extractZip(zip, gameDir + "/BepInEx/plugins", err)) return false; // zip 内 dll 平铺
    if (!hasPluginDll(gameDir, "Il2Cpp_net6")) {
        if (err) *err = QObject::tr("解压完成但未找到去马赛克 IL2CPP 插件");
        return false;
    }
    return true;
}

bool ensureCheatSupportIl2Cpp(const QString &gameDir, QString *err)
{
    if (!hasPluginDll(gameDir, "Bepin6.IL2CPP")) {
        if (!QFile::exists(gameDir + "/BepInEx/core/BepInEx.Unity.IL2CPP.dll")) {
            if (err) *err = QObject::tr("需要先部署 BepInEx 6 IL2CPP 框架");
            return false;
        }
        QString zip;
        if (!obtainZip("RuntimeUnityEditor.Bepin6.IL2CPP_", URL_RUE_IL2CPP, "RUE6.zip", &zip, err)) return false;
        if (!extractZip(zip, gameDir, err)) return false; // zip 自带 BepInEx/plugins 前缀
        if (!hasPluginDll(gameDir, "Bepin6.IL2CPP")) {
            if (err) *err = QObject::tr("解压完成但未找到 RuntimeUnityEditor IL2CPP");
            return false;
        }
    }
    // IL2CPP chain uses the dedicated BepInEx 6 bridge; never deploy the Mono DLL.
    if (!ensureBridge(gameDir, true, err)) return false;
    return writeConsoleEnabled(gameDir, err);
}

bool ensureBepInEx(const QString &gameDir, const QString &exePath, QString *err)
{
    if (QFile::exists(gameDir + "/BepInEx/core/BepInEx.dll")
        || QFile::exists(gameDir + "/BepInEx/core/BepInEx.Core.dll")) // 5.x=BepInEx.dll, 6.x=BepInEx.Core.dll
        return true;
    const quint16 machine = readPEMachineType(exePath); // 0/未知 → x64（Review Focus #2）
    const int idx = (machine == 0x014C) ? 1 : 0;
    QString zip;
    if (!obtainZip(QString("BepInEx_win_%1_").arg(idx == 1 ? "x86" : "x64"), URL_BEPINEX[idx],
                   "BepInEx.zip", &zip, err))
        return false;
    if (!extractZip(zip, gameDir, err)) return false;
    if (!QFile::exists(gameDir + "/BepInEx/core/BepInEx.dll")
        && !QFile::exists(gameDir + "/BepInEx/core/BepInEx.Core.dll")) {
        if (err) *err = QObject::tr("解压完成但未找到 BepInEx 核心，zip 内容异常");
        return false;
    }
    return true;
}

bool ensureXUAT(const QString &gameDir, QString *err)
{
    const AppConfig scheduling = ConfigManager::loadConfig();
    const bool complete = QFile::exists(gameDir + "/BepInEx/core/XUnity.Common.dll")
        && hasPluginDll(gameDir, "ResourceRedirector")
        && QFile::exists(gameDir + "/BepInEx/config/AutoTranslatorConfig.ini");
    if (complete) {
        ensureFont(gameDir, nullptr);
        return configureTranslationLaunch(gameDir, scheduling, false, err);
    }
    QString xuat, rr;
    if (!obtainZip("XUnity.AutoTranslator-BepInEx-", URL_XUAT, "XUAT.zip", &xuat, err)) return false;
    if (!obtainZip("XUnity.ResourceRedirector-BepInEx-", URL_RR, "RR.zip", &rr, err)) return false;
    if (!extractZip(xuat, gameDir, err)) return false;
    if (!extractZip(rr, gameDir, err)) return false;
    if (!QFile::exists(gameDir + "/BepInEx/core/XUnity.Common.dll")) {
        if (err) *err = QObject::tr("解压完成但未找到 XUnity.Common.dll");
        return false;
    }
    // 接管：让 XUAT 走本工具内置翻译代理（键名来自 Moil README 的权威片段）
    QDir().mkpath(gameDir + "/BepInEx/config");
    const QString ini = gameDir + "/BepInEx/config/AutoTranslatorConfig.ini";
    if (!QFile::exists(ini)) {
        QFile f(ini);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (err) *err = f.errorString();
            return false;
        }
        f.write(QString("[General]\nMaxConcurrentTranslations=%1\nMaxTranslationsPerRequest=%2\n\n[Service]\nEndpoint=%3\nFallbackEndpoint=\n\n[Custom]\nUrl=http://127.0.0.1:6800\n")
                    .arg(scheduling.max_concurrency)
                    .arg(scheduling.max_batch_lines)
                    .arg(scheduling.translation_enabled ? "UnityToolsTranslate" : "Passthrough").toUtf8());
    }
    // 字体：uGUI 覆盖 + TMP 中文回退（Mono/IL2CPP 共用逻辑，见 ensureFont）
    ensureFont(gameDir, nullptr);
    // 真并发端点插件 + ini 端点兜底切换（新装 ini 已写 UnityToolsTranslate）
    return configureTranslationLaunch(gameDir, scheduling, false, err);
}

bool ensureDemosaic(const QString &gameDir, QString *err)
{
    if (hasPluginDll(gameDir, "Demosaic"))
        return true;
    QString zip;
    if (!obtainZip("UniversalUnityDemosaics_BepInEx5_", URL_UUD, "UUD.zip", &zip, err)) return false;
    // UUD zip 内 dll 平铺无目录前缀 → 解压到 plugins
    if (!extractZip(zip, gameDir + "/BepInEx/plugins", err)) return false;
    if (!hasPluginDll(gameDir, "Demosaic")) {
        if (err) *err = QObject::tr("解压完成但未找到去马赛克插件");
        return false;
    }
    return true;
}

bool writeConsoleEnabled(const QString &gameDir, QString *err)
{
    QDir().mkpath(gameDir + "/BepInEx/config");
    const QString path = gameDir + "/BepInEx/config/BepInEx.cfg";
    QFile f(path);
    if (!f.exists()) {
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            if (err) *err = f.errorString();
            return false;
        }
        f.write("[Logging.Console]\n\nEnabled=true\n");
        return true;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        if (err) *err = f.errorString();
        return false;
    }
    QStringList lines = QString::fromUtf8(f.readAll()).split("\n");
    f.close();

    // 定位 [Logging.Console] 节内 Enabled 键（兼容 "Enabled=true" / "Enabled = false" 两种真实格式）
    int section = -1, keyLine = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines[i].trimmed();
        if (trimmed == "[Logging.Console]") { section = i; continue; }
        if (section >= 0 && keyLine < 0) {
            if (trimmed.startsWith("[")) break; // 进入下一节
            if (trimmed.startsWith("Enabled") && trimmed.contains('=')) { keyLine = i; break; }
        }
    }
    if (section >= 0 && keyLine >= 0) {
        if (lines[keyLine].remove(' ').replace("Enabled=", "Enabled=") == "Enabled=true")
            return true; // 已是目标状态：幂等早退，不动文件（审查 F8）
        lines[keyLine] = "Enabled=true";
    } else if (section >= 0) {
        lines.insert(section + 1, "Enabled=true"); // 节存在无键 → 紧跟节头插入
    } else {
        if (!lines.isEmpty() && !lines.last().trimmed().isEmpty()) lines << "";
        lines << "[Logging.Console]" << "Enabled=true"; // 无节 → 追加
    }
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = f.errorString();
        return false;
    }
    f.write(lines.join("\n").toUtf8());
    return true;
}

bool ensureCheatSupport(const QString &gameDir, QString *err)
{
    const bool hasRue = hasPluginDll(gameDir, "RuntimeUnityEditor");
    if (!hasRue) {
        QString zip;
        if (!obtainZip("RuntimeUnityEditor.Bepin5_", URL_RUE, "RUE.zip", &zip, err)) return false;
        if (!extractZip(zip, gameDir, err)) return false; // zip 自带 BepInEx/plugins 前缀
        if (!hasPluginDll(gameDir, "RuntimeUnityEditor")) {
            if (err) *err = QObject::tr("解压完成但未找到 RuntimeUnityEditor");
            return false;
        }
    }
    if (!ensureBridge(gameDir, false, err)) return false;
    return writeConsoleEnabled(gameDir, err);
}

} // namespace deployment
