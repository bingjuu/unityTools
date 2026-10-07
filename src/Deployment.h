#pragma once
#include <QString>

// ==========================================
// UnityTools 部署引擎：PE 架构检测 + 幂等一键部署
// 所有 ensure* 均为幂等：已部署返回 true 且不改动任何文件。
// 真实测试：tests/test_deployment.cpp（设 UNITYTOOLS_VENDOR_DIR 走本地 vendor）
// ==========================================
namespace deployment {

// PE 头机器类型：0x014C=x86, 0x8664=x64, 0=读取失败/非 PE（调用方默认按 x64 处理）
quint16 readPEMachineType(const QString &exePath);

// 在 applicationDirPath 向上最多 6 级查找 vendor 目录（可用环境变量
// UNITYTOOLS_VENDOR_DIR 强制指定，供测试与便携布局使用），返回其中
// 文件名以 prefix 开头且以 .zip 结尾的第一个文件；找不到返回空串。
QString findVendorZip(const QString &prefix);

// 从 gameDir 定位游戏主 exe（*_Data 同名 exe 优先，其次首个 exe）；找不到返回空串。
bool isIl2CppGame(const QString &gameDir); // *_Data 下存在 il2cpp_data

// 注入链路：Mono 用 BepInEx 5（一次启动），IL2CPP 用 BepInEx 6 BE（首次需生成 interop 后二次启动）
enum class Injector { Mono, Il2Cpp };
Injector detectInjector(const QString &gameDir);
bool interopGenerated(const QString &gameDir);     // BepInEx/interop 含 dll（IL2CPP 首启完成标志）
bool chainloaderSucceeded(const QString &gameDir); // 日志含 "Chainloader startup complete"

// —— 启动辅助 ——
bool isPureAscii(const QString &s);                      // 是否纯 ASCII（中文/日文/全角符号均 false）
QString versionString(const QString &exePath, const QString &field);  // exe VERSIONINFO 字段
QStringList locateSaveDirs(const QString &gameDir, const QString &exePath); // 常见存档目录定位
QString asciiLaunchPath(const QString &exePath, QString *err); // 非 ASCII 路径 → junction 联接到卷根纯 ASCII 路径（保留 exe 名与数据目录）；失败返回空串
QString findGameExe(const QString &gameDir);

// —— 幂等部署入口 ——
bool ensureBepInEx(const QString &gameDir, const QString &exePath, QString *err);
bool ensureXUAT(const QString &gameDir, QString *err);
bool ensureDemosaic(const QString &gameDir, QString *err);
bool ensureCheatSupport(const QString &gameDir, QString *err);
bool ensureBridge(const QString &gameDir, bool il2cpp, QString *err);

// —— 部署状态检测（与 ensure* 的幂等判定一致，供启动页拖入后 ✓/✗ 展示）——
bool isBepInExDeployed(const QString &gameDir);
bool isXuatDeployed(const QString &gameDir);
bool isDemosaicDeployed(const QString &gameDir);
bool isCheatDeployed(const QString &gameDir);

// —— IL2CPP 链路（BepInEx 6 BE，与 Mono 链路严格分开）——
bool ensureBepInExIl2Cpp(const QString &gameDir, const QString &exePath, QString *err);
bool ensureXUATIl2Cpp(const QString &gameDir, QString *err);
bool ensureDemosaicIl2Cpp(const QString &gameDir, QString *err);
bool ensureCheatSupportIl2Cpp(const QString &gameDir, QString *err);

// —— XUAT 外部端点插件（真并发）部署：把 bridge 产物复制进游戏 Translators 目录 ——
bool ensureTranslatorPlugin(const QString &gameDir, const QString &sourceDllName,
                            const QString &deployedName, QString *err);

// —— 以下供内部复用与单元测试 ——
bool extractZip(const QString &zipPath, const QString &destDir, QString *err);
bool downloadFileSync(const QString &url, const QString &dest, QString *err);
QString ghpUrl(const QString &url); // GitHub 镜像加速（随机镜像前缀）
bool hasPluginDll(const QString &gameDir, const QString &nameContains); // BepInEx/plugins 递归查找
QString findToolFile(const QString &relPath); // 从 applicationDirPath 向上找随包工具文件（如 bridge/UnityToolsBridge.dll）
bool writeConsoleEnabled(const QString &gameDir, QString *err); // BepInEx.cfg [Logging.Console] Enabled=true
bool ensureFont(const QString &gameDir, QString *err); // 内置字体部署（复制+安装+ini 写 OverrideFont/FallbackFontTextMeshPro）
QString pickMachineFontName(const QString &fontsDir);  // 机器级字体名选择（Unity 只枚举机器级；测试用可传自定义目录）
bool installFontMachineWide(); // Maple 机器级安装（需管理员；--install-font 提权模式与部署时静默尝试共用）
bool setXuatIniValue(const QString &gameDir, const QString &section, const QString &key,
                      const QString &value, QString *err); // AutoTranslatorConfig.ini 幂等键写入（文件存在也更新）

} // namespace deployment
