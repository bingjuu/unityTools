#pragma once
#include <QString>
#include <QSettings>
#include <QStringList>
#include <QMap>

// 应用程序配置结构体
// Application configuration struct
struct AppConfig
{
    // 默认 API 地址
    QString api_address = "https://api.openai.com/v1";
    // 默认 API 密钥
    QString api_key;
    // 模型名称
    QString model_name = "gpt-3.5-turbo";
    // 服务端口号
    int port = 6800;
    // 系统提示词
    QString system_prompt;
    // 预设提示词
    QString pre_prompt = "将下面的文本翻译成简体中文：";
    // 上下文数量
    int context_num = 3; // 称谓/人称一致性 vs 污染的折衷
    // 温度参数
    double temperature = 0.3; // 翻译需确定性（业界 0~0.3）
    // 最大线程数
    int max_threads = 100; // Moil 文档：有效值 64~256，默认 100
    // 语言设置 (0: English, 1: Chinese)
    int language = 1;

    bool enable_debug_mode = false; // ⏱️ 测速/开发者模式开关
    bool enable_batch = false;      // 📦 打包翻译/配置文件劫持开关
    bool handle_rich_text = false;  // 📝 文本处理/富文本渲染开关 (HandleRichText)
    bool extract_newline = false;   // ↩️ 提取换行开关 (IgnoreWhitespaceInDialogue)

    // --- 术语表相关设置 ---
    // 是否开启术语表
    bool enable_glossary = false;

    // 内置种子术语表（galgame/adult/standard），编译词典时与全局表合并
    QString glossary_seed = "galgame";
    // 内置提示词术语表（galgame/adult/standard），注入提示词引导用词（只读）
    QString glossary_prompt_seed = "galgame";
    // 字体名（写入 XUAT [Behaviour] OverrideFont/FallbackFontTextMeshPro）。
    // 默认内置 Maple；若未以管理员装入机器级，部署时自动兜底为机器自带 CJK 字体
    QString font_name = "Maple Mono NF CN";
    // 翻译调度（XUAT 端点插件从游戏 ini 实时读取）：并发请求数 / 每请求打包行数
    int max_concurrency = 8;
    int max_batch_lines = 10;
    // XUAT remains deployed when false; false selects Passthrough at next launch.
    bool translation_enabled = true;

    // Agent configuration is independent from translation configuration.
    QString agent_api_address;
    QString agent_api_key;
    QString agent_model;
    QString glossary_path = "";
    // 📝 术语表历史记录
    QStringList glossary_history;
    // 自定义 API 地址列表
    QStringList custom_api_urls;

    // --- 🔥 新增：将锁定状态正式纳入配置结构体 ---
    // 锁定系统提示词
    bool lock_system_prompt = false;
    // 锁定术语表路径
    bool lock_glossary = false;

    // --- 🔥 新增：UI 持久化设置 ---
    // 0: Classic (默认), 1: Modern
    // true: Dark (默认), false: Light
    bool is_dark = true;
    // true: 圆角 (默认), false: 直角
    bool is_rounded = true;
    // 流光背景透明度 (0-255, 默认 210)
    // 毛玻璃渲染模式: 0=Frosted, 1=Legacy
    // 全局色相偏移 (0-360)
    // 全局流光浓度 (0-200, 默认 100)

    // 经典模式透明度

    // 跨模式保护屏障标志
    bool is_from_modern = false;

    // --- ⚙️ 高级设置 ---
    int max_retries = 5;
    int timeout_ms = 3000;

    // --- 💉 劫持注入设置 ---
    bool self_evo_enabled = false;          // 自进化（默认关）
    QString self_evo_endpoint;              // 整理模型端点
    QString self_evo_model;                 // 整理模型名
    QString self_evo_key;                   // 整理模型 Key（可空）
    double self_evo_temperature = 0.3;
    QString hijack_from_lang = "auto"; // auto=提示词内"源语言自动识别"
    QString hijack_to_lang = "简体中文"; // 值=自然语言名（写入 ini 并注入提示词）
    // 端点锁定 CustomTranslate（unityTools 中间件）；原 GoogleTranslate 适配已删除
    bool hijack_text_getter = false;

    // --- [TextFrameworks] 文本框架开关 ---
    bool hijack_enable_imgui = false;
    bool hijack_enable_ugui = true;
    bool hijack_enable_ui_elements = true;
    bool hijack_enable_ngui = true;
    bool hijack_enable_text_mesh_pro = true;
    bool hijack_enable_text_mesh = false;
    bool hijack_enable_fairy_gui = true;


    // 备选提示词
    AppConfig()
    {
        system_prompt = "\xe4\xbd\xa0\xe6\x98\xaf\xe4\xb8\x93\xe4\xb8\x9a\xe7\x9a\x84\xe6\xb8\xb8\xe6\x88\x8f\xe6\x96\x87\xe6\x9c\xac\xe7\xbf\xbb\xe8\xaf\x91\xe5\xbc\x95\xe6\x93\x8e\xe3\x80\x82"
                      "\xe8\xa7\x84\xe8\x8c\x83\xef\xbc\x9a"
                      "1. \xe5\x8f\xaa\xe8\xbe\x93\xe5\x87\xba\xe8\xaf\x91\xe6\x96\x87\xef\xbc\x8c\xe7\xa6\x81\xe6\xad\xa2\xe8\xa7\xa3\xe9\x87\x8a\xe3\x80\x81\xe6\xb3\xa8\xe9\x87\x8a\xe3\x80\x81\xe4\xbb\xbb\xe4\xbd\x95\xe5\x89\x8d\xe7\xbc\x80\xe3\x80\x82"
                      "2. \xe8\xbe\x93\xe5\x85\xa5\xe5\x87\xa0\xe8\xa1\x8c\xe5\xb0\xb1\xe8\xbe\x93\xe5\x87\xba\xe5\x87\xa0\xe8\xa1\x8c\xef\xbc\x8c\xe4\xbf\x9d\xe7\x95\x99\xe6\x8d\xa2\xe8\xa1\x8c\xe7\xbb\x93\xe6\x9e\x84\xe3\x80\x82"
                      "3. \xe5\x8d\xa0\xe4\xbd\x8d\xe7\xac\xa6\xe3\x80\x81Ruby \xe6\xa0\x87\xe7\xad\xbe\xe3\x80\x81HTML/\xe6\xa0\xbc\xe5\xbc\x8f\xe6\xa0\x87\xe8\xae\xb0\xe3\x80\x81\xe6\x95\xb0\xe5\xad\x97\xe4\xb8\x8e\xe7\xac\xa6\xe5\x8f\xb7\xe5\x8e\x9f\xe6\xa0\xb7\xe4\xbf\x9d\xe7\x95\x99\xe3\x80\x82"
                      "4. \xe6\x9c\xaf\xe8\xaf\xad\xe8\xa1\xa8\xef\xbc\x88\xe8\x8b\xa5\xe6\x8f\x90\xe4\xbe\x9b\xef\xbc\x89\xe4\xb8\xad\xe7\x9a\x84\xe4\xba\xba\xe5\x90\x8d/\xe4\xb8\x93\xe6\x9c\x89\xe5\x90\x8d\xe8\xaf\x8d\xe6\x8c\x89\xe8\xa1\xa8\xe7\xbf\xbb\xe8\xaf\x91\xe3\x80\x82"
                      "5. \xe8\xaf\x91\xe6\x96\x87\xe5\x8f\xa3\xe8\xaf\xad\xe3\x80\x81\xe8\x87\xaa\xe7\x84\xb6\xe3\x80\x81\xe8\xb4\xb4\xe5\x90\x88\xe8\xa7\x92\xe8\x89\xb2\xe8\xaf\xad\xe5\xa2\x83\xef\xbc\x9b\xe4\xb8\x8d\xe5\xa2\x9e\xe8\xaf\x91\xe4\xb8\x8d\xe5\x87\x8f\xe8\xaf\x91\xef\xbc\x9b\xe4\xb8\x8d\xe7\xa1\xae\xe5\xae\x9a\xe6\x97\xb6\xe7\xbb\x99\xe6\x9c\x80\xe6\x8e\xa5\xe8\xbf\x91\xe7\x9a\x84\xe5\x90\x88\xe7\x90\x86\xe8\xaf\x91\xe6\x96\x87\xef\xbc\x8c\xe7\xbb\x9d\xe4\xb8\x8d\xe7\x95\x99\xe7\xa9\xba\xe3\x80\x82";
    }
};

class ConfigManager
{
public:
    static AppConfig loadConfig(const QString &filename = "config.ini");
    static void saveConfig(const AppConfig &config, const QString &filename = "config.ini");

    // 按 base_url 记忆并读取 API key（自动去除末尾 /）
    static QString loadApiKeyForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");
    static void saveApiKeyForBaseUrl(const QString &baseUrl, const QString &apiKey, const QString &filename = "config.ini");
    static void removeApiKeyForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");

    // 按 base_url 记忆并读取模型名（自动去除末尾 /）
    static QString loadModelForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");
    static void saveModelForBaseUrl(const QString &baseUrl, const QString &modelName, const QString &filename = "config.ini");
    static void removeModelForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");

    // 按 base_url 记忆并读取预设名称（自动去除末尾 /）
    static QString loadPresetNameForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");
    static void savePresetNameForBaseUrl(const QString &baseUrl, const QString &presetName, const QString &filename = "config.ini");
    static void removePresetNameForBaseUrl(const QString &baseUrl, const QString &filename = "config.ini");
};